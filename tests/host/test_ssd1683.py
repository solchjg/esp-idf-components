"""Host tier: ssd1683, the init sequence for a panel's size and the refresh steps."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ssd1683_core.h"

static void show(const ssd1683_step_t *s, int n)
{
    for (int i = 0; i < n; i++) {
        printf("%02X", s[i].cmd);
        for (int j = 0; j < s[i].n; j++) printf(",%02X", s[i].p[j]);
        printf(" ");
    }
}

// argv: init <w> <h> | refresh <full 0|1>
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "init")) {
        ssd1683_step_t s[SSD1683_INIT_STEPS];
        int r = ssd1683_init_steps(atoi(argv[2]), atoi(argv[3]), s);
        printf("%d ", r);
        if (r == 0) show(s, SSD1683_INIT_STEPS);
    } else {
        ssd1683_step_t s[2];
        ssd1683_refresh_steps(atoi(argv[2]), s);
        show(s, 2);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("ssd1683", HARNESS, ["ssd1683/ssd1683_core.c"], ["ssd1683/include"])


def test_init_for_the_4_2_inch_panel(run):
    assert run("init", 400, 300).split() == [
        "0", "01,2B,01,00", "3C,05", "18,80", "11,03", "44,00,31", "45,00,00,2B,01"]


@pytest.mark.parametrize("w,h", [(404, 300), (396, 300), (400, 301), (0, 300), (401, 300)])
def test_init_refuses_a_size_the_controller_cannot_hold(run, w, h):
    assert run("init", w, h).split() == ["-1"]


def test_refresh_steps_full_and_partial(run):
    assert run("refresh", 1).split() == ["21,40,00", "22,F7"]
    assert run("refresh", 0).split() == ["21,00,00", "22,FC"]
