// Blues Notecard on I2C: Blues' note-c library (vendored, note-c/) over the
// ESP-IDF I2C master driver. Board-agnostic: every pin and setting comes from
// notecard_config_t. It joins the master bus already open on the port (e.g. a
// sensor's) or, if none is, opens its own and releases it at notecard_stop.
// One Notecard per firmware (the driver keeps a single instance); calls are
// meant for one task at a time.
//
// The requests themselves are JSON lines (notecard_core.h builds them); this
// driver only carries them. Every call reports its esp_err_t; none retries or
// aborts: what happens on failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "notecard_core.h"

#define NOTECARD_I2C_ADDR 0x17

typedef struct {
    int i2c_port;            // I2C controller, e.g. 0
    int sda_gpio, scl_gpio;  // used only when no bus is open on the port
    uint8_t address;         // NOTECARD_I2C_ADDR
    uint32_t clk_hz;         // e.g. 100000
    bool internal_pullup;    // only for a bus the driver opens itself
    uint32_t timeout_s;      // longest wait for an answer; 0 for note-c's default (30 s)
} notecard_config_t;

// Bus and device setup and note-c's hooks. No request is sent, so an absent
// Notecard fails only at the first notecard_request. On failure what was set
// up is released again.
esp_err_t notecard_start(const notecard_config_t *cfg);

// Sends req (one JSON line, '\n' at the end; see notecard_core.h) and copies
// the answer, without its '\n', into rsp. The answer may itself carry "err"
// (nc_err). ESP_ERR_INVALID_STATE before a start; ESP_ERR_TIMEOUT when no
// answer comes; ESP_ERR_INVALID_SIZE when rsp is too small (rsp then holds
// the start of the answer).
esp_err_t notecard_request(const char *req, char *rsp, size_t rsp_size);

// Release the device, and the bus if the driver opened it. The Notecard
// itself stays powered and keeps its state.
esp_err_t notecard_stop(void);
