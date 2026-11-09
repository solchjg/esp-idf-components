// Wall-clock time: the time zone (Kconfig TIME_SYNC_TZ) and SNTP from
// TIME_SYNC_SERVER while a network is up. The clock keeps running through deep
// sleep, so a sync made before a sleep still counts after the wake; a power-on
// or reset starts unsynced. Menu "Time sync".
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Called from the SNTP (lwIP) task after each sync, with the clock already set.
typedef void (*time_sync_cb_t)(void);

// Every boot, before any use of local time: applies the time zone and decides
// whether an earlier sync still holds.
void time_sync_init(void);

// Network up: start SNTP (polls the server, then every CONFIG_LWIP_SNTP_UPDATE_DELAY).
// on_sync may be NULL. ESP_ERR_INVALID_STATE if already started.
esp_err_t time_sync_start(time_sync_cb_t on_sync);

// Network down: stop SNTP. The clock keeps running and stays synced.
void time_sync_stop(void);

// Sets the clock from another source (seconds since 1970 UTC, e.g. a modem's
// network time) and marks it synced as an SNTP sync does, kept across deep
// sleep; on_sync is not called. ESP_ERR_INVALID_ARG for a time that fails
// ts_epoch_valid, the clock then unchanged.
esp_err_t time_sync_set(int64_t epoch_s);

// The same in milliseconds since 1970 UTC (e.g. a hardware clock that counts
// 1/100 s, or a phone's clock); the validity check is on the whole second.
esp_err_t time_sync_set_ms(int64_t epoch_ms);

// True once the clock was synced since the last power-on or reset.
bool time_sync_synced(void);
