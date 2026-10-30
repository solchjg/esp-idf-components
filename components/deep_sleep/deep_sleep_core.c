#include "deep_sleep_core.h"

bool ds_period_valid(uint32_t seconds, uint32_t min_s, uint32_t max_s)
{
    return seconds >= min_s && seconds <= max_s;
}

uint64_t ds_period_us(uint32_t seconds)
{
    return (uint64_t)seconds * 1000000u;
}

deep_sleep_wake_t ds_wake(bool timer, uint64_t ext1_pins, int wake_gpio)
{
    if (wake_gpio >= 0 && wake_gpio < 64 && (ext1_pins >> wake_gpio) & 1u) {
        return DEEP_SLEEP_WAKE_BUTTON;
    }
    return timer ? DEEP_SLEEP_WAKE_TIMER : DEEP_SLEEP_WAKE_NONE;
}
