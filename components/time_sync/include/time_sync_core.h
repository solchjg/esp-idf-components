// Time sync, pure core: whether the clock counts as synced. No ESP-IDF headers,
// so the host tier compiles it.
#pragma once

#include <stdbool.h>

// At boot: a sync made before a deep sleep still holds after the wake (the
// clock kept running); any other boot starts unsynced.
bool ts_synced_at_boot(bool synced_before, bool deep_sleep_wake);
