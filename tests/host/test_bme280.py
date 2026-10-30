"""Host tier: bme280, the pure compensation core and the register sequence on a fake bus."""

import struct

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bme280_core.h"

// The fake chip: a 256-byte register file, a log of writes and delays, and
// knobs for the failures under test.
static uint8_t regs[256];
static int nak;           // nonzero: every transfer fails with this code
static int busy_polls;    // status reads that still report measuring

static int fake_read(void *ctx, uint8_t reg, uint8_t *buf, int len)
{
    if (nak) return nak;
    if (reg == BME280_REG_STATUS) {
        buf[0] = busy_polls > 0 ? BME280_STATUS_MEASURING : 0;
        if (busy_polls > 0) busy_polls--;
        return 0;
    }
    memcpy(buf, &regs[reg], len);
    return 0;
}

static int fake_write(void *ctx, uint8_t reg, uint8_t value)
{
    if (nak) return nak;
    printf("w %02x %02x\n", reg, value);
    regs[reg] = value;
    return 0;
}

static void fake_delay(void *ctx, uint32_t ms) { printf("d %" PRIu32 "\n", ms); }

static const bme280_bus_t bus = {fake_read, fake_write, fake_delay, NULL};

static void hex_into(const char *hex, uint8_t *out)
{
    for (size_t i = 0; hex[2 * i]; i++) sscanf(&hex[2 * i], "%2hhx", &out[i]);
}

static void print_reading(const bme280_reading_t *r)
{
    printf("r %" PRId32 " %" PRIu32 " %" PRIu32 "\n", r->temp_centi_c, r->press_q8_pa,
           r->hum_q10_pct);
}

// argv: comp <tp hex> <h hex> <adc_t> <adc_p> <adc_h>
//     | calib <tp hex> <h hex>
//     | regs <osrs_t> <osrs_p> <osrs_h> <filter>
//     | seq <id hex> <tp hex> <h hex> <data hex> <nak> <busy_polls>
int main(int argc, char **argv)
{
    bme280_settings_t w = {1, 1, 1, 0};
    uint8_t tp[BME280_CALIB_TP_LEN] = {0}, h[BME280_CALIB_H_LEN] = {0};
    if (!strcmp(argv[1], "comp") || !strcmp(argv[1], "calib")) {
        hex_into(argv[2], tp);
        hex_into(argv[3], h);
        bme280_calib_t c;
        bme280_parse_calib(tp, h, &c);
        if (argv[1][1] == 'a') {
            printf("%u %d %d %u %d %d %d %d %d %d %d %d %u %d %u %d %d %d\n", c.t1, c.t2, c.t3,
                   c.p1, c.p2, c.p3, c.p4, c.p5, c.p6, c.p7, c.p8, c.p9, c.h1, c.h2, c.h3, c.h4,
                   c.h5, c.h6);
            return 0;
        }
        bme280_raw_t raw = {atoi(argv[4]), atoi(argv[5]), atoi(argv[6])};
        bme280_reading_t r;
        bme280_compensate(&c, &raw, &r);
        print_reading(&r);
    } else if (!strcmp(argv[1], "regs")) {
        bme280_settings_t s = {atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5])};
        printf("%02x %02x %02x %" PRIu32 "\n", bme280_ctrl_hum(&s), bme280_ctrl_meas_forced(&s),
               bme280_config(&s), bme280_max_measure_ms(&s));
    } else {
        hex_into(argv[2], &regs[BME280_REG_ID]);
        hex_into(argv[3], &regs[BME280_REG_CALIB_TP]);
        hex_into(argv[4], &regs[BME280_REG_CALIB_H]);
        hex_into(argv[5], &regs[BME280_REG_DATA]);
        nak = atoi(argv[6]);
        busy_polls = atoi(argv[7]);
        bme280_calib_t c;
        int err = bme280_core_init(&bus, &w, &c);
        printf("init %d\n", err);
        if (err) return 0;
        bme280_reading_t r;
        err = bme280_core_read(&bus, &w, &c, &r);
        printf("read %d\n", err);
        if (!err) print_reading(&r);
    }
    return 0;
}
"""

# The datasheet's worked example (BMP280 datasheet §8.2, same formulas as the
# BME280's) for temperature and pressure; humidity coefficients of a real BME280.
T = (27504, 26435, -1000)
P = (36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000)
H = dict(h1=75, h2=362, h3=0, h4=313, h5=50, h6=30)
ADC_T, ADC_P = 519888, 415148


def calib_hex(t=T, p=P, h=H):
    tp = struct.pack("<HhhHhhhhhhhh", *t, *p) + bytes([0, h["h1"]])
    h4, h5 = h["h4"] & 0xFFF, h["h5"] & 0xFFF
    hb = struct.pack("<hB", h["h2"], h["h3"]) + bytes(
        [h4 >> 4, (h4 & 0x0F) | ((h5 & 0x0F) << 4), h5 >> 4, h["h6"] & 0xFF])
    return tp.hex(), hb.hex()


def data_hex(adc_t, adc_p, adc_h):
    return bytes([adc_p >> 12, (adc_p >> 4) & 0xFF, (adc_p & 0x0F) << 4,
                  adc_t >> 12, (adc_t >> 4) & 0xFF, (adc_t & 0x0F) << 4,
                  adc_h >> 8, adc_h & 0xFF]).hex()


def reference_humidity(adc_t, adc_h, t=T, h=H):
    """The datasheet's double-precision humidity formula (§8.1)."""
    v1 = (adc_t / 16384.0 - t[0] / 1024.0) * t[1]
    v2 = ((adc_t / 131072.0 - t[0] / 8192.0) ** 2) * t[2]
    t_fine = v1 + v2
    x = t_fine - 76800.0
    x = (adc_h - (h["h4"] * 64.0 + h["h5"] / 16384.0 * x)) * (
        h["h2"] / 65536.0 * (1.0 + h["h6"] / 67108864.0 * x * (1.0 + h["h3"] / 67108864.0 * x)))
    x = x * (1.0 - h["h1"] * x / 524288.0)
    return min(max(x, 0.0), 100.0)


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("bme280", HARNESS, ["bme280/bme280_core.c"], ["bme280/include"])


def comp(core, adc_t, adc_p, adc_h, **kw):
    out = core("comp", *calib_hex(**kw), adc_t, adc_p, adc_h).split()
    return int(out[1]), int(out[2]), int(out[3])


def test_calibration_parse_keeps_signs_and_shared_nibbles(core):
    h = dict(h1=75, h2=-362, h3=7, h4=-313, h5=-50, h6=-30)
    out = core("calib", *calib_hex(h=h)).split()
    assert list(map(int, out)) == [*T, *P, 75, -362, 7, -313, -50, -30]


def test_datasheet_example_temperature_and_pressure(core):
    t, p, _ = comp(core, ADC_T, ADC_P, 30000)
    assert t == 2508, "25.08 °C"
    assert p == 25767233, "the 64-bit formula's exact result, 100653.254 Pa"
    assert round(p / 25600, 2) == 1006.53, "the datasheet's 1006.53 hPa"


@pytest.mark.parametrize("adc_h", [20000, 26000, 30000, 36000])
def test_humidity_matches_the_double_formula(core, adc_h):
    _, _, hq = comp(core, ADC_T, ADC_P, adc_h)
    assert abs(hq / 1024 - reference_humidity(ADC_T, adc_h)) < 0.05


def test_results_are_clamped_to_the_sensor_ranges(core):
    for adc in (0, 0xFFFFF):
        t, p, hq = comp(core, adc, adc, adc & 0xFFFF)
        assert -4000 <= t <= 8500
        assert 30000 * 256 <= p <= 110000 * 256
        assert 0 <= hq <= 102400


def test_weather_monitoring_registers_and_time(core):
    assert core("regs", 1, 1, 1, 0).split() == ["01", "25", "00", "10"], "9.3 ms rounds up to 10"
    assert core("regs", 5, 5, 5, 4).split() == ["05", "b5", "10", "113"], "112.8 ms at x16"


def seq(core, id_hex="60", nak=0, busy=0, adc_h=26000):
    return core("seq", id_hex, *calib_hex(), data_hex(ADC_T, ADC_P, adc_h), nak, busy).splitlines()


def test_one_forced_reading_on_the_bus(core):
    lines = seq(core)
    assert lines[:3] == ["w f2 01", "w f5 00", "init 0"], "ctrl_hum and config in sleep mode"
    assert lines[3:5] == ["w f4 25", "d 10"], "x1, x1, forced, then the maximum time"
    assert lines[5] == "read 0"
    assert lines[6].split()[1] == "2508"


def test_a_slow_measurement_is_polled_then_given_up(core):
    assert seq(core, busy=3)[-2:][0] == "read 0"
    assert "read -2" in seq(core, busy=100), "BME280_E_BUSY after the poll limit"


def test_wrong_chip_or_no_answer_keeps_no_reading(core):
    assert seq(core, id_hex="58") == ["init -1"], "a BMP280 is not a BME280"
    assert seq(core, nak=263) == ["init 263"], "the bus's code passes through (ESP_ERR_TIMEOUT)"
