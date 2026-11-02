// ADS1115 16-bit ADC on I2C, single-shot conversions. Board-agnostic: every
// pin and setting comes from ads1115_config_t. It joins the master bus already
// open on the port (e.g. the one another sensor's driver created) or, if none
// is, opens its own and releases it again at ads1115_stop. One ADC per
// firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts: what happens on
// failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ads1115_core.h"
#include "esp_err.h"

typedef struct {
    int i2c_port;          // I2C controller, e.g. 0
    int sda_gpio, scl_gpio;  // used only when no bus is open on the port
    uint8_t address;       // 0x48 (ADDR to GND), 0x49 (VDD), 0x4A (SDA), 0x4B (SCL)
    uint32_t clk_hz;       // e.g. 100000
    bool internal_pullup;  // only for a bus the driver opens itself
} ads1115_config_t;

// Device setup and a read of the config register, so an absent chip fails
// here. The bus driver's error if it does not answer; on any failure what was
// set up is released again.
esp_err_t ads1115_start(const ads1115_config_t *cfg);

// n (1 – ADS1115_MEAN_MAX) single-shot conversions; *mean_uv their mean in
// microvolts. ESP_ERR_INVALID_STATE before a successful start; ESP_ERR_TIMEOUT
// if a conversion does not finish; ESP_ERR_INVALID_ARG for n out of range.
esp_err_t ads1115_read_mean(const ads1115_settings_t *s, int n, int32_t *mean_uv);

// Release the device, and the bus if the driver opened it.
esp_err_t ads1115_stop(void);
