#include "ssd1322_core.h"

#include <string.h>

const ssd1322_step_t ssd1322_init_steps[SSD1322_INIT_STEPS] = {
    {0xFD, 1, {0x12}},        // command lock off
    {0xAE, 0, {0}},           // display off
    {0xB3, 1, {0x91}},        // clock divider and oscillator
    {0xCA, 1, {0x3F}},        // MUX ratio 64
    {0xA2, 1, {0x00}},        // display offset
    {0xA1, 1, {0x00}},        // start line
    {0xA0, 2, {0x14, 0x11}},  // remap: horizontal, nibble remap, COM reversed; dual COM
    {0xB5, 1, {0x00}},        // GPIOs off
    {0xAB, 1, {0x01}},        // internal VDD regulator
    {0xB4, 2, {0xA0, 0xFD}},  // enhancement A: external VSL, better low grey
    {0xC1, 1, {0x9F}},        // contrast current
    {0xC7, 1, {0x0F}},        // master current
    {0xB9, 0, {0}},           // default linear grey table
    {0xB1, 1, {0xE2}},        // phase 1 and 2 lengths
    {0xD1, 2, {0x82, 0x20}},  // enhancement B
    {0xBB, 1, {0x1F}},        // precharge voltage
    {0xB6, 1, {0x08}},        // second precharge period
    {0xBE, 1, {0x07}},        // VCOMH
    {0xA6, 0, {0}},           // normal display
    {0xA9, 0, {0}},           // partial display off
};

int ssd1322_columns(int col_offset, int w, uint8_t out[2])
{
    if (col_offset < 0 || w <= 0 || w % 4 != 0 || col_offset + w / 4 > 120) {
        return -1;
    }
    out[0] = (uint8_t)col_offset;
    out[1] = (uint8_t)(col_offset + w / 4 - 1);
    return 0;
}

size_t ssd1322_pack(const uint16_t *px, int w, int h, int x0, int width, uint8_t *out)
{
    if (width <= 0 || width % 2 != 0 || w < 0 || h < 0 || x0 < 0 || x0 + w > width) {
        return 0;
    }
    size_t row = (size_t)width / 2;
    memset(out, 0, row * (size_t)h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t g = (uint8_t)(px[y * w + x] & 0x0F);
            int c = x0 + x;
            out[(size_t)y * row + (size_t)c / 2] |= (c % 2 == 0) ? (uint8_t)(g << 4) : g;
        }
    }
    return row * (size_t)h;
}
