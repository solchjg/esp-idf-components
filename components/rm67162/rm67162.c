#include "rm67162.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DONE_TIMEOUT_MS 100
#define MAX_TRANSFER_BYTES 32768  // largest DMA transaction of one SPI write

// QSPI framing: a register write is opcode 0x02 + 24-bit address (0x00 reg 0x00)
// on one line, then its parameters on one line; pixels are opcode 0x32 + RAMWR,
// then the data on four lines.
#define REG(cmd) ((0x02 << 24) | ((cmd) << 8))
#define PIXELS ((0x32 << 24) | (0x2C << 8))

#define SLPIN 0x10
#define SLPOUT 0x11
#define DISPOFF 0x28
#define DISPON 0x29
#define CASET 0x2A
#define RASET 0x2B
#define MADCTL 0x36
#define COLMOD 0x3A
#define WRDISBV 0x51
#define PAGE 0xFE

static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done;
static uint16_t *canvas;
static size_t canvas_pixels;
static int gap_x, gap_y;
static int power_gpio = -1;

static bool on_trans_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *ev,
                          void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done, &woken);
    return woken == pdTRUE;
}

static void out_pin(int pin, int level)
{
    if (pin < 0) {
        return;
    }
    gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(pin, level);
    gpio_hold_dis(pin);  // rm67162_sleep may have held it low through a deep sleep
}

static esp_err_t reg(uint8_t cmd, const uint8_t *param, size_t len)
{
    return esp_lcd_panel_io_tx_param(io, REG(cmd), param, len);
}

static esp_err_t reg1(uint8_t cmd, uint8_t value)
{
    return reg(cmd, &value, 1);
}

static esp_err_t window(int x0, int y0, int x1, int y1)
{
    const uint8_t col[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    const uint8_t row[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    ESP_TRY(reg(CASET, col, sizeof col));
    return reg(RASET, row, sizeof row);
}

uint16_t *rm67162_canvas(size_t *pixels)
{
    if (pixels) {
        *pixels = canvas_pixels;
    }
    return canvas;
}

esp_err_t rm67162_write(int x, int y, int w, int h)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (w <= 0 || h <= 0 || (size_t)w * (size_t)h > canvas_pixels) {
        return ESP_ERR_INVALID_SIZE;
    }
    x += gap_x;
    y += gap_y;
    ESP_TRY(window(x, y, x + w - 1, y + h - 1));
    ESP_TRY(esp_lcd_panel_io_tx_color(io, PIXELS, canvas, (size_t)w * (size_t)h * 2));
    return xSemaphoreTake(done, pdMS_TO_TICKS(DONE_TIMEOUT_MS)) == pdTRUE ? ESP_OK
                                                                         : ESP_ERR_TIMEOUT;
}

esp_err_t rm67162_fill(int x, int y, int w, int h, uint16_t rgb565)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int band = w > 0 ? (int)(canvas_pixels / (size_t)w) : 0;
    if (band <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    // Full-width bands of the canvas; DMA is done with it after every write,
    // and the colour does not change, so it is filled once.
    size_t n = (size_t)w * (size_t)(band < h ? band : h);
    uint16_t c = rm67162_bus_colour(rgb565);
    for (size_t i = 0; i < n; i++) {
        canvas[i] = c;
    }
    for (int row = 0; row < h; row += band) {
        int rows = h - row < band ? h - row : band;
        ESP_TRY(rm67162_write(x, y + row, w, rows));
    }
    return ESP_OK;
}

esp_err_t rm67162_brightness(uint8_t level)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return reg1(WRDISBV, level);
}

esp_err_t rm67162_sleep(void)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_TRY(reg1(WRDISBV, 0x00));
    ESP_TRY(reg(DISPOFF, NULL, 0));
    ESP_TRY(reg(SLPIN, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(120));  // sleep-in sequence before power is removed
    if (power_gpio < 0) {
        return ESP_OK;
    }
    out_pin(power_gpio, 0);
    return gpio_hold_en(power_gpio);
}

esp_err_t rm67162_start(const rm67162_config_t *cfg)
{
    power_gpio = cfg->power_gpio;
    out_pin(cfg->power_gpio, 1);
    out_pin(cfg->rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gap_x = cfg->gap_x;
    gap_y = cfg->gap_y;

    done = xSemaphoreCreateBinary();
    canvas = heap_caps_malloc(cfg->canvas_pixels * sizeof(uint16_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (done == NULL || canvas == NULL) {
        return ESP_ERR_NO_MEM;
    }
    canvas_pixels = cfg->canvas_pixels;

    size_t bytes = cfg->canvas_pixels * sizeof(uint16_t);
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = cfg->sck_gpio,
        .data0_io_num = cfg->data_gpio[0],
        .data1_io_num = cfg->data_gpio[1],
        .data2_io_num = cfg->data_gpio[2],
        .data3_io_num = cfg->data_gpio[3],
        .max_transfer_sz = bytes < MAX_TRANSFER_BYTES ? bytes : MAX_TRANSFER_BYTES,
    };
    ESP_TRY(spi_bus_initialize(cfg->spi_host, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = cfg->cs_gpio,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = cfg->pclk_hz,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {.quad_mode = 1},
    };
    esp_lcd_panel_io_handle_t h;
    ESP_TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->spi_host, &io_cfg, &h));
    io = h;

    // Hardware reset, then LilyGO's RM67162 sequence for the 1.91" panel:
    // sleep out, the ELVSS/OVSS trim on manufacturer pages 5 and 1, scan
    // direction, 16 bit/pixel, dark, clear, display on.
    gpio_set_level(cfg->rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(cfg->rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_TRY(reg1(PAGE, 0x00));
    ESP_TRY(reg(SLPOUT, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_TRY(reg1(PAGE, 0x05));
    ESP_TRY(reg1(0x05, 0x05));
    ESP_TRY(reg1(PAGE, 0x01));
    ESP_TRY(reg1(0x73, 0x25));
    ESP_TRY(reg1(PAGE, 0x00));
    ESP_TRY(reg1(MADCTL, cfg->madctl));
    ESP_TRY(reg1(COLMOD, 0x55));
    ESP_TRY(reg1(WRDISBV, 0x00));
    ESP_TRY(rm67162_fill(0, 0, cfg->width, cfg->height, 0x0000));
    ESP_TRY(reg(DISPON, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}
