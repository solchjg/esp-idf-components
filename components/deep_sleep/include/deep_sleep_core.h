// Deep sleep, pure core: period checks and the wake-cause mapping. No ESP-IDF
// headers, so the host tier compiles it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DEEP_SLEEP_WAKE_NONE,    // cold boot, reset, or any other wake than these two
    DEEP_SLEEP_WAKE_TIMER,   // the sleep period ran out
    DEEP_SLEEP_WAKE_BUTTON,  // the wake GPIO went low
} deep_sleep_wake_t;

// True when seconds lies in [min_s, max_s].
bool ds_period_valid(uint32_t seconds, uint32_t min_s, uint32_t max_s);

// The period in microseconds, as the sleep timer takes it.
uint64_t ds_period_us(uint32_t seconds);

// Why the chip woke. timer: the timer is among the wake causes. ext1_pins: the
// EXT1 pins that caused the wake (0 if EXT1 did not). wake_gpio: the configured
// wake GPIO, -1 if none. A button press wins over a timer that ran out with it.
deep_sleep_wake_t ds_wake(bool timer, uint64_t ext1_pins, int wake_gpio);
