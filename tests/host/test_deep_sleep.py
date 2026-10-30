"""Host tier: deep_sleep, the pure period checks and wake-cause mapping."""

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "deep_sleep_core.h"

// argv: valid <s> <min> <max> | us <s> | wake <timer 0|1> <ext1 pins hex> <gpio>
int main(int argc, char **argv)
{
    switch (argv[1][0]) {
    case 'v':
        printf("%d", ds_period_valid(strtoul(argv[2], NULL, 10), strtoul(argv[3], NULL, 10),
                                     strtoul(argv[4], NULL, 10)));
        break;
    case 'u':
        printf("%" PRIu64, ds_period_us(strtoul(argv[2], NULL, 10)));
        break;
    default:
        printf("%d", ds_wake(atoi(argv[2]), strtoull(argv[3], NULL, 16), atoi(argv[4])));
    }
    return 0;
}
"""

NONE, TIMER, BUTTON = "0", "1", "2"


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("deep_sleep", HARNESS, ["deep_sleep/deep_sleep_core.c"],
                           ["deep_sleep/include"])


def test_period_range_is_inclusive(core):
    assert core("valid", 1, 1, 86400) == "1"
    assert core("valid", 86400, 1, 86400) == "1"
    assert core("valid", 0, 1, 86400) == "0"
    assert core("valid", 86401, 1, 86400) == "0"


def test_period_in_microseconds_does_not_overflow(core):
    assert core("us", 10) == "10000000"
    assert core("us", 4294967295) == "4294967295000000"


def test_wake_cause(core):
    assert core("wake", 0, "0", -1) == NONE, "cold boot"
    assert core("wake", 1, "0", 21) == TIMER
    assert core("wake", 0, "200000", 21) == BUTTON, "EXT1 on GPIO 21"
    assert core("wake", 1, "200000", 21) == BUTTON, "a press wins over the timer"
    assert core("wake", 0, "1", 21) == NONE, "EXT1 on a pin that is not the wake GPIO"
    assert core("wake", 0, "200000", -1) == NONE, "no wake GPIO configured"
