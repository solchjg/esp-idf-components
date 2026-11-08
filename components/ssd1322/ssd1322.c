#include "ssd1322.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_rom_sys.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DONE_TIMEOUT_MS 100

static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done;
static uint8_t *canvas;
static size_t canvas_bytes;
static int width, height, col_offset;

static bool on_trans_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *ev,
                          void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done, &woken);
    return woken == pdTRUE;
}

static esp_err_t cmd(uint8_t c, const uint8_t *p, size_t n)
{
    return esp_lcd_panel_io_tx_param(io, c, p, n);
}

uint8_t *ssd1322_canvas(size_t *bytes)
{
    if (bytes) {
        *bytes = canvas_bytes;
    }
    return canvas;
}

esp_err_t ssd1322_write(int y, int h)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t row = (size_t)width / 2;
    if (y < 0 || h <= 0 || y + h > height || row * (size_t)h > canvas_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t cols[2];
    if (ssd1322_columns(col_offset, width, cols) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t rows[2] = {(uint8_t)y, (uint8_t)(y + h - 1)};
    ESP_TRY(cmd(SSD1322_SET_COLUMN, cols, 2));
    ESP_TRY(cmd(SSD1322_SET_ROW, rows, 2));
    ESP_TRY(esp_lcd_panel_io_tx_color(io, SSD1322_WRITE_RAM, canvas, row * (size_t)h));
    return xSemaphoreTake(done, pdMS_TO_TICKS(DONE_TIMEOUT_MS)) == pdTRUE ? ESP_OK
                                                                         : ESP_ERR_TIMEOUT;
}

esp_err_t ssd1322_display(bool on)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return cmd(on ? SSD1322_DISPLAY_ON : SSD1322_DISPLAY_OFF, NULL, 0);
}

esp_err_t ssd1322_start(const ssd1322_config_t *cfg)
{
    if (cfg == NULL || cfg->width % 4 != 0 || cfg->height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (io != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    width = cfg->width;
    height = cfg->height;
    col_offset = cfg->col_offset;
    done = xSemaphoreCreateBinary();
    canvas_bytes = (size_t)width / 2 * (size_t)height;
    canvas = heap_caps_malloc(canvas_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (done == NULL || canvas == NULL) {
        return ESP_ERR_NO_MEM;
    }
    spi_bus_config_t bus = {
        .sclk_io_num = cfg->sck_gpio,
        .mosi_io_num = cfg->mosi_gpio,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)canvas_bytes,
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

    if (cfg->rst_gpio >= 0) {
        gpio_config_t c = {.pin_bit_mask = 1ULL << cfg->rst_gpio, .mode = GPIO_MODE_OUTPUT};
        ESP_TRY(gpio_config(&c));
        // An exact 1 ms low, then at least 2 ms before the first command: at a
        // 100 Hz tick, pdMS_TO_TICKS(1) is 0 and would give no pulse at all.
        gpio_set_level(cfg->rst_gpio, 0);
        esp_rom_delay_us(1000);
        gpio_set_level(cfg->rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(2) + 1);
    }
    for (int i = 0; i < SSD1322_INIT_STEPS; i++) {
        const ssd1322_step_t *s = &ssd1322_init_steps[i];
        ESP_TRY(cmd(s->cmd, s->n ? s->p : NULL, s->n));
    }
    memset(canvas, 0, canvas_bytes);
    return ssd1322_write(0, height);
}
