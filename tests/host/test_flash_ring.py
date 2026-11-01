"""Host tier: flash_ring, the pure ring core over a RAM image of the partition."""
import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flash_ring_core.h"

#define SECTORS 4
#define RECORD 32
#define PER_SECTOR (FR_SECTOR_SIZE / RECORD)
static const fr_geom_t g = {.sectors = SECTORS, .record_size = RECORD};
static uint8_t image[SECTORS * FR_SECTOR_SIZE];
static fr_pos_t pos;

static int reads;

static bool rd(void *ctx, uint32_t off, void *buf, size_t len)
{
    reads++;
    memcpy(buf, image + off, len);
    return true;
}

static void append(int v)
{
    if (pos.slot == 0) {
        memset(image + pos.sector * FR_SECTOR_SIZE, 0xFF, FR_SECTOR_SIZE);
    }
    uint8_t payload[RECORD - FR_TRAILER_SIZE] = {0}, rec[RECORD];
    memcpy(payload, &v, sizeof v);
    fr_encode(&g, rec, payload, pos.seq);
    memcpy(image + fr_offset(&g, pos.sector, pos.slot), rec, RECORD);
    fr_advance(&g, &pos);
}

static void list(void)
{
    fr_cursor_t c;
    uint8_t payload[RECORD - FR_TRAILER_SIZE];
    fr_begin(&g, &c, &pos);
    int n = 0, v = 0, first = -1, last = -1;
    bool ordered = true;
    while (fr_next(&g, &c, rd, NULL, payload)) {
        memcpy(&v, payload, sizeof v);
        if (n && v != last + 1) {
            ordered = false;
        }
        if (!n) {
            first = v;
        }
        last = v;
        n++;
    }
    printf("n=%d first=%d last=%d ordered=%d next=%u/%u seq=%u", n, first, last, ordered,
           (unsigned)pos.sector, (unsigned)pos.slot, (unsigned)pos.seq);
}

int main(int argc, char **argv)
{
    memset(image, 0xFF, sizeof image);
    pos = (fr_pos_t){.seq = 1};
    int count = atoi(argv[2]);
    for (int i = 0; i < count; i++) {
        append(i);
    }
    if (strcmp(argv[1], "reads") == 0) {
        // Reading to the end: the slots read.
        fr_cursor_t c;
        uint8_t payload[RECORD - FR_TRAILER_SIZE];
        fr_begin(&g, &c, &pos);
        reads = 0;
        while (fr_next(&g, &c, rd, NULL, payload)) {
        }
        printf("%d", reads);
    } else if (strcmp(argv[1], "list") == 0) {
        list();
    } else if (strcmp(argv[1], "rescan") == 0) {
        fr_find(&g, rd, NULL, &pos);
        list();
    } else if (strcmp(argv[1], "torn") == 0) {
        // The next slot written halfway, then a boot.
        memset(image + pos.sector * FR_SECTOR_SIZE + pos.slot * RECORD, 0x12, 10);
        fr_find(&g, rd, NULL, &pos);
        append(count);
        list();
    } else if (strcmp(argv[1], "flip") == 0) {
        image[RECORD * atoi(argv[3]) + 4] ^= 1;
        list();
    } else if (strcmp(argv[1], "torn0") == 0) {
        // Slot 0 of the newest sector torn, later slots good.
        memset(image + (count / PER_SECTOR) * FR_SECTOR_SIZE, 0x12, 10);
        fr_find(&g, rd, NULL, &pos);
        list();
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("flash_ring", HARNESS, ["flash_ring/flash_ring_core.c"],
                           ["flash_ring/include"])


# Oldest first, the oldest sector reused when full
def test_empty(core):
    assert core("list", 0).startswith("n=0 ")


def test_appends_read_oldest_first(core):
    assert core("list", 3) == "n=3 first=0 last=2 ordered=1 next=0/3 seq=4"


def test_full_log_reuses_the_oldest_sector(core):
    # 4 sectors of 128 = 512; the 513th erases sector 0.
    assert core("list", 512) == "n=512 first=0 last=511 ordered=1 next=0/0 seq=513"
    assert core("list", 513) == "n=385 first=128 last=512 ordered=1 next=0/1 seq=514"
    assert core("list", 1000).startswith("n=") and "ordered=1" in core("list", 1000)


@pytest.mark.parametrize("count", [1, 127, 128, 129, 512, 513, 1000])
def test_a_boot_finds_the_end_again(core, count):
    assert core("rescan", count) == core("list", count)


# A torn or damaged record is skipped and appends carry on
def test_torn_record_is_skipped(core):
    out = core("torn", 5)
    assert out == "n=6 first=0 last=5 ordered=1 next=0/7 seq=7"


def test_torn_last_slot_moves_to_the_next_sector(core):
    assert "next=1/1" in core("torn", 127)


def test_damaged_record_is_skipped(core):
    assert core("flip", 5, 2).startswith("n=4 first=0 last=4 ordered=0")


def test_torn_first_slot_of_the_newest_sector(core):
    out = core("torn0", 130)
    assert out.startswith("n=129 first=0 last=129") and "next=1/2" in out


# An empty sector costs one read: a large, mostly empty ring reads quickly
def test_empty_sectors_are_skipped(core):
    assert int(core("reads", 0)) == 4
    assert int(core("reads", 3)) == 3 + 3  # the written slots, one per empty sector
