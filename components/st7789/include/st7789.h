// ST7789 panel on an 8-bit Intel-8080 (i80) bus or a 4-wire SPI bus: bring-up,
// a DMA canvas and rectangle writes. Board-agnostic: every pin and panel
// setting comes from st7789_config_t. On SPI the bus may already be up for
// another device on the same lines with its own CS. One panel per firmware
// (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts (FSD §6.5): what
// happens on failure is the caller's decision.
//
// Settings of two known panels:
//   LilyGO T-Display-S3 1.9" 170 x 320, i80: tdisplay_s3.h.
//   Waveshare 1.9" LCD module 170 x 320, SPI: invert true; portrait 170 x 320
//   with gap_x 35, gap_y 0; landscape 320 x 170 with swap_xy and mirror_x,
//   gap_x 0, gap_y 35. The glass sits centred in the controller's 240 columns.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "st7789_core.h"

typedef enum {
    ST7789_BUS_I80 = 0,  // the default, so an i80 config need not name it
    ST7789_BUS_SPI,
} st7789_bus_t;

typedef struct {
    st7789_bus_t bus;
    // i80 only
    int data_gpio[8];    // D0 – D7
    int wr_gpio;
    int rd_gpio;         // driven high and never read; -1 if not wired
    // SPI only (mode 0, write only)
    int spi_host;        // e.g. SPI2_HOST
    int sck_gpio, mosi_gpio;
    // both
    int dc_gpio;
    int cs_gpio;
    int rst_gpio;        // -1 if not wired: a software reset instead
    int power_gpio;      // driven high before init; -1 if none
    int backlight_gpio;  // driven low at start, see st7789_backlight; -1 if none
    uint32_t pclk_hz;    // SPI: at most 62.5 MHz by the datasheet, less on long wires
    int width, height;   // visible area after swap_xy
    int gap_x, gap_y;    // visible-area offset in controller RAM
    bool invert;
    bool swap_xy, mirror_x, mirror_y;
    size_t canvas_pixels;  // DMA canvas size; one write sends at most this many
} st7789_config_t;

// Power, bus and panel bring-up, then clear to black. The backlight stays off.
esp_err_t st7789_start(const st7789_config_t *cfg);

// The DMA-capable pixel buffer that st7789_write sends, and its size in
// pixels. NULL before a successful start's allocation. Fill it in bus order
// (st7789_bus_colour); it may be refilled as soon as st7789_write returns.
uint16_t *st7789_canvas(size_t *pixels);

// Send the first w*h canvas pixels to the w x h rectangle at (x, y) and wait
// until DMA has finished reading them. ESP_ERR_INVALID_SIZE for a rectangle
// outside the visible area or larger than the canvas; ESP_ERR_TIMEOUT if DMA
// does not finish.
esp_err_t st7789_write(int x, int y, int w, int h);

// Fill a rectangle of any size with one colour (RGB565, host order).
esp_err_t st7789_fill(int x, int y, int w, int h, uint16_t rgb565);

// Display on or off (the controller keeps its RAM while off).
esp_err_t st7789_display(bool on);

void st7789_backlight(bool on);
