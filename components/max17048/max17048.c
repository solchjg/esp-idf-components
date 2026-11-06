#include "max17048.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_try.h"

#define XFER_TIMEOUT_MS 50

static struct {
    i2c_master_bus_handle_t bus;
    bool own_bus;
    i2c_master_dev_handle_t dev;
} s;

static int bus_read16(void *ctx, uint8_t reg, uint16_t *value)
{
    uint8_t b[2];
    int err = i2c_master_transmit_receive(s.dev, &reg, 1, b, sizeof b, XFER_TIMEOUT_MS);
    if (err == 0) {
        *value = (uint16_t)(b[0] << 8 | b[1]);
    }
    return err;
}

static const max17048_bus_t bus = {bus_read16, NULL};

static esp_err_t esp_of(int err)
{
    return err == MAX17048_E_ID ? ESP_ERR_NOT_FOUND : (esp_err_t)err;
}

// The master bus another driver already opened on port. A port with none is
// a normal case here, so the I2C driver's error log for it is held back.
static bool shared_bus(int port, i2c_master_bus_handle_t *b)
{
    esp_log_level_t was = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    bool ok = i2c_master_get_bus_handle(port, b) == ESP_OK;
    esp_log_level_set("i2c.master", was);
    return ok;
}

static esp_err_t open_bus(const max17048_config_t *cfg)
{
    if (!shared_bus(cfg->i2c_port, &s.bus)) {
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
        .device_address = MAX17048_ADDR,
        .scl_speed_hz = cfg->clk_hz,
    };
    return i2c_master_bus_add_device(s.bus, &dc, &s.dev);
}

esp_err_t max17048_start(const max17048_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_bus(cfg);
    if (err == ESP_OK) {
        err = esp_of(max17048_core_check(&bus));
    }
    if (err != ESP_OK) {
        max17048_stop();
    }
    return err;
}

esp_err_t max17048_read(max17048_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_of(max17048_core_read(&bus, out));
}

esp_err_t max17048_stop(void)
{
    esp_err_t err = ESP_OK;
    if (s.dev != NULL) {
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
