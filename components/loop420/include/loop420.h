// 4-20 mA current loop, pure core: the current from a shunt voltage, the
// NAMUR NE43 fault limits, and the linear scale from 4 – 20 mA to a value.
// No ESP-IDF headers; it decides nothing about when to read or what to log.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LOOP420_FAULT_LOW_MA 3.6f    // NAMUR NE43: below is a fault
#define LOOP420_FAULT_HIGH_MA 21.0f  // above is a fault

typedef enum {
    LOOP420_OK,
    LOOP420_LOW,   // below LOOP420_FAULT_LOW_MA: open loop, failed transmitter
    LOOP420_HIGH,  // above LOOP420_FAULT_HIGH_MA: short, failed transmitter
} loop420_state_t;

typedef struct {
    float lo;  // the value at 4 mA
    float hi;  // the value at 20 mA
} loop420_scale_t;

// The loop current in mA through a shunt of shunt_mohm milliohms.
float loop420_ma(int32_t shunt_uv, uint32_t shunt_mohm);

// LOOP420_OK from 3.6 to 21.0 mA inclusive, else the side it left on.
loop420_state_t loop420_check(float ma);

// lo + (ma - 4) / 16 * (hi - lo), unclamped.
float loop420_value(const loop420_scale_t *scale, float ma);

// Both in one: true and *value for a current inside the limits, false for a
// fault (*state says which, *value untouched).
bool loop420_convert(const loop420_scale_t *scale, int32_t shunt_uv, uint32_t shunt_mohm,
                     float *ma, float *value, loop420_state_t *state);
