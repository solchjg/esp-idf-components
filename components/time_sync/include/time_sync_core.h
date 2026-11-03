// Time sync, pure core: whether the clock counts as synced. No ESP-IDF headers,
// so the host tier compiles it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 2024-01-01T00:00:00Z: an earlier time from another source is not a time.
#define TS_EPOCH_MIN 1704067200LL

// At boot: a sync made before a deep sleep still holds after the wake (the
// clock kept running); any other boot starts unsynced.
bool ts_synced_at_boot(bool synced_before, bool deep_sleep_wake);

// A time from another source (e.g. a modem's network time), in seconds since
// 1970 UTC: one at or after TS_EPOCH_MIN and before 2106 counts.
bool ts_epoch_valid(int64_t s);
