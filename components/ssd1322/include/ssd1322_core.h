// SSD1322 OLED controller (256 x 64, 16 grey levels), pure core: the init
// sequence, the column window and the 4-bit packing of a pixel buffer. No
// ESP-IDF headers, so the host tier compiles it as is.
#pragma once

#include <stddef.h>
#include <stdint.h>

// One controller command and its parameters (sent as data, DC high).
typedef struct {
    uint8_t cmd;
    uint8_t n;     // parameters, 0 – 2
    uint8_t p[2];
} ssd1322_step_t;

#define SSD1322_INIT_STEPS 20

// Unlock, display off, clock, 64 MUX, no offset or start line, the remap of
// Newhaven's 256 x 64 modules (horizontal addressing, nibble remap, COM scan
// reversed, dual COM), internal VDD, the enhancements, contrast and master
// current, the default grey table, phase lengths, precharge and VCOMH, normal
// display, partial mode off. The display stays off: ssd1322_display turns it on.
extern const ssd1322_step_t ssd1322_init_steps[SSD1322_INIT_STEPS];

#define SSD1322_SET_COLUMN 0x15
#define SSD1322_SET_ROW 0x75
#define SSD1322_WRITE_RAM 0x5C
#define SSD1322_DISPLAY_OFF 0xAE
#define SSD1322_DISPLAY_ON 0xAF

// The column window for a panel w pixels wide starting at controller column
// col_offset: columns address 4 pixels each. 0, or -1 when w is not a
// positive multiple of 4 or the window passes the controller's 120 columns.
int ssd1322_columns(int col_offset, int w, uint8_t out[2]);

// Packs h rows of w pixels (each the low 4 bits of px, its grey level) into
// rows of width/2 bytes at x0, two pixels a byte with the left one in the high
// nibble; the rest of each row is black. Returns the bytes written, or 0 when
// width is odd or the pixels do not fit.
size_t ssd1322_pack(const uint16_t *px, int w, int h, int x0, int width, uint8_t *out);
