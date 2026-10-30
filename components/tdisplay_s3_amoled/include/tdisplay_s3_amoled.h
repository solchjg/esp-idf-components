// LilyGO T-Display-S3 AMOLED 1.91" board support: every number the panel
// bring-up reads, stated here rather than taken from a driver default. Pin map
// from LilyGO's T-Display-S3-AMOLED and LilyGo-AMOLED-Series sources (the QSPI
// wiring is the same on the touch and non-touch boards). Plain macros, no
// ESP-IDF headers, so the host tier compiles it as is and any project for this
// board can reuse it.
#pragma once

#define TDISPLAY_S3_AMOLED_LCD_DATA {18, 7, 48, 5}  // QSPI D0 – D3
#define TDISPLAY_S3_AMOLED_LCD_SCK 47
#define TDISPLAY_S3_AMOLED_LCD_CS 6
#define TDISPLAY_S3_AMOLED_LCD_RST 17
#define TDISPLAY_S3_AMOLED_LCD_TE 9      // tearing-effect output, unused
#define TDISPLAY_S3_AMOLED_LCD_PWR 38    // panel power enable on the touch board, LED on the other
#define TDISPLAY_S3_AMOLED_LCD_PCLK_HZ 75000000  // QSPI clock (the SPI divider rounds it to 80 MHz)

// Keys, active low: BOOT on GPIO 0 (a strapping pin) and the user key, an RTC
// GPIO, so it can wake the chip from deep sleep (LilyGO factory pins_config.h).
#define TDISPLAY_S3_AMOLED_KEY_BOOT 0
#define TDISPLAY_S3_AMOLED_KEY 21

// RM67162 MADCTL for landscape with the USB port on the right: MX | MV.
#define TDISPLAY_S3_AMOLED_LCD_MADCTL 0x60

// Visible area in landscape; the panel is 240 x 536 native, no RAM offset.
#define TDISPLAY_S3_AMOLED_LCD_W 536
#define TDISPLAY_S3_AMOLED_LCD_H 240

// Brightness (RM67162 register 0x51) the panel is lit at; 0 is dark.
#define TDISPLAY_S3_AMOLED_LCD_BRIGHTNESS 0xD0
