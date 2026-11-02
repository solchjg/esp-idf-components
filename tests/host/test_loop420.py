"""Host tier: loop420, current from the shunt, NAMUR limits and the scale (FSD FR-29.3, FR-29.4)."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "loop420.h"

// argv: <shunt_uv> <shunt_mohm> <lo> <hi>
int main(int argc, char **argv)
{
    loop420_scale_t sc = {strtof(argv[3], NULL), strtof(argv[4], NULL)};
    float ma, v = 0;
    loop420_state_t st;
    bool ok = loop420_convert(&sc, atoi(argv[1]), (uint32_t)atoi(argv[2]), &ma, &v, &st);
    printf("%d %d %.4f %.4f\n", ok, st, ma, v);
    return 0;
}
"""

SHUNT = 100000  # 100 Ω in mΩ


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("loop420", HARNESS, ["loop420/loop420.c"], ["loop420/include"])


def conv(run, ma, lo, hi):
    ok, st, cur, v = run(round(ma * SHUNT), SHUNT, lo, hi).split()
    return ok == "1", int(st), float(cur), float(v)


@pytest.mark.parametrize("ma,state", [(3.5, 1), (21.1, 2), (0.0, 1), (-1.0, 1)])
def test_fault_currents_give_no_value(run, ma, state):
    ok, st, cur, v = conv(run, ma, 0, 100)
    assert not ok and st == state and v == 0
    assert cur == pytest.approx(ma)


@pytest.mark.parametrize("ma,value", [(3.6, -2.5), (4, 0), (12, 50), (20, 100), (21.0, 106.25)])
def test_scale_unclamped_inside_limits(run, ma, value):
    ok, st, _, v = conv(run, ma, 0, 100)
    assert ok and st == 0
    assert v == pytest.approx(value, abs=1e-3)


@pytest.mark.parametrize("ma,value", [(3.6, 102.5), (4, 100), (12, 50), (20, 0), (21.0, -6.25)])
def test_reversed_scale_mirrored(run, ma, value):
    ok, _, _, v = conv(run, ma, 100, 0)
    assert ok
    assert v == pytest.approx(value, abs=1e-3)


def test_default_scale_is_the_current(run):
    ok, _, cur, v = conv(run, 12.345, 4, 20)
    assert ok and v == pytest.approx(cur, abs=1e-4)


def test_zero_shunt_is_a_fault(run):
    ok, st, _, _ = run(400000, 0, 0, 100).split()[:4]
    assert ok == "0" and st == "1"
