// BME280 temperature, humidity and pressure sensor on I2C, read in forced
// mode: one measurement per bme280_read, the sensor asleep in between.
// Board-agnostic: every pin and setting comes from bme280_config_t. One sensor
// per firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts: what happens on
// failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bme280_core.h"
#include "esp_err.h"

typedef struct {
    int i2c_port;          // I2C controller, e.g. 0; -1 lets the driver choose
    int sda_gpio, scl_gpio;
    uint8_t address;       // 0x76 (SDO to GND) or 0x77
    uint32_t clk_hz;       // e.g. 100000
    bool internal_pullup;  // the chip's pull-ups, in addition to the board's
    bme280_settings_t settings;
} bme280_config_t;

// The Bosch "weather monitoring" settings: ×1 oversampling for all three, filter off.
#define BME280_SETTINGS_WEATHER {.osrs_t = 1, .osrs_p = 1, .osrs_h = 1, .filter = 0}

// Bus and device setup, chip ID check, calibration load, settings written.
// ESP_ERR_NOT_FOUND if the chip answers with another ID than 0x60 (e.g. a
// BMP280); the bus driver's error if it does not answer. On any failure the
// bus is released again.
esp_err_t bme280_start(const bme280_config_t *cfg);

// One forced measurement, compensated. ESP_ERR_INVALID_STATE before a
// successful start; ESP_ERR_TIMEOUT if the measurement does not finish.
esp_err_t bme280_read(bme280_reading_t *out);

// Release the device and bus. The sensor stays in sleep mode.
esp_err_t bme280_stop(void);
