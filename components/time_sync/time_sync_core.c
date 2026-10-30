#include "time_sync_core.h"

bool ts_synced_at_boot(bool synced_before, bool deep_sleep_wake)
{
    return deep_sleep_wake && synced_before;
}
