#include "bme280.h"

#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define XFER_TIMEOUT_MS 50

static struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    bme280_settings_t settings;
    bme280_calib_t calib;
} s;

static int bus_read(void *ctx, uint8_t reg, uint8_t *buf, int len)
{
    return i2c_master_transmit_receive(s.dev, &reg, 1, buf, len, XFER_TIMEOUT_MS);
}

static int bus_write(void *ctx, uint8_t reg, uint8_t value)
{
    uint8_t b[2] = {reg, value};
    return i2c_master_transmit(s.dev, b, sizeof b, XFER_TIMEOUT_MS);
}

static void bus_delay_ms(void *ctx, uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

static const bme280_bus_t bus = {bus_read, bus_write, bus_delay_ms, NULL};

static esp_err_t esp_of(int err)
{
    switch (err) {
    case BME280_E_CHIP_ID:
        return ESP_ERR_NOT_FOUND;
    case BME280_E_BUSY:
        return ESP_ERR_TIMEOUT;
    default:
        return (esp_err_t)err;  // 0 or the I2C driver's own code
    }
}

static esp_err_t open_bus(const bme280_config_t *cfg)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = cfg->i2c_port,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = cfg->internal_pullup,
    };
    ESP_TRY(i2c_new_master_bus(&bc, &s.bus));
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->address,
        .scl_speed_hz = cfg->clk_hz,
    };
    return i2c_master_bus_add_device(s.bus, &dc, &s.dev);
}

esp_err_t bme280_start(const bme280_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.bus != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_bus(cfg);
    if (err == ESP_OK) {
        s.settings = cfg->settings;
        err = esp_of(bme280_core_init(&bus, &s.settings, &s.calib));
    }
    if (err != ESP_OK) {
        bme280_stop();
    }
    return err;
}

esp_err_t bme280_read(bme280_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_of(bme280_core_read(&bus, &s.settings, &s.calib, out));
}

esp_err_t bme280_stop(void)
{
    esp_err_t err = ESP_OK;
    if (s.dev != NULL) {
        err = i2c_master_bus_rm_device(s.dev);
        s.dev = NULL;
    }
    if (s.bus != NULL) {
        esp_err_t e = i2c_del_master_bus(s.bus);
        err = err != ESP_OK ? err : e;
        s.bus = NULL;
    }
    return err;
}
