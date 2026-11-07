"""Host tier: st7789, the colour helpers and the rectangle and band rules."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "st7789_core.h"

// argv: rgb <r> <g> <b> | bus <rgb565> | rect <x> <y> <w> <h> <width> <height>
//       | band <pixels> <w> <h>
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "rgb")) {
        printf("%04X", st7789_rgb565(atoi(argv[2]), atoi(argv[3]), atoi(argv[4])));
    } else if (!strcmp(argv[1], "bus")) {
        printf("%04X", st7789_bus_colour((uint16_t)strtol(argv[2], 0, 0)));
    } else if (!strcmp(argv[1], "rect")) {
        printf("%d", st7789_rect_ok(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                                    atoi(argv[5]), atoi(argv[6]), atoi(argv[7])));
    } else {
        printf("%d", st7789_band((size_t)atol(argv[2]), atoi(argv[3]), atoi(argv[4])));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("st7789", HARNESS, ["st7789/st7789_core.c"], ["st7789/include"])


@pytest.mark.parametrize("rgb,want", [
    ((255, 0, 0), "F800"), ((0, 255, 0), "07E0"), ((0, 0, 255), "001F"),
    ((255, 255, 255), "FFFF"), ((7, 3, 7), "0000"),   # the dropped low bits
])
def test_rgb565(run, rgb, want):
    assert run("rgb", *rgb) == want


def test_bus_colour_sends_the_high_byte_first(run):
    assert run("bus", 0xF800) == "00F8"
    assert run("bus", 0x1234) == "3412"


@pytest.mark.parametrize("rect,want", [
    ((0, 0, 170, 320), 1),     # the whole 170 x 320 portrait panel
    ((169, 319, 1, 1), 1),     # the last pixel
    ((1, 0, 170, 320), 0),     # one column past the right edge
    ((0, 1, 170, 320), 0),     # one row past the bottom
    ((-1, 0, 10, 10), 0),
    ((0, 0, 0, 10), 0),        # empty
    ((0, 0, 10, -1), 0),
])
def test_rect_inside_the_panel(run, rect, want):
    assert run("rect", *rect, 170, 320) == str(want)


@pytest.mark.parametrize("pixels,w,h,want", [
    (320 * 20, 320, 170, 20),  # a 20-row canvas fills in bands of 20
    (320 * 20, 320, 7, 7),     # a short fill in one band
    (319, 320, 10, 0),         # not one row fits
    (100, 0, 10, 0),
    (100, 10, 0, 0),
])
def test_band(run, pixels, w, h, want):
    assert run("band", pixels, w, h) == str(want)
