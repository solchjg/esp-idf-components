"""Host tier: rv8803, the pure core (time registers, offset register, trim)."""

import calendar

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "rv8803_core.h"

// argv: to <s>            the seven registers from 0x11 for second s, or "no"
//       from <16 hex>     the eight registers from 0x10 as epoch ms, or "no"
//       off <reg hex>     the offset register as steps
//       reg <steps>       steps as the offset register
//       trim <cur> <err_ms> <elapsed_s>
//       stale <flag hex> <ctrl hex>
//       map               the register addresses the driver uses
int main(int argc, char **argv)
{
    char c = argv[1][0], c2 = argv[1][1];
    if (c == 'm') {
        printf("%02x %02x %02x %02x %02x", RV8803_REG_100TH, RV8803_REG_SEC, RV8803_REG_FLAG,
               RV8803_REG_CTRL, RV8803_REG_OFFSET);
    } else if (c == 't' && c2 == 'o') {
        uint8_t r[7];
        if (!rv8803_regs_from_s(atoll(argv[2]), r)) { printf("no"); return 0; }
        for (int i = 0; i < 7; i++) printf("%02x", r[i]);
    } else if (c == 'f') {
        uint8_t r[8];
        for (int i = 0; i < 8; i++) sscanf(argv[2] + 2 * i, "%2hhx", &r[i]);
        long long ms;
        if (!rv8803_regs_to_ms(r, (int64_t *)&ms)) { printf("no"); return 0; }
        printf("%lld", ms);
    } else if (c == 'o') {
        printf("%d", rv8803_offset_steps((uint8_t)strtol(argv[2], NULL, 16)));
    } else if (c == 'r') {
        printf("%02x", rv8803_offset_reg(atoi(argv[2])));
    } else if (c == 's') {
        printf("%d", rv8803_stale((uint8_t)strtol(argv[2], NULL, 16),
                                  (uint8_t)strtol(argv[3], NULL, 16)));
    } else {
        printf("%d", rv8803_trim(atoi(argv[2]), atoll(argv[3]), atoll(argv[4])));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("rv8803", HARNESS, ["rv8803/rv8803_core.c"], ["rv8803/include"])


def utc(*t):
    return calendar.timegm(t)


@pytest.mark.parametrize("t,regs", [
    ((2000, 1, 1, 0, 0, 0), "00000040010100"),   # a Saturday
    ((2026, 11, 6, 12, 8, 35), "3508122006 1126".replace(" ", "")),  # a Friday
    ((2028, 2, 29, 23, 59, 59), "5959230429 0228".replace(" ", "")),  # a Tuesday, leap day
    ((2099, 12, 31, 23, 59, 59), "5959231031 1299".replace(" ", "")),  # a Thursday
])
def test_time_to_registers(core, t, regs):
    assert core("to", utc(*t)) == regs


def test_outside_the_chips_calendar(core):
    assert core("to", utc(1999, 12, 31, 23, 59, 59)) == "no"
    assert core("to", utc(2100, 1, 1, 0, 0, 0)) == "no"


@pytest.mark.parametrize("t", [
    (2000, 1, 1, 0, 0, 0), (2024, 2, 29, 6, 30, 1), (2026, 11, 6, 12, 8, 35),
    (2099, 12, 31, 23, 59, 59),
])
def test_round_trip_with_hundredths(core, t):
    regs = core("to", utc(*t))
    assert core("from", "37" + regs) == str(utc(*t) * 1000 + 370)


@pytest.mark.parametrize("regs", [
    "00000000010100" + "0a",   # month 0? no: year 0x0a is not BCD
    "00" + "60000040010100",   # 60 seconds
    "00" + "00002440010100",   # hour 24
    "00" + "00000040300200",   # 30 February
    "00" + "00000040290201",   # 29 February 2001
    "00" + "00000040000100",   # day 0
    "00" + "00000040011300",   # month 13
    "a0" + "00000040010100",   # 1/100 s not BCD
])
def test_registers_with_no_valid_date(core, regs):
    assert core("from", regs) == "no"


def test_leap_day_of_a_leap_year(core):
    assert core("from", "00" + "00000004290224") == str(utc(2024, 2, 29, 0, 0, 0) * 1000)


@pytest.mark.parametrize("reg,steps", [("00", 0), ("01", 1), ("1f", 31), ("20", -32),
                                       ("3f", -1), ("c1", 1)])
def test_offset_register_to_steps(core, reg, steps):
    assert core("off", reg) == str(steps)


@pytest.mark.parametrize("steps,reg", [(0, "00"), (31, "1f"), (-1, "3f"), (-32, "20"),
                                       (40, "1f"), (-40, "20")])
def test_steps_to_offset_register(core, steps, reg):
    assert core("reg", steps) == reg


@pytest.mark.parametrize("cur,err_ms,elapsed,new", [
    (0, 0, 7 * 86400, 0),
    # A positive step slows the clock (measured by commissioning/rtc-probe on
    # the bench, 2026-11-12: +31 steps -6.2 ppm, -32 steps +7.7 ppm).
    (0, 1000, 7 * 86400, 7),         # +1.65 ppm fast: slowed by 7 steps (1.67 ppm)
    (0, -1000, 7 * 86400, -7),
    (3, 144, 7 * 86400, 4),          # +0.238 ppm: one step
    (0, 60, 7 * 86400, 0),           # under half a step: no change
    (30, 10000, 86400, 31),          # clamped
    (-30, -10000, 86400, -32),
    (5, 1000, 0, 5),                 # no time passed: unchanged
])
def test_trim_takes_out_the_measured_rate(core, cur, err_ms, elapsed, new):
    assert core("trim", cur, err_ms, elapsed) == str(new)


@pytest.mark.parametrize("flag,ctrl,stale", [
    ("00", "00", "0"),  # counting
    ("01", "00", "0"),  # V1F only: compensation paused, the time still counts
    ("02", "00", "1"),  # V2F: the backup ran out
    ("00", "01", "1"),  # RESET held (a supply dip sets it): the time is frozen
    ("00", "40", "0"),  # other control bits do not matter
])
def test_a_frozen_or_lost_time_is_stale(core, flag, ctrl, stale):
    assert core("stale", flag, ctrl) == stale


def test_register_map(core):
    # The extended block 0x10 - 0x17 holds 1/100 s then seconds ... year (the
    # basic block starts with seconds at 0x00, so 0x00 is not hundredths).
    assert core("map") == "10 11 0e 0f 2c"
