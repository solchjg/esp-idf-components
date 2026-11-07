#include "ssd1683_core.h"

int ssd1683_init_steps(int w, int h, ssd1683_step_t out[SSD1683_INIT_STEPS])
{
    if (w <= 0 || h <= 0 || w % 8 != 0 || w > 400 || h > 300) {
        return -1;
    }
    uint8_t y0 = (uint8_t)((h - 1) & 0xFF), y1 = (uint8_t)((h - 1) >> 8);
    const ssd1683_step_t steps[SSD1683_INIT_STEPS] = {
        {0x01, 3, {y0, y1, 0x00}},                 // gates: h - 1, scan order
        {0x3C, 1, {0x05}},                         // border waveform: follows white
        {0x18, 1, {0x80}},                         // internal temperature sensor
        {0x11, 1, {0x03}},                         // X increment, then Y increment
        {0x44, 2, {0x00, (uint8_t)(w / 8 - 1)}},   // RAM X window, in bytes
        {0x45, 4, {0x00, 0x00, y0, y1}},           // RAM Y window
    };
    for (int i = 0; i < SSD1683_INIT_STEPS; i++) {
        out[i] = steps[i];
    }
    return 0;
}

void ssd1683_refresh_steps(int full, ssd1683_step_t out[2])
{
    out[0] = (ssd1683_step_t){SSD1683_UPDATE_CTRL1, 2, {full ? 0x40 : 0x00, 0x00}};
    out[1] = (ssd1683_step_t){SSD1683_UPDATE_CTRL2, 1, {full ? 0xF7 : 0xFC}};
}
