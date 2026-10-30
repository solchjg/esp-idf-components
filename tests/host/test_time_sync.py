"""Host tier: time_sync, the pure synced-state core."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "time_sync_core.h"

// argv: <synced before 0|1> <deep-sleep wake 0|1>
int main(int argc, char **argv)
{
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
