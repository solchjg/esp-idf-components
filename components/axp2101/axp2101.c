#include "axp2101.h"

#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_try.h"

#define XFER_TIMEOUT_MS 100

static struct {
    i2c_master_bus_handle_t bus;
    bool own_bus;
    i2c_master_dev_handle_t dev;
} s;

static int bus_read8(void *ctx, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s.dev, &reg, 1, value, 1, XFER_TIMEOUT_MS);
}

static int bus_write8(void *ctx, uint8_t reg, uint8_t value)
{
    uint8_t b[2] = {reg, value};
    return i2c_master_transmit(s.dev, b, sizeof b, XFER_TIMEOUT_MS);
}

static const axp2101_bus_t bus = {bus_read8, bus_write8, NULL};

static esp_err_t esp_of(int err)
{
    switch (err) {
    case AXP2101_E_ARG:
        return ESP_ERR_INVALID_ARG;
    case AXP2101_E_CHIP:
        return ESP_ERR_NOT_FOUND;
    default:
        return (esp_err_t)err;  // 0 or the I2C driver's own code
    }
}

// The master bus another driver already opened on port. A port with none is
// the normal case here, so the I2C driver's error log for it is held back.
static bool shared_bus(int port, i2c_master_bus_handle_t *b)
{
    esp_log_level_t was = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    bool ok = i2c_master_get_bus_handle(port, b) == ESP_OK;
    esp_log_level_set("i2c.master", was);
    return ok;
}

static esp_err_t open_bus(const axp2101_config_t *cfg)
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
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = cfg->clk_hz,
    };
    return i2c_master_bus_add_device(s.bus, &dc, &s.dev);
}

esp_err_t axp2101_start(const axp2101_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_bus(cfg);
    if (err == ESP_OK) {
        err = esp_of(axp2101_core_start(&bus));
    }
    if (err != ESP_OK) {
        axp2101_stop();
    }
    return err;
}

esp_err_t axp2101_rail(axp2101_rail_t rail, unsigned mv, bool on)
{
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_of(axp2101_core_rail(&bus, rail, mv, on));
}

esp_err_t axp2101_vbat_mv(unsigned *mv)
{
    if (mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_of(axp2101_core_vbat_mv(&bus, mv));
}

esp_err_t axp2101_status(axp2101_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_of(axp2101_core_status(&bus, status));
}

esp_err_t axp2101_stop(void)
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
