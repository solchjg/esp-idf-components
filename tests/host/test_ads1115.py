"""Host tier: ads1115, the config word, the scale and the single-shot sequence on a fake bus."""

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ads1115_core.h"

static uint16_t conv[32];  // successive conversion results
static int nconv, next_conv;
static int nak, busy_polls;

static int fake_read16(void *ctx, uint8_t reg, uint16_t *v)
{
    if (nak) return nak;
    printf("r %02x\n", reg);
    if (reg == ADS1115_REG_CONFIG) {
        *v = busy_polls > 0 ? 0x0383 : 0x8383;
        if (busy_polls > 0) busy_polls--;
    } else {
        *v = conv[next_conv < nconv ? next_conv++ : nconv - 1];
    }
    return 0;
}

static int fake_write16(void *ctx, uint8_t reg, uint16_t v)
{
    if (nak) return nak;
    printf("w %02x %04x\n", reg, v);
    return 0;
}

static void fake_delay(void *ctx, uint32_t ms) { printf("d %" PRIu32 "\n", ms); }

static const ads1115_bus_t bus = {fake_read16, fake_write16, fake_delay, NULL};

// argv: cfg <mux> <pga> <dr> | uv <raw> <pga> | ms <dr>
//     | mean <n> <nak> <busy_polls> <raw>...
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "cfg")) {
        ads1115_settings_t s = {atoi(argv[2]), atoi(argv[3]), atoi(argv[4])};
        printf("%04x\n", ads1115_config_single(&s));
    } else if (!strcmp(argv[1], "uv")) {
        printf("%" PRId32 "\n", ads1115_uv((int16_t)atoi(argv[2]), atoi(argv[3])));
    } else if (!strcmp(argv[1], "ms")) {
        printf("%" PRIu32 "\n", ads1115_conversion_ms(atoi(argv[2])));
    } else {
        int n = atoi(argv[2]);
        nak = atoi(argv[3]);
        busy_polls = atoi(argv[4]);
        for (int i = 5; i < argc; i++) conv[nconv++] = (uint16_t)atoi(argv[i]);
        ads1115_settings_t s = {ADS1115_MUX_A0_A1, ADS1115_PGA_4096, ADS1115_DR_128};
        int32_t uv = 0;
        int err = ads1115_core_read_mean(&bus, &s, n, &uv);
        printf("= %d %" PRId32 "\n", err, uv);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("ads1115", HARNESS, ["ads1115/ads1115_core.c"], ["ads1115/include"])


def test_config_word_a0_a1_4096_single_shot(run):
    # FR-29.2 (core): OS start, MUX 000, PGA 001, MODE single, 128 SPS, comparator off.
    assert run("cfg", 0, 1, 4).strip() == "8383"


@pytest.mark.parametrize("mux,pga,dr,word", [(4, 0, 0, "c103"), (7, 5, 7, "fbe3"),
                                             (3, 2, 5, "b5a3")])
def test_config_word_fields(run, mux, pga, dr, word):
    assert run("cfg", mux, pga, dr).strip() == word


@pytest.mark.parametrize("raw,pga,uv", [(32767, 1, 4095875), (-32768, 1, -4096000),
                                        (3200, 1, 400000), (16000, 1, 2000000),
                                        (1, 5, 7), (0, 0, 0)])
def test_scale(run, raw, pga, uv):
    assert int(run("uv", raw, pga)) == uv


@pytest.mark.parametrize("dr,ms", [(0, 126), (4, 9), (7, 3)])
def test_conversion_time(run, dr, ms):
    assert int(run("ms", dr)) == ms


def test_mean_of_four_single_shot(run):
    out = run("mean", 4, 0, 0, 3200, 3201, 3202, 3203).splitlines()
    # Each conversion: start in single shot, wait, poll OS, read the result.
    assert out[:4] == ["w 01 8383", "d 9", "r 01", "r 00"]
    assert out.count("w 01 8383") == 4
    assert not any(line.startswith("w 00") for line in out)
    assert out[-1] == "= 0 400187"  # mean code 3201.5 × 125 µV, truncated


def test_busy_polls_then_done(run):
    out = run("mean", 1, 0, 2, 3200).splitlines()
    assert out[:6] == ["w 01 8383", "d 9", "r 01", "d 1", "r 01", "d 1"]
    assert out[-1] == "= 0 400000"


def test_never_finishes_is_busy(run):
    assert run("mean", 1, 0, 99, 3200).splitlines()[-1] == "= -1 0"


def test_bus_error_passes_through(run):
    assert run("mean", 4, 263, 0, 3200).splitlines()[-1] == "= 263 0"


@pytest.mark.parametrize("n", [0, 17])
def test_n_out_of_range(run, n):
    assert run("mean", n, 0, 0, 3200).splitlines()[-1] == "= -2 0"


def test_negative_mean(run):
    assert run("mean", 2, 0, 0, 65535, 65534).splitlines()[-1] == "= 0 -187"  # mean code -1.5 × 125 µV
