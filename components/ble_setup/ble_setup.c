// BLE setup (see ble_setup.h): NimBLE port, GAP advertising, the GATT table and
// the security manager. The decisions on bytes live in ble_setup_core.c.
#include "ble_setup.h"

#include <stdlib.h>
#include <string.h>

#include "ble_setup_core.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#if !CONFIG_BT_NIMBLE_GATT_CLIENT
// A phone sends an indication (Service Changed) on connecting; only the GATT
// client confirms it, and an unconfirmed one makes the phone drop the link
// after 30 s. It needs CONFIG_BT_NIMBLE_ROLE_CENTRAL=y.
#error "ble_setup needs CONFIG_BT_NIMBLE_GATT_CLIENT=y"
#endif

static const char *TAG = "ble_setup";

void ble_store_config_init(void);  // NimBLE's RAM/NVS key store; no public header

#define RESULT_MAX 64
#define BOND_NAMESPACE "nimble_bond"  // NimBLE's NVS store (CONFIG_BT_NIMBLE_NVS_PERSIST)

// 6e0a00xx-7d3b-4c2a-9f1e-54445333424c, little-endian as NimBLE stores it.
#define BS_UUID(x) BLE_UUID128_INIT(0x4c, 0x42, 0x33, 0x53, 0x44, 0x54, 0x1e, 0x9f, \
                                    0x2a, 0x4c, 0x3b, 0x7d, (x), 0x00, 0x0a, 0x6e)
static const ble_uuid128_t svc_uuid = BS_UUID(0x01);
static const ble_uuid128_t info_uuid = BS_UUID(0x02);
static const ble_uuid128_t settings_uuid = BS_UUID(0x03);
static const ble_uuid128_t result_uuid = BS_UUID(0x04);
static const ble_uuid128_t control_uuid = BS_UUID(0x05);
static const ble_uuid128_t history_uuid = BS_UUID(0x06);

static const ble_setup_config_t *cfg;
static bool running;
static uint8_t own_addr_type;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t result_handle;
// The read buffer holds a settings read or the field rules, whichever is larger.
#define READ_MAX (CONFIG_BLE_SETUP_FORM_MAX > CONFIG_BLE_SETUP_BODY_MAX ? CONFIG_BLE_SETUP_FORM_MAX \
                                                                 : CONFIG_BLE_SETUP_BODY_MAX)

static char *stage_buf;  // CONFIG_BLE_SETUP_BODY_MAX + 1
static char *read_buf;   // READ_MAX
static bs_session_t session;
static char result[RESULT_MAX] = "none";
static SemaphoreHandle_t host_stopped;
static ble_store_read_fn *store_read;  // the RAM store's own reader
static struct ble_npl_event drop_ev;   // drops the link of a central with a stale key
static struct ble_npl_event forget_ev; // erases every bond, after the write is answered
static bool stale_key;                 // this link's central presented one
static bool pairing;                   // a pairing reached its passkey step on this link
static struct ble_npl_callout sec_co;  // the delayed Security Request

// How long a central gets to start pairing by itself before it is asked.
#define SEC_REQ_DELAY_MS 3000

static void advertise(void);

// A central that starts encryption with a key it remembers but this device
// does not hold (no bonds kept, CONFIG_BLE_SETUP_BOND off, or the bond was
// forgotten here, e.g. by a factory reset): NimBLE looks the key
// up (our security record for that peer), finds none and answers with a
// negative reply, but leaves the link up (older NimBLE even waits 30 s for an
// encryption change), so the central sits on an unencrypted link whose reads
// it cannot get answered and never pairs again by itself. Drop the link at once
// instead, so the central learns the key is gone (Android then forgets it) and
// its next connection pairs afresh; the setup page reconnects once by itself.
// A lookup after a pairing has started on the link (the passkey step) is
// NimBLE's own bookkeeping, not a restore, and is left alone.
// Runs in the host task with the host lock held, so the drop is posted.
static int read_store(int obj_type, const union ble_store_key *key, union ble_store_value *val)
{
    int rc = store_read(obj_type, key, val);
    if (obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC || obj_type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        ESP_LOGI(TAG, "store read %s sec: %d", obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC ? "our" : "peer", rc);
    }
    if (rc == BLE_HS_ENOENT && obj_type == BLE_STORE_OBJ_TYPE_OUR_SEC &&
        conn != BLE_HS_CONN_HANDLE_NONE && !pairing && !stale_key) {
        stale_key = true;
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &drop_ev);
    }
    return rc;
}

static void drop_stale(struct ble_npl_event *ev)
{
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "central has a key from an earlier pairing: disconnecting");
        ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL);
    }
}

// Ask for pairing (Security Request) only when the central has not started
// it by itself: some stacks (BlueZ under Chrome) never pair on an
// Insufficient Authentication error, while Android, after an earlier
// pairing, leaves a Security Request sent at connection unanswered and both
// sides time out after 30 s. Android pairs on the page's first protected read.
static void request_security(struct ble_npl_event *ev)
{
    struct ble_gap_conn_desc desc;
    if (conn == BLE_HS_CONN_HANDLE_NONE || pairing || ble_gap_conn_find(conn, &desc) != 0 ||
        desc.sec_state.encrypted) {
        return;
    }
    int sec = ble_gap_security_initiate(conn);
    ESP_LOGI(TAG, "security request: %d", sec);
}

static void emit(ble_setup_event_t ev)
{
    if (cfg->on_event) {
        cfg->on_event(ev);
    }
}

static void forget_all(struct ble_npl_event *ev)
{
    int rc = ble_store_clear();
    ESP_LOGI(TAG, "bonds erased: %d", rc);
    if (rc == 0) {
        emit(BLE_SETUP_EV_FORGOTTEN);
    }
}

static void passkey_down(void)
{
    if (cfg->on_passkey) {
        cfg->on_passkey(0, false);
    }
}

static int append(struct os_mbuf *om, const char *p, size_t n)
{
    return os_mbuf_append(om, p, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access(uint16_t conn_handle, uint16_t attr_handle,
                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(uuid, &info_uuid.u) == 0) {
            size_t n = cfg->read_info ? cfg->read_info(read_buf, BS_ATTR_MAX) : 0;
            return append(ctxt->om, read_buf, n);
        }
        if (ble_uuid_cmp(uuid, &settings_uuid.u) == 0) {
            size_t (*src)(char *, size_t) = session.read_form ? cfg->read_form
                                                              : cfg->read_settings;
            size_t all = src ? src(read_buf, session.read_form ? READ_MAX
                                                               : CONFIG_BLE_SETUP_BODY_MAX)
                             : 0;
            const char *start;
            size_t n = bs_page(read_buf, all, session.read_off, &start);
            return append(ctxt->om, start, n);
        }
        if (ble_uuid_cmp(uuid, &result_uuid.u) == 0) {
            return append(ctxt->om, result, strlen(result));
        }
        if (ble_uuid_cmp(uuid, &history_uuid.u) == 0) {
            size_t n = cfg->read_history
                           ? cfg->read_history(session.hist_line, read_buf, BS_ATTR_MAX) : 0;
            return append(ctxt->om, read_buf, n);
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > BS_ATTR_MAX) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    static char chunk[BS_ATTR_MAX];  // host task only
    uint16_t got = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, chunk, sizeof chunk, &got) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ble_uuid_cmp(uuid, &settings_uuid.u) == 0) {
        return bs_append(&session, chunk, got) ? 0 : BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (ble_uuid_cmp(uuid, &control_uuid.u) == 0) {
        uint32_t off = 0;
        switch (bs_control(chunk, got, &off)) {
        case BS_CTRL_APPLY:
            result[0] = '\0';
            cfg->on_apply(session.buf, session.len, result, sizeof result);
            result[sizeof result - 1] = '\0';
            bs_reset(&session);
            ble_gatts_chr_updated(result_handle);
            return 0;
        case BS_CTRL_DONE:
            emit(BLE_SETUP_EV_DONE);
            return 0;
        case BS_CTRL_FORGET:
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &forget_ev);
            return 0;
        case BS_CTRL_READ:
            session.read_off = off;
            session.read_form = false;
            return 0;
        case BS_CTRL_FORM:
            if (!cfg->read_form) {
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;  // the page then checks nothing itself
            }
            session.read_off = off;
            session.read_form = true;
            return 0;
        case BS_CTRL_HIST:
            session.hist_line = off;
            return 0;
        case BS_CTRL_RANGE:
            if (!cfg->on_history_range || !cfg->on_history_range(chunk + off, got - off)) {
                return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            }
            session.hist_line = 0;
            return 0;
        default:
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

#define PAIRED (BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN)
#define PAIRED_W (BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = &info_uuid.u, .access_cb = access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &settings_uuid.u, .access_cb = access,
             .flags = BLE_GATT_CHR_F_READ | PAIRED | BLE_GATT_CHR_F_WRITE | PAIRED_W},
            {.uuid = &result_uuid.u, .access_cb = access, .val_handle = &result_handle,
             .flags = BLE_GATT_CHR_F_READ | PAIRED | BLE_GATT_CHR_F_NOTIFY},
            {.uuid = &control_uuid.u, .access_cb = access,
             .flags = BLE_GATT_CHR_F_WRITE | PAIRED_W},
            {.uuid = &history_uuid.u, .access_cb = access,
             .flags = BLE_GATT_CHR_F_READ | PAIRED},
            {0},
        },
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            advertise();
            return 0;
        }
        conn = event->connect.conn_handle;
        ESP_LOGI(TAG, "connected");
        stale_key = pairing = false;
        bs_reset(&session);
        strcpy(result, "none");
        emit(BLE_SETUP_EV_CONNECTED);
        ble_npl_callout_reset(&sec_co, ble_npl_time_ms_to_ticks32(SEC_REQ_DELAY_MS));
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected: reason 0x%x", event->disconnect.reason);
        conn = BLE_HS_CONN_HANDLE_NONE;
        ble_npl_callout_stop(&sec_co);
        bs_reset(&session);
        passkey_down();
        emit(BLE_SETUP_EV_DISCONNECTED);
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (conn == BLE_HS_CONN_HANDLE_NONE) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        ESP_LOGI(TAG, "encryption change: status 0x%x", event->enc_change.status);
        passkey_down();
        struct ble_gap_conn_desc desc = {0};
        bool ok = event->enc_change.status == 0 &&
                  ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
                  desc.sec_state.encrypted && desc.sec_state.authenticated;
        if (!ok) {
            ESP_LOGW(TAG, "pairing failed: status 0x%x encrypted %d authenticated %d",
                     event->enc_change.status, desc.sec_state.encrypted, desc.sec_state.authenticated);
        }
        if (!ok && stale_key) {
            return 0;  // a stale key, not a pairing: drop_stale() ends the link
        }
        emit(ok ? BLE_SETUP_EV_PAIRED : BLE_SETUP_EV_PAIR_FAILED);
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI(TAG, "passkey action %d", event->passkey.params.action);  // never the passkey
        pairing = true;
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) {
            return 0;  // DisplayOnly: no other action is negotiated
        }
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
        if (cfg->on_passkey) {
            cfg->on_passkey(io.passkey, true);
        }
        int rc = ble_sm_inject_io(event->passkey.conn_handle, &io);
        if (rc != 0) {
            ESP_LOGW(TAG, "passkey inject failed: %d", rc);
        }
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The central forgot its bond and pairs again: replace ours.
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        ESP_LOGI(TAG, "gap event %d", event->type);
        return 0;
    }
}

static void advertise(void)
{
    if (!running || conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) {
        return;
    }
    struct ble_hs_adv_fields adv = {0};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &svc_uuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (const uint8_t *)cfg->name;
    rsp.name_len = (uint8_t)strlen(cfg->name);
    rsp.name_is_complete = 1;
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(CONFIG_BLE_SETUP_ADV_INTERVAL_MS),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(CONFIG_BLE_SETUP_ADV_INTERVAL_MS),
    };
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_fields(&rsp);
    }
    if (rc == 0) {
        rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising failed: %d", rc);
    }
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        ESP_LOGE(TAG, "no usable address");
        return;
    }
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset: %d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();  // returns after nimble_port_stop()
    xSemaphoreGive(host_stopped);
    nimble_port_freertos_deinit();
}

static void release(void)
{
    free(stage_buf);
    free(read_buf);
    stage_buf = read_buf = NULL;
}

esp_err_t ble_setup_start(const ble_setup_config_t *c)
{
    if (running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!c || !c->name || !c->on_apply) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host_stopped && !(host_stopped = xSemaphoreCreateBinary())) {
        return ESP_ERR_NO_MEM;
    }
    stage_buf = malloc(CONFIG_BLE_SETUP_BODY_MAX + 1);
    read_buf = malloc(READ_MAX);
    if (!stage_buf || !read_buf) {
        release();
        return ESP_ERR_NO_MEM;
    }
    cfg = c;
    bs_init(&session, stage_buf, CONFIG_BLE_SETUP_BODY_MAX);
    strcpy(result, "none");
    conn = BLE_HS_CONN_HANDLE_NONE;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        release();
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 1;
#if CONFIG_BLE_SETUP_BOND
    // Android keeps its half of a pairing whether or not the peripheral asks
    // for bonding, and then stalls on the next connection; keeping ours lets
    // it reconnect by encryption alone.
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#else
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;
#endif

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(services);
    }
    if (rc == 0) {
        rc = ble_svc_gap_device_name_set(c->name);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT setup failed: %d", rc);
        nimble_port_deinit();
        release();
        return ESP_FAIL;
    }
    ble_store_config_init();
    store_read = ble_hs_cfg.store_read_cb;
    ble_hs_cfg.store_read_cb = read_store;
    ble_npl_event_init(&drop_ev, drop_stale, NULL);
    ble_npl_event_init(&forget_ev, forget_all, NULL);
    ble_npl_callout_init(&sec_co, nimble_port_get_dflt_eventq(), request_security, NULL);
    running = true;
    xSemaphoreTake(host_stopped, 0);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t ble_setup_stop(void)
{
    if (!running) {
        return ESP_OK;
    }
    running = false;  // advertise() is a no-op from here on
    if (nimble_port_stop() == 0) {
        xSemaphoreTake(host_stopped, pdMS_TO_TICKS(2000));
    }
    ble_npl_callout_stop(&sec_co);
    ble_npl_callout_deinit(&sec_co);
    ble_npl_eventq_remove(nimble_port_get_dflt_eventq(), &drop_ev);
    ble_npl_event_deinit(&drop_ev);
    ble_npl_eventq_remove(nimble_port_get_dflt_eventq(), &forget_ev);
    ble_npl_event_deinit(&forget_ev);
    esp_err_t err = nimble_port_deinit();
    conn = BLE_HS_CONN_HANDLE_NONE;
    release();
    return err;
}

bool ble_setup_running(void)
{
    return running;
}

esp_err_t ble_setup_forget(void)
{
    if (running) {
        return ble_store_clear() == 0 ? ESP_OK : ESP_FAIL;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(BOND_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  // never bonded
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
