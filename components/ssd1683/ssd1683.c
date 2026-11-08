#include "ssd1683.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DONE_TIMEOUT_MS 500   // one RAM write of 15000 bytes takes 30 ms at 4 MHz
#define RESET_TIMEOUT_MS 1000
#define RESET_LOW_US 2000
#define PRESENT_WAIT_US 2000  // BUSY rises within about 15 us of the software reset
#define SLEEP_WAIT_MS 6000    // a full refresh takes about 4 s (the task watchdog is 10 s)
#define SPI_CHUNK 4096

static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done;
static uint8_t *canvas, *last;  // drawn, and shown last (the partial refresh's base)
static size_t canvas_bytes;
static int rst_gpio = -1, busy_gpio = -1;
static int width, height;

static bool on_trans_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *ev,
                          void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done, &woken);
    return woken == pdTRUE;
}

static esp_err_t step(const ssd1683_step_t *s)
{
    return esp_lcd_panel_io_tx_param(io, s->cmd, s->n ? s->p : NULL, s->n);
}

static esp_err_t cmd(uint8_t c)
{
    return esp_lcd_panel_io_tx_param(io, c, NULL, 0);
}

uint8_t *ssd1683_canvas(size_t *bytes)
{
    if (bytes) {
        *bytes = canvas_bytes;
    }
    return io ? canvas : NULL;
}

bool ssd1683_busy(void)
{
    return busy_gpio >= 0 && gpio_get_level(busy_gpio) == 1;
}

esp_err_t ssd1683_wait(uint32_t timeout_ms)
{
    int64_t t0 = esp_timer_get_time();
    while (ssd1683_busy()) {
        if (esp_timer_get_time() - t0 > (int64_t)timeout_ms * 1000) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);  // at least one tick, so IDLE runs
    }
    return ESP_OK;
}

// One RAM from its start; waits until DMA has read the buffer.
static esp_err_t write_ram(uint8_t ram, const uint8_t *img)
{
    const ssd1683_step_t x = {SSD1683_SET_X, 1, {0x00}};
    const ssd1683_step_t y = {SSD1683_SET_Y, 2, {0x00, 0x00}};
    ESP_TRY(step(&x));
    ESP_TRY(step(&y));
    ESP_TRY(esp_lcd_panel_io_tx_color(io, ram, img, canvas_bytes));
    return xSemaphoreTake(done, pdMS_TO_TICKS(DONE_TIMEOUT_MS)) == pdTRUE ? ESP_OK
                                                                         : ESP_ERR_TIMEOUT;
}

esp_err_t ssd1683_show(bool full)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssd1683_busy()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (full) {
        ESP_TRY(write_ram(SSD1683_WRITE_BW, canvas));
        ESP_TRY(write_ram(SSD1683_WRITE_RED, canvas));
    } else {
        ESP_TRY(write_ram(SSD1683_WRITE_RED, last));
        ESP_TRY(write_ram(SSD1683_WRITE_BW, canvas));
    }
    ssd1683_step_t r[2];
    ssd1683_refresh_steps(full, r);
    ESP_TRY(step(&r[0]));
    ESP_TRY(step(&r[1]));
    ESP_TRY(cmd(SSD1683_ACTIVATE));
    memcpy(last, canvas, canvas_bytes);
    return ESP_OK;
}

esp_err_t ssd1683_sleep(void)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ssd1683_wait(SLEEP_WAIT_MS);
    // The analog block and the clock off first (a partial refresh leaves them
    // on), then deep sleep mode 1.
    const ssd1683_step_t off = {SSD1683_UPDATE_CTRL2, 1, {SSD1683_POWER_OFF}};
    esp_err_t e = step(&off);
    if (e == ESP_OK) {
        e = cmd(SSD1683_ACTIVATE);
    }
    if (e == ESP_OK) {
        e = ssd1683_wait(RESET_TIMEOUT_MS);
    }
    const ssd1683_step_t s = {SSD1683_DEEP_SLEEP, 1, {0x01}};
    esp_err_t e2 = step(&s);
    e = e != ESP_OK ? e : e2;
    gpio_set_level(rst_gpio, 1);
    gpio_hold_en(rst_gpio);  // a low RST would wake the controller
    return err != ESP_OK ? err : e;
}

static esp_err_t reset(void)
{
    gpio_hold_dis(rst_gpio);
    gpio_set_level(rst_gpio, 0);
    // An exact 2 ms low: on the Waveshare 4.2" V2 module a low of 10 ms or
    // more leaves every refresh hanging (BUSY high), while 0.2 to 5 ms works.
    esp_rom_delay_us(RESET_LOW_US);
    gpio_set_level(rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(10) + 1);  // at least 10 ms at any tick rate
    ESP_TRY(cmd(SSD1683_SW_RESET));
    // A fitted controller holds BUSY high for about 4.4 ms after the software
    // reset; with no panel, the pull-down keeps BUSY low.
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(busy_gpio) == 0) {
        if (esp_timer_get_time() - t0 > PRESENT_WAIT_US) {
            return ESP_ERR_NOT_FOUND;
        }
    }
    return ssd1683_wait(RESET_TIMEOUT_MS);
}

esp_err_t ssd1683_start(const ssd1683_config_t *cfg)
{
    if (cfg == NULL || cfg->rst_gpio < 0 || cfg->busy_gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ssd1683_step_t init[SSD1683_INIT_STEPS];
    if (ssd1683_init_steps(cfg->width, cfg->height, init) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (io == NULL) {
        width = cfg->width;
        height = cfg->height;
        canvas_bytes = (size_t)width / 8 * (size_t)height;
        done = xSemaphoreCreateBinary();
        canvas = heap_caps_malloc(canvas_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        last = heap_caps_malloc(canvas_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (done == NULL || canvas == NULL || last == NULL) {
            return ESP_ERR_NO_MEM;
        }
        rst_gpio = cfg->rst_gpio;
        busy_gpio = cfg->busy_gpio;
        gpio_hold_dis(rst_gpio);
        gpio_set_level(rst_gpio, 1);
        gpio_config_t out = {.pin_bit_mask = 1ULL << rst_gpio, .mode = GPIO_MODE_OUTPUT};
        gpio_config_t in = {.pin_bit_mask = 1ULL << busy_gpio, .mode = GPIO_MODE_INPUT,
                              .pull_down_en = GPIO_PULLDOWN_ENABLE};  // low with no panel
        ESP_TRY(gpio_config(&out));
        ESP_TRY(gpio_config(&in));
        spi_bus_config_t bus = {
            .sclk_io_num = cfg->sck_gpio,
            .mosi_io_num = cfg->mosi_gpio,
            .miso_io_num = -1,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = SPI_CHUNK,  // esp_lcd splits a RAM write into chunks
        };
        esp_err_t err = spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // up already for another device
            return err;
        }
        esp_lcd_panel_io_spi_config_t io_cfg = {
            .cs_gpio_num = cfg->cs_gpio,
            .dc_gpio_num = cfg->dc_gpio,
            .spi_mode = 0,
            .pclk_hz = cfg->pclk_hz,
            .trans_queue_depth = 4,
            .on_color_trans_done = on_trans_done,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        esp_lcd_panel_io_handle_t h;
        ESP_TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->spi_host, &io_cfg, &h));
        io = h;
    }
    ESP_TRY(reset());
    for (int i = 0; i < SSD1683_INIT_STEPS; i++) {
        ESP_TRY(step(&init[i]));
    }
    memset(canvas, 0xFF, canvas_bytes);
    memset(last, 0xFF, canvas_bytes);
    return ESP_OK;
}
