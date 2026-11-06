// AXP2101 power management IC on I2C: switches the DC3 and BLDO1/2 rails and
// reads the battery voltage and charger state. Board-agnostic: the bus comes
// from axp2101_config_t. It joins the master bus already open on the port or
// opens its own and releases it at axp2101_stop, so a board whose PMU shares
// an I2C controller with another bus can take the controller in turn. One PMU
// per firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts.
#pragma once

#include <stdbool.h>

#include "axp2101_core.h"
#include "esp_err.h"

typedef struct {
    int i2c_port;            // I2C controller
    int sda_gpio, scl_gpio;  // used only when no bus is open on the port
    uint32_t clk_hz;         // up to 400000
    bool internal_pullup;    // only for a bus the driver opens itself
} axp2101_config_t;

// Device setup, the chip ID check (ESP_ERR_NOT_FOUND for another chip), the
// battery ADC and battery detection on. On failure what was set up is released.
esp_err_t axp2101_start(const axp2101_config_t *cfg);

// Sets rail to mv and switches it on, or switches it off (mv ignored).
// ESP_ERR_INVALID_ARG for a voltage the rail cannot make.
esp_err_t axp2101_rail(axp2101_rail_t rail, unsigned mv, bool on);

esp_err_t axp2101_vbat_mv(unsigned *mv);
esp_err_t axp2101_status(axp2101_status_t *status);

// Releases the device, and the bus if the driver opened it. The rails stay as set.
esp_err_t axp2101_stop(void);
