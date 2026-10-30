// Deep sleep for a period: arms the timer (and the wake GPIO, Kconfig
// DEEP_SLEEP_WAKE_GPIO), runs the app's prepare callbacks, then sleeps. Waking
// is a full reboot; deep_sleep_wake_cause tells the app why it booted. Period
// limits and the wake GPIO are Kconfig options (menu "Deep sleep").
//
// The component powers nothing down itself: the app registers what to stop
// (display, WiFi) as prepare callbacks.
#pragma once

#include <stdint.h>

#include "deep_sleep_core.h"
#include "esp_err.h"

#define DEEP_SLEEP_PREPARE_MAX 4

// Run just before sleep, in registration order. A failing callback is logged
// and the rest still run.
typedef esp_err_t (*deep_sleep_prepare_cb_t)(void *arg);

// ESP_ERR_NO_MEM once DEEP_SLEEP_PREPARE_MAX callbacks are registered.
esp_err_t deep_sleep_on_prepare(deep_sleep_prepare_cb_t cb, void *arg);

// Sleep for seconds. Does not return on success. ESP_ERR_INVALID_ARG for a
// period outside DEEP_SLEEP_MIN_S..DEEP_SLEEP_MAX_S, or the error that arming
// a wake source returned; either way nothing was powered down.
esp_err_t deep_sleep_for(uint32_t seconds);

// Why this boot happened, and the period the last sleep asked for (0 after a
// cold boot).
deep_sleep_wake_t deep_sleep_wake_cause(void);
uint32_t deep_sleep_last_period_s(void);
