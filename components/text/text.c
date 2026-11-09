#include "text.h"

#include <string.h>

const text_font_t text_font_large = {FONT_W, FONT_H, &font16x32[0][0]};
const text_font_t text_font_small = {FONT_SMALL_W, FONT_SMALL_H, &font12x24[0][0]};
const text_font_t text_font_mini = {FONT_MINI_W, FONT_MINI_H, &font8x16[0][0]};
const text_font_t text_font_tiny = {FONT_TINY_W, FONT_TINY_H, &font6x10[0][0]};

int text_font_width(const text_font_t *font, const char *s)
{
    return (int)strlen(s) * font->w;
}

void text_font_render(const text_font_t *font, const char *s, uint16_t fg, uint16_t bg,
                      uint16_t *buf, int w)
{
    int n = (int)strlen(s);
    for (int y = 0; y < font->h; y++) {
        for (int x = 0; x < w; x++) {
            int i = x / font->w;
            uint16_t px = bg;
            if (i < n) {
                unsigned char c = (unsigned char)s[i];
                if (c < FONT_FIRST || c > FONT_LAST) {
                    c = '?';
                }
                if (font->rows[(c - FONT_FIRST) * font->h + y] & (0x8000u >> (x % font->w))) {
                    px = fg;
                }
            }
            buf[y * w + x] = px;
        }
    }
}

int text_width(const char *s)
{
    return text_font_width(&text_font_large, s);
}

void text_render(const char *s, uint16_t fg, uint16_t bg, uint16_t *buf, int w)
{
    text_font_render(&text_font_large, s, fg, bg, buf, w);
}
