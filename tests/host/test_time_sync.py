"""Host tier: time_sync, the pure synced-state core."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "time_sync_core.h"

// argv: <synced before 0|1> <deep-sleep wake 0|1> | epoch <s>
int main(int argc, char **argv)
{
    if (argv[1][0] == 'e') {
        printf("%d", ts_epoch_valid(atoll(argv[2])));
        return 0;
    }
    printf("%d", ts_synced_at_boot(atoi(argv[1]) != 0, atoi(argv[2]) != 0));
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("time_sync", HARNESS, ["time_sync/time_sync_core.c"],
                           ["time_sync/include"])


def test_a_sync_survives_deep_sleep_only(core):
    assert core(1, 1) == "1", "sync lost across a deep sleep"
    assert core(0, 1) == "0"
    assert core(1, 0) == "0", "sync kept across a power-on or reset"
    assert core(0, 0) == "0"


def test_a_time_from_another_source(core):
    assert core("epoch", 1704067200) == "1"          # 2024-01-01T00:00:00Z
    assert core("epoch", 1798761600) == "1"          # 2027-01-01
    assert core("epoch", 1704067199) == "0", "a time before 2024 accepted"
    assert core("epoch", 0) == "0", "an unset modem clock accepted"
    assert core("epoch", -1) == "0"
    assert core("epoch", 4294967296) == "0"
