#include "notecard.h"

#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "note-c/note.h"

#define XFER_TIMEOUT_MS 100
#define CHUNK_MAX 250  // bytes of payload per I2C write or read (protocol header excluded)

static struct {
    i2c_master_bus_handle_t bus;
    bool own_bus;
    i2c_master_dev_handle_t dev;
} s;

// ------------------------------------------------------------ note-c hooks

static void *hook_malloc(size_t size)
{
    return malloc(size);
}

static void hook_free(void *p)
{
    free(p);
}

static void hook_delay_ms(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t ? t : 1);
}

static uint32_t hook_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool hook_i2c_reset(uint16_t address)
{
    (void)address;
    return s.dev != NULL;
}

// Serial-over-I2C: a write is one length byte, then the payload.
static const char *hook_i2c_transmit(uint16_t address, uint8_t *tx, uint16_t size)
{
    (void)address;
    uint8_t buf[CHUNK_MAX + 1];
    if (size > CHUNK_MAX) {
        return "i2c: chunk too large {io}";
    }
    buf[0] = (uint8_t)size;
    memcpy(buf + 1, tx, size);
    if (i2c_master_transmit(s.dev, buf, size + 1, XFER_TIMEOUT_MS) != ESP_OK) {
        return "i2c: transmit failed {io}";
    }
    return NULL;
}

// A read asks for size bytes with {0, size}, then reads two header bytes
// (bytes still waiting, bytes in this read) and the payload.
static const char *hook_i2c_receive(uint16_t address, uint8_t *rx, uint16_t size,
                                    uint32_t *available)
{
    (void)address;
    uint8_t ask[2] = {0, (uint8_t)size};
    uint8_t buf[CHUNK_MAX + 2];
    if (size > CHUNK_MAX) {
        return "i2c: chunk too large {io}";
    }
    if (i2c_master_transmit(s.dev, ask, sizeof ask, XFER_TIMEOUT_MS) != ESP_OK) {
        return "i2c: read request failed {io}";
    }
    vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);  // the Notecard fills its buffer
    if (i2c_master_receive(s.dev, buf, size + 2, XFER_TIMEOUT_MS) != ESP_OK) {
        return "i2c: receive failed {io}";
    }
    if (buf[1] != size) {
        return "i2c: unexpected byte count {io}";
    }
    memcpy(rx, buf + 2, size);
    *available = buf[0];
    return NULL;
}

// ------------------------------------------------------------------ driver

static esp_err_t open_bus(const notecard_config_t *cfg)
{
    if (i2c_master_get_bus_handle(cfg->i2c_port, &s.bus) != ESP_OK) {
        i2c_master_bus_config_t bc = {
            .i2c_port = cfg->i2c_port,
            .sda_io_num = cfg->sda_gpio,
            .scl_io_num = cfg->scl_gpio,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = cfg->internal_pullup,
        };
        ESP_TRY(i2c_new_master_bus(&bc, &s.bus));
        s.own_bus = true;
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->address,
        .scl_speed_hz = cfg->clk_hz,
    };
    return i2c_master_bus_add_device(s.bus, &dc, &s.dev);
}

esp_err_t notecard_start(const notecard_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_bus(cfg);
    if (err != ESP_OK) {
        notecard_stop();
        return err;
    }
    NoteSetFnDefault(hook_malloc, hook_free, hook_delay_ms, hook_millis);
    NoteSetFnI2C(cfg->address, CHUNK_MAX, hook_i2c_reset, hook_i2c_transmit, hook_i2c_receive);
    NoteSetRequestTimeout(cfg->timeout_s);
    return ESP_OK;
}

esp_err_t notecard_request(const char *req, char *rsp, size_t rsp_size)
{
    if (req == NULL || rsp == NULL || rsp_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    rsp[0] = '\0';
    char *answer = NoteRequestResponseJSON(req);
    if (answer == NULL) {
        return ESP_ERR_TIMEOUT;
    }
    size_t len = strlen(answer);
    while (len > 0 && (answer[len - 1] == '\n' || answer[len - 1] == '\r')) {
        len--;
    }
    esp_err_t err = ESP_OK;
    if (len >= rsp_size) {
        len = rsp_size - 1;
        err = ESP_ERR_INVALID_SIZE;
    }
    memcpy(rsp, answer, len);
    rsp[len] = '\0';
    NoteFree(answer);
    return err;
}

esp_err_t notecard_stop(void)
{
    esp_err_t err = ESP_OK;
    if (s.dev != NULL) {
        NoteSetFnDisabled();
        err = i2c_master_bus_rm_device(s.dev);
        s.dev = NULL;
    }
    if (s.bus != NULL && s.own_bus) {
        esp_err_t e = i2c_del_master_bus(s.bus);
        err = err != ESP_OK ? err : e;
    }
    s.bus = NULL;
    s.own_bus = false;
    return err;
}
