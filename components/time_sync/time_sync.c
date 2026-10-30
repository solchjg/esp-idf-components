#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "time_sync_core.h"

static RTC_DATA_ATTR bool synced_rtc;  // kept in RTC memory across deep sleep
static bool synced, started;
static time_sync_cb_t sync_cb;

static void on_sntp(struct timeval *tv)
{
    synced = true;
    synced_rtc = true;
    if (sync_cb) {
        sync_cb();
    }
}

void time_sync_init(void)
{
    setenv("TZ", CONFIG_TIME_SYNC_TZ, 1);
    tzset();
    synced = ts_synced_at_boot(synced_rtc, esp_reset_reason() == ESP_RST_DEEPSLEEP);
    synced_rtc = synced;
}

esp_err_t time_sync_start(time_sync_cb_t on_sync)
{
    if (started) {
        return ESP_ERR_INVALID_STATE;
    }
    sync_cb = on_sync;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_TIME_SYNC_SERVER);
    cfg.sync_cb = on_sntp;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    started = err == ESP_OK;
    return err;
}

void time_sync_stop(void)
{
    if (started) {
        esp_netif_sntp_deinit();
        started = false;
    }
}

bool time_sync_synced(void)
{
    return synced;
}
