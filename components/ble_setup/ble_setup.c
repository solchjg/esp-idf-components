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
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_setup";

void ble_store_config_init(void);  // NimBLE's RAM/NVS key store; no public header

#define RESULT_MAX 64

// 6e0a00xx-7d3b-4c2a-9f1e-54445333424c, little-endian as NimBLE stores it.
#define BS_UUID(x) BLE_UUID128_INIT(0x4c, 0x42, 0x33, 0x53, 0x44, 0x54, 0x1e, 0x9f, \
                                    0x2a, 0x4c, 0x3b, 0x7d, (x), 0x00, 0x0a, 0x6e)
static const ble_uuid128_t svc_uuid = BS_UUID(0x01);
static const ble_uuid128_t info_uuid = BS_UUID(0x02);
static const ble_uuid128_t settings_uuid = BS_UUID(0x03);
static const ble_uuid128_t result_uuid = BS_UUID(0x04);
static const ble_uuid128_t control_uuid = BS_UUID(0x05);

static const ble_setup_config_t *cfg;
static bool running;
static uint8_t own_addr_type;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t result_handle;
static char *stage_buf;  // CONFIG_BLE_SETUP_BODY_MAX + 1
static char *read_buf;   // CONFIG_BLE_SETUP_BODY_MAX
static bs_session_t session;
static char result[RESULT_MAX] = "none";
static SemaphoreHandle_t host_stopped;

static void advertise(void);

static void emit(ble_setup_event_t ev)
{
    if (cfg->on_event) {
        cfg->on_event(ev);
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
            size_t all = cfg->read_settings
                             ? cfg->read_settings(read_buf, CONFIG_BLE_SETUP_BODY_MAX) : 0;
            const char *start;
            size_t n = bs_page(read_buf, all, session.read_off, &start);
            return append(ctxt->om, start, n);
        }
        if (ble_uuid_cmp(uuid, &result_uuid.u) == 0) {
            return append(ctxt->om, result, strlen(result));
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
        case BS_CTRL_READ:
            session.read_off = off;
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
        bs_reset(&session);
        strcpy(result, "none");
        emit(BLE_SETUP_EV_CONNECTED);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        conn = BLE_HS_CONN_HANDLE_NONE;
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
        passkey_down();
        struct ble_gap_conn_desc desc;
        bool ok = event->enc_change.status == 0 &&
                  ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
                  desc.sec_state.encrypted && desc.sec_state.authenticated;
        emit(ok ? BLE_SETUP_EV_PAIRED : BLE_SETUP_EV_PAIR_FAILED);
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
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
        // No bonds are kept; a central that remembers one pairs afresh.
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
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
    read_buf = malloc(CONFIG_BLE_SETUP_BODY_MAX);
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
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

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
    esp_err_t err = nimble_port_deinit();
    conn = BLE_HS_CONN_HANDLE_NONE;
    release();
    return err;
}

bool ble_setup_running(void)
{
    return running;
}
