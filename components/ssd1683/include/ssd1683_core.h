// SSD1683 e-paper controller (up to 400 x 300, black and white), pure core: the
// init sequence for a panel's size and the commands of a full and a partial
// refresh. No ESP-IDF headers, so the host tier compiles it as is.
#pragma once

#include <stdint.h>

// One controller command and its parameters (sent as data, DC high).
typedef struct {
    uint8_t cmd;
    uint8_t n;     // parameters, 0 – 4
    uint8_t p[4];
} ssd1683_step_t;

#define SSD1683_INIT_STEPS 6

#define SSD1683_SW_RESET 0x12
#define SSD1683_WRITE_BW 0x24     // the new image (1 = white)
#define SSD1683_WRITE_RED 0x26    // the old image, the base of a partial refresh
#define SSD1683_SET_X 0x4E
#define SSD1683_SET_Y 0x4F
#define SSD1683_UPDATE_CTRL1 0x21
#define SSD1683_UPDATE_CTRL2 0x22
#define SSD1683_ACTIVATE 0x20
#define SSD1683_POWER_OFF 0x83    // UPDATE_CTRL2: the analog block and the clock off
#define SSD1683_DEEP_SLEEP 0x10   // parameter 0x01: mode 1, ended by a hardware reset

// After the software reset: the gate count, the border following white, the
// internal temperature sensor, X then Y increment, and the RAM window of the
// whole panel. Returns 0, or -1 when w is not a multiple of 8 or the size is
// out of the controller's 400 x 300.
int ssd1683_init_steps(int w, int h, ssd1683_step_t out[SSD1683_INIT_STEPS]);

// The two update steps of a refresh (UPDATE_CTRL1, UPDATE_CTRL2), before
// ACTIVATE: a full refresh (the whole waveform, the red RAM read as 0) or a
// partial one (the difference between the two RAMs).
void ssd1683_refresh_steps(int full, ssd1683_step_t out[2]);
