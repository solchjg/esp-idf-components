"""Host tier: axp2101, the rail codes, the battery decoding and the rail sequences on a fake bus."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "axp2101_core.h"

static uint8_t regs[256];
static int nak;

static int fake_read8(void *ctx, uint8_t reg, uint8_t *v)
{
    if (nak) return nak;
    *v = regs[reg];
    return 0;
}

static int fake_write8(void *ctx, uint8_t reg, uint8_t v)
{
    if (nak) return nak;
    printf("w %02x %02x\n", reg, v);
    regs[reg] = v;
    return 0;
}

static const axp2101_bus_t bus = {fake_read8, fake_write8, NULL};

// argv: code <rail> <mv> | vbat <h> <l> | status <s1> <s2> <cc>
//     | rail <rail> <mv> <on> <vol_reg_before> <onoff_before> | start <id> <nak>
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "code")) {
        printf("%d\n", axp2101_rail_code(atoi(argv[2]), (unsigned)atoi(argv[3])));
    } else if (!strcmp(argv[1], "vbat")) {
        printf("%u\n", axp2101_vbat_decode((uint8_t)strtol(argv[2], 0, 0), (uint8_t)strtol(argv[3], 0, 0)));
    } else if (!strcmp(argv[1], "status")) {
        axp2101_status_t s = axp2101_status_decode((uint8_t)strtol(argv[2], 0, 0),
                                                   (uint8_t)strtol(argv[3], 0, 0),
                                                   (uint8_t)strtol(argv[4], 0, 0));
        printf("bat=%d vbus=%d on=%d %s %s\n", s.battery, s.vbus, s.charge_on,
               axp2101_current_name(s.current), axp2101_charger_name(s.charger));
    } else if (!strcmp(argv[1], "rail")) {
        int rail = atoi(argv[2]);
        uint8_t vol = rail == AXP2101_DC3 ? AXP2101_REG_DC3_VOL
                      : rail == AXP2101_BLDO1 ? AXP2101_REG_BLDO1_VOL
                      : rail == AXP2101_ALDO3 ? AXP2101_REG_ALDO3_VOL : AXP2101_REG_BLDO2_VOL;
        uint8_t onoff = rail == AXP2101_DC3 ? AXP2101_REG_DC_ONOFF : AXP2101_REG_LDO_ONOFF0;
        regs[vol] = (uint8_t)strtol(argv[5], 0, 0);
        regs[onoff] = (uint8_t)strtol(argv[6], 0, 0);
        printf("= %d\n", axp2101_core_rail(&bus, rail, (unsigned)atoi(argv[3]), atoi(argv[4])));
    } else {
        regs[AXP2101_REG_ID] = (uint8_t)strtol(argv[2], 0, 0);
        regs[AXP2101_REG_ADC_CTRL] = 0x02;
        nak = atoi(argv[3]);
        printf("= %d\n", axp2101_core_start(&bus));
    }
    return 0;
}
"""

DC3, BLDO1, BLDO2, ALDO3 = 0, 1, 2, 3


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("axp2101", HARNESS, ["axp2101/axp2101_core.c"], ["axp2101/include"])


@pytest.mark.parametrize("rail,mv,code", [
    (DC3, 3000, 102),    # the modem rail (FR-35.1): 88 + 14 steps of 100 mV
    (DC3, 500, 0), (DC3, 1200, 70), (DC3, 1220, 71), (DC3, 1540, 87), (DC3, 1600, 88),
    (DC3, 3400, 106),
    (BLDO1, 3300, 28),   # the level shifter
    (BLDO2, 3300, 28),   # the GPS antenna
    (ALDO3, 3300, 28),   # the microSD slot
    (BLDO1, 500, 0), (BLDO2, 3500, 30),
])
def test_rail_codes(run, rail, mv, code):
    assert int(run("code", rail, mv)) == code


@pytest.mark.parametrize("rail,mv", [(DC3, 3500), (DC3, 1210), (DC3, 1550), (DC3, 450),
                                     (BLDO1, 3600), (BLDO2, 3250), (ALDO3, 3600), (4, 3300)])
def test_rail_codes_out_of_range(run, rail, mv):
    assert int(run("code", rail, mv)) == -1


@pytest.mark.parametrize("high,low,mv", [("0x0f", "0x6e", 3950), ("0xef", "0x6e", 3950), ("0", "0", 0)])
def test_vbat_13_bits(run, high, low, mv):
    assert int(run("vbat", high, low)) == mv


def test_status_charging_on_usb(run):
    # s1: VBUS good (bit 5), battery (bit 3); s2: current 01 charging, charger cc (2).
    assert run("status", "0x28", "0x22", "0x02").strip() == "bat=1 vbus=1 on=1 charging cc"


def test_status_on_battery(run):
    assert run("status", "0x08", "0x44", "0x00").strip() == "bat=1 vbus=0 on=0 discharging done"


def test_status_no_battery(run):
    assert run("status", "0x20", "0x05", "0x02").strip() == "bat=0 vbus=1 on=1 idle off"


def test_dc3_on_keeps_other_bits(run):
    # DC3's voltage field is 7 bits (bit 7 kept); its switch is bit 2 of 0x80.
    out = run("rail", DC3, 3000, 1, "0x80", "0x01").splitlines()
    assert out == ["w 84 e6", "w 80 05", "= 0"]


def test_bldo2_on_then_off(run):
    out = run("rail", BLDO2, 3300, 1, "0xe0", "0x10").splitlines()
    assert out == ["w 97 fc", "w 90 30", "= 0"]
    out = run("rail", BLDO2, 3300, 0, "0xfc", "0x30").splitlines()
    assert out == ["w 90 10", "= 0"]  # off: the voltage is left as it was


def test_aldo3_on_then_off_keeps_the_bldos(run):
    # ALDO3 (the T-SIM7080G-S3's microSD slot) is bit 2 of 0x90, beside BLDO1 (bit 4).
    out = run("rail", ALDO3, 3300, 1, "0xe0", "0x10").splitlines()
    assert out == ["w 94 fc", "w 90 14", "= 0"]
    out = run("rail", ALDO3, 3300, 0, "0xfc", "0x14").splitlines()
    assert out == ["w 90 10", "= 0"]


def test_bad_voltage_writes_nothing(run):
    assert run("rail", BLDO1, 3600, 1, "0", "0").splitlines() == ["= -1"]


def test_start_turns_on_adc_and_detection(run):
    out = run("start", "0x4a", 0).splitlines()
    assert out == ["w 30 03", "w 68 01", "= 0"]


def test_start_another_chip(run):
    assert run("start", "0x47", 0).splitlines() == ["= -2"]


def test_start_bus_error_passes_through(run):
    assert run("start", "0x4a", 263).splitlines() == ["= 263"]
