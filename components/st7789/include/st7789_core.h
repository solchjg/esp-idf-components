// ST7789 panel, pure core: the colour helpers and the rectangle and band rules
// st7789.c applies before it writes. No ESP-IDF headers, so the host tier
// compiles it as is.
#pragma once

#include <stddef.h>
#include <stdint.h>

// RGB565 from 8-bit red, green and blue (the low bits dropped).
static inline uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Both buses (i80 and SPI) send bytes in memory order; the ST7789 wants
// RGB565 MSB first.
static inline uint16_t st7789_bus_colour(uint16_t rgb565)
{
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}

// 1 when the w x h rectangle at (x, y) lies inside a width x height panel
// and is not empty, else 0.
int st7789_rect_ok(int x, int y, int w, int h, int width, int height);

// The rows of a w-wide fill that one canvas of `pixels` holds, at most h; 0
// when not even one row fits or w or h is not positive.
int st7789_band(size_t pixels, int w, int h);
