// SSD1683 e-paper panel (400 x 300 black and white, e.g. Waveshare's 4.2"
// e-Paper Module V2) on a 4-wire SPI bus: bring-up, a DMA canvas of the whole
// image, full and partial refreshes that run while the caller goes on, and the
// controller's deep sleep. Board-agnostic: every pin comes from
// ssd1683_config_t. The bus may already be up for another device (an OLED on
// the same lines with its own CS). One panel per firmware (the driver keeps a
// single instance).
//
// Every call reports its esp_err_t; none retries or aborts: what happens on
// failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ssd1683_core.h"

typedef struct {
    int spi_host;        // e.g. SPI2_HOST
    int sck_gpio, mosi_gpio, cs_gpio, dc_gpio;
    int rst_gpio;        // low resets; also ends the deep sleep
    int busy_gpio;       // high while the controller is busy
    uint32_t pclk_hz;    // at most 20 MHz
    int width, height;   // the panel, e.g. 400 x 300
} ssd1683_config_t;

// Hardware reset (which ends a deep sleep), software reset and the init
// sequence; the canvas is cleared to white. Nothing is shown until
// ssd1683_show. ESP_ERR_TIMEOUT when BUSY does not fall within 1 s. A second
// start after ssd1683_sleep wakes the same panel again.
esp_err_t ssd1683_start(const ssd1683_config_t *cfg);

// The DMA-capable image ssd1683_show sends, width/8 bytes a row, MSB the
// leftmost pixel, 1 = white; and its size. NULL before a successful start.
uint8_t *ssd1683_canvas(size_t *bytes);

// Starts a refresh of the canvas and returns while it runs (ssd1683_busy).
// full: the whole waveform (both RAMs get the canvas); else a partial refresh
// against the image shown last. ESP_ERR_INVALID_STATE while a refresh runs.
esp_err_t ssd1683_show(bool full);

// True while a refresh runs (BUSY high).
bool ssd1683_busy(void);

// Waits until a running refresh ends; ESP_ERR_TIMEOUT after timeout_ms.
esp_err_t ssd1683_wait(uint32_t timeout_ms);

// Waits for a running refresh (up to 6 s), then deep sleep mode 1: the panel
// keeps its image with no power and the controller draws almost none.
// Holds RST high through the chip's own deep sleep.
esp_err_t ssd1683_sleep(void);
