#include "ads1115.h"

#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static int bus_write16(void *ctx, uint8_t reg, uint16_t value)
{
    uint8_t b[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_transmit(s.dev, b, sizeof b, XFER_TIMEOUT_MS);
}

// Whole ticks asleep, the rest busy: a conversion's 9 ms at 128 SPS is shorter
// than a 10 ms tick, and the caller may be timing a supply that is on meanwhile.
static void bus_delay_ms(void *ctx, uint32_t ms)
{
    uint32_t ticks = ms / portTICK_PERIOD_MS;
    if (ticks) {
        vTaskDelay(ticks);
    }
    uint32_t rest = ms - ticks * portTICK_PERIOD_MS;
    if (rest) {
        esp_rom_delay_us(rest * 1000);
    }
}

static const ads1115_bus_t bus = {bus_read16, bus_write16, bus_delay_ms, NULL};

static esp_err_t esp_of(int err)
{
    switch (err) {
    case ADS1115_E_BUSY:
        return ESP_ERR_TIMEOUT;
    case ADS1115_E_ARG:
        return ESP_ERR_INVALID_ARG;
    default:
        return (esp_err_t)err;  // 0 or the I2C driver's own code
    }
}

static esp_err_t open_bus(const ads1115_config_t *cfg)
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

esp_err_t ads1115_start(const ads1115_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_bus(cfg);
    if (err == ESP_OK) {
        uint16_t cfg_reg;
        err = esp_of(bus_read16(NULL, ADS1115_REG_CONFIG, &cfg_reg));
    }
    if (err != ESP_OK) {
        ads1115_stop();
    }
    return err;
}

esp_err_t ads1115_read_mean(const ads1115_settings_t *settings, int n, int32_t *mean_uv)
{
    if (settings == NULL || mean_uv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_of(ads1115_core_read_mean(&bus, settings, n, mean_uv));
}

esp_err_t ads1115_stop(void)
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
