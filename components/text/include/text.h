// Fixed-width bitmap text rendering (FSD FR-6.2, FR-6.14). Pure C, no ESP-IDF
// headers, so the host tier compiles it as is. Knows nothing about the panel or
// layout.
#pragma once

#include <stdint.h>

#define FONT_W 16  // FR-6.2
#define FONT_H 32
#define FONT_FIRST 0x20
#define FONT_LAST 0x7E
#define FONT_GLYPHS (FONT_LAST - FONT_FIRST + 1)

#define FONT_SMALL_W 12  // FR-6.14
#define FONT_SMALL_H 24

#define FONT_TINY_W 6  // a 256 x 64 OLED's six lines (tdisplay-s3-amoled FR-35.16)
#define FONT_TINY_H 10

// Bit 15 of each row is the leftmost pixel (font16x32.c, font12x24.c,
// font6x10.c, generated).
extern const uint16_t font16x32[FONT_GLYPHS][FONT_H];
extern const uint16_t font12x24[FONT_GLYPHS][FONT_SMALL_H];
extern const uint16_t font6x10[FONT_GLYPHS][FONT_TINY_H];

// A cell font: w x h pixels per glyph, rows[glyph * h + y].
typedef struct {
    int w, h;
    const uint16_t *rows;
} text_font_t;

extern const text_font_t text_font_large;  // 16x32
extern const text_font_t text_font_small;  // 12x24
extern const text_font_t text_font_tiny;   // 6x10

// Width in pixels of s in the 16x32 font.
int text_width(const char *s);

// Render s into a w x FONT_H pixel buffer, left-aligned, filling the rest with
// bg. Characters outside FONT_FIRST – FONT_LAST render as '?'. Colours are
// written as given; byte order for the bus is the caller's concern.
void text_render(const char *s, uint16_t fg, uint16_t bg, uint16_t *buf, int w);

// The same in any font: s into a w x font->h buffer.
int text_font_width(const text_font_t *font, const char *s);
void text_font_render(const text_font_t *font, const char *s, uint16_t fg, uint16_t bg,
                      uint16_t *buf, int w);
