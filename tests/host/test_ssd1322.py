"""Host tier: ssd1322, the init sequence, the column window and the 4-bit packing."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ssd1322_core.h"

static uint16_t px[256 * 10];
static uint8_t out[128 * 10];

// argv: init | cols <offset> <w> | pack <w> <h> <x0> <width> <grey per pixel, row by row>
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "init")) {
        for (int i = 0; i < SSD1322_INIT_STEPS; i++) {
            const ssd1322_step_t *s = &ssd1322_init_steps[i];
            printf("%02X", s->cmd);
            for (int j = 0; j < s->n; j++) printf(",%02X", s->p[j]);
            printf(" ");
        }
    } else if (!strcmp(argv[1], "cols")) {
        uint8_t c[2] = {0, 0};
        int r = ssd1322_columns(atoi(argv[2]), atoi(argv[3]), c);
        printf("%d %02X %02X", r, c[0], c[1]);
    } else {
        int w = atoi(argv[2]), h = atoi(argv[3]);
        for (int i = 0; i < w * h; i++) px[i] = (uint16_t)strtol(argv[6 + i], 0, 0);
        size_t n = ssd1322_pack(px, w, h, atoi(argv[4]), atoi(argv[5]), out);
        printf("%zu", n);
        for (size_t i = 0; i < n; i++) printf(" %02X", out[i]);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("ssd1322", HARNESS, ["ssd1322/ssd1322_core.c"], ["ssd1322/include"])


def test_init_unlocks_first_and_leaves_the_display_off(run):
    steps = run("init").split()
    assert steps[0] == "FD,12" and steps[1] == "AE"
    assert "CA,3F" in steps          # 64 rows
    assert "A0,14,11" in steps       # Newhaven 256 x 64 remap, dual COM
    assert "AF" not in steps         # ssd1322_display turns it on


@pytest.mark.parametrize("offset,w,want", [
    (28, 256, "0 1C 5B"),   # a 256-wide panel at column 28: 64 columns of 4 pixels
    (0, 480, "0 00 77"),    # the controller's whole 480 pixels
    (28, 254, "-1 00 00"),  # not a multiple of 4
    (60, 256, "-1 00 00"),  # past column 119
])
def test_column_window(run, offset, w, want):
    assert run("cols", offset, w) == want


def test_pack_left_pixel_high_nibble_with_offset(run):
    # 3 x 2 pixels at x0 = 1 in a row of 8: column 0 black, then the greys.
    out = run("pack", 3, 2, 1, 8, 15, 1, 0x1F, 2, 3, 4).split()
    assert out == ["8", "0F", "1F", "00", "00",
                   "02", "34", "00", "00"]


@pytest.mark.parametrize("args", [(3, 1, 0, 7), (3, 1, 6, 8)])
def test_pack_refuses_an_odd_row_or_pixels_past_it(run, args):
    w, h, x0, width = args
    assert run("pack", w, h, x0, width, *([1] * (w * h))) == "0"
