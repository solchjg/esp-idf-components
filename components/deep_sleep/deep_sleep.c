#include "deep_sleep.h"

#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define WAKE_GPIO CONFIG_DEEP_SLEEP_WAKE_GPIO
#define LOG_FLUSH_MS 50  // lets the console send the last lines

static const char *TAG = "deep_sleep";

static RTC_DATA_ATTR uint32_t last_period_s;  // kept in RTC memory across the sleep

static struct {
    deep_sleep_prepare_cb_t cb;
    void *arg;
} prepare[DEEP_SLEEP_PREPARE_MAX];
static int prepare_n;

esp_err_t deep_sleep_on_prepare(deep_sleep_prepare_cb_t cb, void *arg)
{
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (prepare_n == DEEP_SLEEP_PREPARE_MAX) {
        return ESP_ERR_NO_MEM;
    }
    prepare[prepare_n].cb = cb;
    prepare[prepare_n].arg = arg;
    prepare_n++;
    return ESP_OK;
}

static esp_err_t arm_button(void)
{
#if WAKE_GPIO >= 0
    // Active low through EXT1. The pull-up is the chip's own unless the board
    // has one; with the RTC peripherals powered down, ESP-IDF holds it on the pad.
    ESP_TRY(rtc_gpio_pulldown_dis(WAKE_GPIO));
#if CONFIG_DEEP_SLEEP_WAKE_PULLUP
    ESP_TRY(rtc_gpio_pullup_en(WAKE_GPIO));
#else
    ESP_TRY(rtc_gpio_pullup_dis(WAKE_GPIO));
#endif
    return esp_sleep_enable_ext1_wakeup_io(1ULL << WAKE_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    return ESP_OK;
#endif
}

esp_err_t deep_sleep_for(uint32_t seconds)
{
    if (!ds_period_valid(seconds, CONFIG_DEEP_SLEEP_MIN_S, CONFIG_DEEP_SLEEP_MAX_S)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_TRY(esp_sleep_enable_timer_wakeup(ds_period_us(seconds)));
    ESP_TRY(arm_button());

    for (int i = 0; i < prepare_n; i++) {
        esp_err_t err = prepare[i].cb(prepare[i].arg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "prepare %d: %s", i, esp_err_to_name(err));
        }
    }
    last_period_s = seconds;
    ESP_LOGI(TAG, "sleeping for %lu s", (unsigned long)seconds);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(LOG_FLUSH_MS));
    gpio_deep_sleep_hold_en();  // pads a prepare callback held keep their level
    esp_deep_sleep_start();
}

deep_sleep_wake_t deep_sleep_wake_cause(void)
{
    uint32_t causes = esp_sleep_get_wakeup_causes();
    bool timer = causes & (1u << ESP_SLEEP_WAKEUP_TIMER);
    uint64_t ext1 =
        (causes & (1u << ESP_SLEEP_WAKEUP_EXT1)) ? esp_sleep_get_ext1_wakeup_status() : 0;
    return ds_wake(timer, ext1, WAKE_GPIO);
}

uint32_t deep_sleep_last_period_s(void)
{
    return deep_sleep_wake_cause() == DEEP_SLEEP_WAKE_NONE ? 0 : last_period_s;
}
