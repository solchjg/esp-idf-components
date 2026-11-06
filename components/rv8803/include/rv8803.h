// RV-8803-C7 temperature-compensated real-time clock on I2C (0x32), as on the
// Adafruit and SparkFun Qwiic breakouts: the time in UTC with 1/100 s, its
// lost-time flag, and its rate trim. Board-agnostic: the bus comes from
// rv8803_config_t. Each call joins the master bus already open on the port
// (another driver's) or opens one and releases it before returning, so the
// chip can sit on a bus other drivers start and stop. Calls are not
// thread-safe: make them from the task that owns the bus's other traffic.
//
// Every call reports its esp_err_t; none retries or aborts.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rv8803_core.h"

typedef struct {
    int i2c_port;            // I2C controller, e.g. 0
    int sda_gpio, scl_gpio;  // used only when no bus is open on the port
    uint32_t clk_hz;         // e.g. 100000
    bool internal_pullup;    // the chip's pull-ups, in addition to the board's (own bus only)
} rv8803_config_t;

typedef struct {
    int64_t epoch_ms;  // UTC; valid only when !lost
    bool lost;         // the time is not to be trusted: V2F set (backup ran out), or no valid date
    bool low;          // V1F: the supply dipped, compensation may have paused
} rv8803_time_t;

// ESP_ERR_NOT_FOUND when nothing answers at 0x32; the bus driver's error when
// a transfer fails.
esp_err_t rv8803_read(const rv8803_config_t *cfg, rv8803_time_t *out);

// Sets whole second epoch_s (call at the start of that second: the divider
// restarts here) and clears the flags. ESP_ERR_INVALID_ARG outside 2000 – 2099.
esp_err_t rv8803_write(const rv8803_config_t *cfg, int64_t epoch_s);

// The rate trim in steps of 0.2384 ppm (rv8803_core.h).
esp_err_t rv8803_get_trim(const rv8803_config_t *cfg, int *steps);
esp_err_t rv8803_set_trim(const rv8803_config_t *cfg, int steps);
