"""Host tier: max17048, the scales, the VERSION check and the read sequence on a fake bus."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "max17048_core.h"

static uint16_t regs[16];
static int nak;

static int fake_read16(void *ctx, uint8_t reg, uint16_t *v)
{
    if (nak) return nak;
    printf("r %02x\n", reg);
    *v = regs[reg];
    return 0;
}

static const max17048_bus_t bus = {fake_read16, NULL};

// argv: mv <raw> | pct <raw> | check <version> <nak> | read <vcell> <soc> <nak>
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "mv")) {
        printf("%u\n", max17048_mv((uint16_t)atoi(argv[2])));
    } else if (!strcmp(argv[1], "pct")) {
        printf("%u\n", max17048_pct((uint16_t)atoi(argv[2])));
    } else if (!strcmp(argv[1], "check")) {
        regs[MAX17048_REG_VERSION] = (uint16_t)strtol(argv[2], NULL, 16);
        nak = atoi(argv[3]);
        printf("= %d\n", max17048_core_check(&bus));
    } else {
        regs[MAX17048_REG_VCELL] = (uint16_t)atoi(argv[2]);
        regs[MAX17048_REG_SOC] = (uint16_t)atoi(argv[3]);
        nak = atoi(argv[4]);
        max17048_reading_t r = {0, 0};
        int err = max17048_core_read(&bus, &r);
        printf("= %d %u %u\n", err, r.mv, r.pct);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("max17048", HARNESS, ["max17048/max17048_core.c"], ["max17048/include"])


@pytest.mark.parametrize("raw,mv", [(0, 0), (51200, 4000), (53248, 4160), (65535, 5119),
                                    (44800, 3500)])
def test_cell_voltage_78_125_uv_per_bit(run, raw, mv):
    assert int(run("mv", raw)) == mv


@pytest.mark.parametrize("raw,pct", [(0, 0), (255, 0), (256, 1), (81 * 256 + 200, 81),
                                     (100 * 256, 100), (103 * 256, 100)])
def test_charge_whole_percent_at_most_100(run, raw, pct):
    assert int(run("pct", raw)) == pct


@pytest.mark.parametrize("version,res", [("0011", 0), ("0012", 0), ("001f", 0),
                                         ("0000", -1), ("ffff", -1), ("0110", -1)])
def test_version_check(run, version, res):
    assert run("check", version, 0).splitlines() == ["r 08", f"= {res}"]


def test_check_bus_error_passes_through(run):
    assert run("check", "0012", 263).strip() == "= 263"


def test_read_vcell_then_soc(run):
    assert run("read", 51200, 81 * 256, 0).splitlines() == ["r 02", "r 04", "= 0 4000 81"]


def test_read_bus_error_leaves_reading(run):
    assert run("read", 51200, 81 * 256, 263).strip() == "= 263 0 0"
