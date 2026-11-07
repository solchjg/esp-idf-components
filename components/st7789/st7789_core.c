#include "st7789_core.h"

int st7789_rect_ok(int x, int y, int w, int h, int width, int height)
{
    return x >= 0 && y >= 0 && w > 0 && h > 0 && x <= width - w && y <= height - h;
}

int st7789_band(size_t pixels, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return 0;
    }
    size_t rows = pixels / (size_t)w;
    return rows < (size_t)h ? (int)rows : h;
}
