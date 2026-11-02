"""Host tier: text, both fonts and the renderer (FSD FR-6.15)."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

static uint16_t buf[536 * 32];

// argv: <large|small> <text> <w> : prints the font's cell and the lit pixels' box
int main(int argc, char **argv)
{
    const text_font_t *f = !strcmp(argv[1], "small") ? &text_font_small : &text_font_large;
    int w = atoi(argv[3]);
    text_font_render(f, argv[2], 1, 0, buf, w);
    int x0 = w, x1 = -1, y0 = f->h, y1 = -1;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < w; x++)
            if (buf[y * w + x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    printf("%d %d %d %d %d %d %d\n", f->w, f->h, text_font_width(f, argv[2]), x0, x1, y0, y1);
    if (!strcmp(argv[1], "large")) {
        static uint16_t b2[536 * 32];
        text_render(argv[2], 1, 0, b2, w);
        printf("%d %d\n", text_width(argv[2]), memcmp(buf, b2, sizeof(uint16_t) * w * 32) == 0);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("text", HARNESS,
                           ["text/text.c", "text/font16x32.c", "text/font12x24.c"],
                           ["text/include"])


def test_small_font_cell_and_44_characters_fit(run):
    cell_w, cell_h, width, x0, x1, y0, y1 = map(int, run("small", "W" * 44, 536).split())
    assert (cell_w, cell_h) == (12, 24)
    assert width == 528 and 0 <= x0 and x1 <= 527 and 0 <= y0 and y1 < 24


def test_large_font_unchanged_through_both_calls(run):
    first, second = run("large", "M" * 33, 536).splitlines()
    cell_w, cell_h, width, _, x1, _, _ = map(int, first.split())
    assert (cell_w, cell_h, width) == (16, 32, 528) and x1 <= 527
    assert second == "528 1"


def test_space_is_blank_and_unknown_is_question_mark(run):
    assert run("small", " ", 12).split()[3:] == ["12", "-1", "24", "-1"]
    q = run("small", "?", 12).split()[3:]
    assert run("small", "\x7f", 12).split()[3:] == q
