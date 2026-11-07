// MAX17048 single-cell fuel gauge on I2C (0x36): cell voltage and state of
// charge. Board-agnostic: pins and the bus come from max17048_config_t. It
// joins the master bus already open on the port (e.g. a sensor's) or, if none
// is, opens its own and releases it again at max17048_stop. One gauge per
// firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts: what happens on
// failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "max17048_core.h"

typedef struct {
    int i2c_port;            // I2C controller, e.g. 0
    int sda_gpio, scl_gpio;  // used only when no bus is open on the port
    uint32_t clk_hz;         // up to 400000
    bool internal_pullup;    // only for a bus the driver opens itself
} max17048_config_t;

// Device setup and a VERSION read, so an absent or other chip fails here:
// the bus driver's error, or ESP_ERR_NOT_FOUND for a VERSION that is not a
// MAX17048's. On any failure what was set up is released again.
esp_err_t max17048_start(const max17048_config_t *cfg);

// The cell voltage and state of charge. ESP_ERR_INVALID_STATE before a
// successful start.
esp_err_t max17048_read(max17048_reading_t *out);

// Release the device, and the bus if the driver opened it.
esp_err_t max17048_stop(void);
