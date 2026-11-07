#include "st7789.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_try.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// A full 320 x 240 canvas at 10 MHz SPI takes about 125 ms.
#define DONE_TIMEOUT_MS 500

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static uint16_t *canvas;
static size_t canvas_pixels;
static int backlight_gpio = -1;
static int width, height;

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev,
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
}

uint16_t *st7789_canvas(size_t *pixels)
{
    if (pixels) {
        *pixels = canvas_pixels;
    }
    return canvas;
}

esp_err_t st7789_write(int x, int y, int w, int h)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!st7789_rect_ok(x, y, w, h, width, height) || (size_t)w * (size_t)h > canvas_pixels) {
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_TRY(esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, canvas));
    return xSemaphoreTake(done, pdMS_TO_TICKS(DONE_TIMEOUT_MS)) == pdTRUE ? ESP_OK
                                                                         : ESP_ERR_TIMEOUT;
}

esp_err_t st7789_fill(int x, int y, int w, int h, uint16_t rgb565)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int band = st7789_band(canvas_pixels, w, h);
    if (band <= 0 || !st7789_rect_ok(x, y, w, h, width, height)) {
        return ESP_ERR_INVALID_SIZE;
    }
    // Full-width bands of the canvas; DMA is done with it after every write,
    // and the colour does not change, so it is filled once.
    size_t n = (size_t)w * (size_t)band;
    uint16_t c = st7789_bus_colour(rgb565);
    for (size_t i = 0; i < n; i++) {
        canvas[i] = c;
    }
    for (int row = 0; row < h; row += band) {
        int rows = h - row < band ? h - row : band;
        ESP_TRY(st7789_write(x, y + row, w, rows));
    }
    return ESP_OK;
}

esp_err_t st7789_display(bool on)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_disp_on_off(panel, on);
}

void st7789_backlight(bool on)
{
    if (backlight_gpio >= 0) {
        gpio_set_level(backlight_gpio, on);
    }
}

static esp_err_t new_i80_io(const st7789_config_t *cfg, esp_lcd_panel_io_handle_t *io)
{
    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = cfg->dc_gpio,
        .wr_gpio_num = cfg->wr_gpio,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .bus_width = 8,
        .max_transfer_bytes = cfg->canvas_pixels * sizeof(uint16_t),
    };
    for (int i = 0; i < 8; i++) {
        bus_cfg.data_gpio_nums[i] = cfg->data_gpio[i];
    }
    ESP_TRY(esp_lcd_new_i80_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = cfg->cs_gpio,
        .pclk_hz = cfg->pclk_hz,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_trans_done,
        .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0,
                      .dc_data_level = 1},
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    return esp_lcd_new_panel_io_i80(bus, &io_cfg, io);
}

static esp_err_t new_spi_io(const st7789_config_t *cfg, esp_lcd_panel_io_handle_t *io)
{
    spi_bus_config_t bus = {
        .sclk_io_num = cfg->sck_gpio,
        .mosi_io_num = cfg->mosi_gpio,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(cfg->canvas_pixels * sizeof(uint16_t)),
    };
    // A bus already up for another device keeps its own max_transfer_sz;
    // esp_lcd splits a longer colour write into transactions that fit it.
    esp_err_t err = spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = cfg->cs_gpio,
        .dc_gpio_num = cfg->dc_gpio,
        .spi_mode = 0,
        .pclk_hz = cfg->pclk_hz,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->spi_host, &io_cfg, io);
}

esp_err_t st7789_start(const st7789_config_t *cfg)
{
    if (cfg == NULL || cfg->width <= 0 || cfg->height <= 0 || cfg->canvas_pixels == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (panel != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    out_pin(cfg->power_gpio, 1);
    if (cfg->bus == ST7789_BUS_I80) {
        out_pin(cfg->rd_gpio, 1);
    }
    out_pin(cfg->backlight_gpio, 0);
    backlight_gpio = cfg->backlight_gpio;
    width = cfg->width;
    height = cfg->height;

    done = xSemaphoreCreateBinary();
    canvas = heap_caps_malloc(cfg->canvas_pixels * sizeof(uint16_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (done == NULL || canvas == NULL) {
        return ESP_ERR_NO_MEM;
    }
    canvas_pixels = cfg->canvas_pixels;

    esp_lcd_panel_io_handle_t io;
    if (cfg->bus == ST7789_BUS_SPI) {
        ESP_TRY(new_spi_io(cfg, &io));
    } else {
        ESP_TRY(new_i80_io(cfg, &io));
    }

    // reset_gpio_num -1: esp_lcd sends the software reset (SWRESET) instead.
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = cfg->rst_gpio,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t p;
    ESP_TRY(esp_lcd_new_panel_st7789(io, &panel_cfg, &p));
    ESP_TRY(esp_lcd_panel_reset(p));
    ESP_TRY(esp_lcd_panel_init(p));
    ESP_TRY(esp_lcd_panel_invert_color(p, cfg->invert));
    ESP_TRY(esp_lcd_panel_swap_xy(p, cfg->swap_xy));
    ESP_TRY(esp_lcd_panel_mirror(p, cfg->mirror_x, cfg->mirror_y));
    ESP_TRY(esp_lcd_panel_set_gap(p, cfg->gap_x, cfg->gap_y));
    ESP_TRY(esp_lcd_panel_disp_on_off(p, true));
    panel = p;

    return st7789_fill(0, 0, cfg->width, cfg->height, 0x0000);
}
