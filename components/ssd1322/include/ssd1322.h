// SSD1322 OLED panel (256 x 64, 16 grey levels) on a 4-wire SPI bus: bring-up,
// a DMA canvas of packed rows, row writes, display on and off. Board-agnostic:
// every pin comes from ssd1322_config_t. The bus may already be up for another
// device (an e-paper on the same lines with its own CS). One panel per
// firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries or aborts: what happens on
// failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ssd1322_core.h"

typedef struct {
    int spi_host;        // e.g. SPI2_HOST
    int sck_gpio, mosi_gpio, cs_gpio, dc_gpio;
    int rst_gpio;        // -1 when not wired
    uint32_t pclk_hz;    // at most 10 MHz
    int width, height;   // the panel, e.g. 256 x 64
    int col_offset;      // the panel's first controller column (28 for 256 wide)
} ssd1322_config_t;

// Reset, the init sequence, the RAM cleared to black. The display stays off
// until ssd1322_display(true).
esp_err_t ssd1322_start(const ssd1322_config_t *cfg);

// The DMA-capable buffer ssd1322_write sends, width/2 bytes a row
// (ssd1322_pack's layout), and its size. NULL before a successful start.
uint8_t *ssd1322_canvas(size_t *bytes);

// Sends the canvas's first h rows to panel rows y .. y+h-1 and waits until DMA
// has read them.
esp_err_t ssd1322_write(int y, int h);

// Display on or off (the controller keeps its RAM while off).
esp_err_t ssd1322_display(bool on);
