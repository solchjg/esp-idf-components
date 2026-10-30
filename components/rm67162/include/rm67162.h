// RM67162 AMOLED panel on a QSPI bus: bring-up, a DMA canvas and rectangle
// writes. Board-agnostic: every pin and panel setting comes from
// rm67162_config_t. One panel per firmware (the driver keeps a single instance).
// The API follows st7789's, so an app's display layer can take either.
//
// Every call reports its esp_err_t; none retries or aborts (FSD §6.5): what
// happens on failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int spi_host;        // SPI host the bus is created on, e.g. SPI2_HOST
    int data_gpio[4];    // QSPI D0 – D3
    int sck_gpio;
    int cs_gpio;
    int rst_gpio;
    int power_gpio;      // driven high before init; -1 if none
    uint32_t pclk_hz;
    int width, height;   // visible area in the orientation madctl selects
    int gap_x, gap_y;    // visible-area offset in controller RAM
    uint8_t madctl;      // scan direction (register 0x36): orientation and RGB/BGR
    size_t canvas_pixels;  // DMA canvas size; one write sends at most this many
} rm67162_config_t;

// Power, bus and panel bring-up, then clear to black and display on. The panel
// stays dark (brightness 0) until rm67162_brightness.
esp_err_t rm67162_start(const rm67162_config_t *cfg);

// The DMA-capable pixel buffer that rm67162_write sends, and its size in
// pixels. NULL before a successful start's allocation. Fill it in bus order
// (rm67162_bus_colour); it may be refilled as soon as rm67162_write returns.
uint16_t *rm67162_canvas(size_t *pixels);

// Send the first w*h canvas pixels to the w x h rectangle at (x, y) and wait
// until DMA has finished reading them. ESP_ERR_TIMEOUT if DMA does not finish.
esp_err_t rm67162_write(int x, int y, int w, int h);

// Fill a rectangle of any size with one colour (RGB565, host order).
esp_err_t rm67162_fill(int x, int y, int w, int h, uint16_t rgb565);

// Panel brightness (register 0x51), 0 = dark. An AMOLED has no backlight: this
// is how the panel is lit.
esp_err_t rm67162_brightness(uint8_t level);

// The QSPI bus sends bytes in memory order; the RM67162 wants RGB565 MSB first.
static inline uint16_t rm67162_bus_colour(uint16_t rgb565)
{
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}
