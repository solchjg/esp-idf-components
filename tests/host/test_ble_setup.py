"""Host tier: BLE setup, the pure core (staged body, control commands, pages, passkey)."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ble_setup_core.h"

// argv: cap, then commands: a:<text> append, A:<n> append n 'x', r reset,
// p:<off> page of a 1300-byte "0123456789..." text, c:<text> control, k:<n> passkey,
// F which source `settings` reads page (form?) and from where,
// S:<12 hex digits> an address through bs_addr_static, K:<stored 0|1>:<bonds> bs_addr_keep
int main(int argc, char **argv)
{
    size_t cap = (size_t)atoi(argv[1]);
    char *buf = malloc(cap + 1);
    bs_session_t s;
    bs_init(&s, buf, cap);
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == 'a') {
            int ok = bs_append(&s, a + 2, strlen(a + 2));
            printf("%d:%zu:%s;", ok, s.len, s.buf);
        } else if (a[0] == 'A') {
            size_t n = (size_t)atoi(a + 2);
            char *x = malloc(n);
            memset(x, 'x', n);
            int ok = bs_append(&s, x, n);
            printf("%d:%zu;", ok, s.len);
            free(x);
        } else if (a[0] == 'r') {
            bs_reset(&s);
            printf("%zu:%u:%u;", s.len, (unsigned)s.read_off, (unsigned)s.hist_line);
        } else if (a[0] == 'p') {
            static char all[1300];
            for (int j = 0; j < 1300; j++) all[j] = (char)('0' + j % 10);
            const char *start;
            size_t n = bs_page(all, sizeof all, (uint32_t)atol(a + 2), &start);
            printf("%zu:%ld;", n, (long)(start - all));
        } else if (a[0] == 'c') {
            uint32_t arg = 7;
            bs_ctrl_t c = bs_control(a + 2, strlen(a + 2), &arg);
            if (c == BS_CTRL_READ) { s.read_off = arg; s.read_form = false; }
            if (c == BS_CTRL_FORM) { s.read_off = arg; s.read_form = true; }
            if (c == BS_CTRL_HIST) s.hist_line = arg;
            printf("%d:%u;", (int)c, (unsigned)arg);
        } else if (a[0] == 'T') {
            printf("%lld;", (long long)bs_time_ms(a + 2, strlen(a + 2)));
        } else if (a[0] == 'F') {
            printf("%d:%u;", (int)s.read_form, (unsigned)s.read_off);
        } else if (a[0] == 'S') {
            uint8_t addr[6];
            for (int j = 0; j < 6; j++) sscanf(a + 2 + 2 * j, "%2hhx", &addr[j]);
            int ok = bs_addr_static(addr);
            printf("%d:", ok);
            for (int j = 0; j < 6; j++) printf("%02x", addr[j]);
            printf(";");
        } else if (a[0] == 'K') {
            printf("%d;", (int)bs_addr_keep(a[2] == '1', atoi(a + 4)));
        } else if (a[0] == 'k') {
            char out[7];
            bs_passkey_text((uint32_t)atol(a + 2), out);
            printf("%s;", out);
        }
    }
    free(buf);
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    run = compile_harness("ble_setup", HARNESS, ["ble_setup/ble_setup_core.c"],
                          ["ble_setup/include"])
    return lambda *args: run(*args).rstrip(";").split(";")


def test_writes_append_until_cap(core):
    assert core(17, "a:ssid=ab", "a:&password=") == ["1:7:ssid=ab", "1:17:ssid=ab&password="]


def test_overflow_empties_the_body(core):
    assert core(16, "A:10", "A:7", "a:ok") == ["1:10", "0:0", "1:2:ok"]


def test_body_at_exactly_cap_is_kept(core):
    assert core(2048, "A:512", "A:512", "A:512", "A:512", "A:1") == \
        ["1:512", "1:1024", "1:1536", "1:2048", "0:0"]


def test_reset_clears_body_read_page_and_history_line(core):
    assert core(64, "a:x=1", "c:read=512", "c:hist=40", "r") == \
        ["1:3:x=1", "3:512", "4:40", "0:0:0"]


@pytest.mark.parametrize("value,expected", [
    ("apply", "1:7"), ("done", "2:7"), ("read=0", "3:0"), ("read=512", "3:512"),
    ("read=999999999", "3:999999999"),
    ("", "0:7"), ("Apply", "0:7"), ("apply ", "0:7"), ("read=", "0:7"), ("read=-1", "0:7"),
    ("read=1x", "0:7"), ("read=1234567890", "0:7"), ("done\n", "0:7"),
    ("hist=0", "4:0"), ("hist=10700", "4:10700"), ("hist=", "0:7"), ("hist=x", "0:7"),
    ("hist=1234567890", "0:7"), ("range=", "5:6"), ("range=2026-10-01,2026-10-31", "5:6"),
    ("range", "0:7"), ("Range=,", "0:7"),
    ("forget", "6:7"), ("forget ", "0:7"), ("Forget", "0:7"),
    ("form=0", "7:0"), ("form=512", "7:512"), ("form=", "0:7"), ("form=x", "0:7"),
    ("Form=0", "0:7"),
    ("time=1", "8:5"), ("time=1794052115123", "8:5"), ("time=123456789012345", "8:5"),
    ("time=", "0:7"), ("time=1234567890123456", "0:7"), ("time=-1", "0:7"),
    ("time=17940521151x3", "0:7"), ("Time=1", "0:7"),
])
def test_control_commands(core, value, expected):
    assert core(16, f"c:{value}") == [expected]


def test_time_value(core):
    assert core(16, "T:time=1794052115123", "T:time=0", "T:time=999999999999999") == \
        ["1794052115123", "0", "999999999999999"]


def test_form_pages_until_read_or_reset(core):
    # The page reads the rules with form=, then the settings again with read=;
    # a new connection (reset) starts on the settings.
    assert core(64, "F", "c:form=512", "F", "c:read=0", "F", "c:form=0", "r", "F") == \
        ["0:0", "7:512", "1:512", "3:0", "0:0", "7:0", "0:0:0", "0:0"]


@pytest.mark.parametrize("off,expected", [
    (0, "512:0"), (512, "512:512"), (1024, "276:1024"), (1299, "1:1299"),
    (1300, "0:1300"), (5000, "0:1300"),
])
def test_read_pages_are_at_most_512_bytes(core, off, expected):
    assert core(16, f"p:{off}") == [expected]


@pytest.mark.parametrize("pk,text", [(0, "000000"), (4207, "004207"), (999999, "999999"),
                                     (1000000, "000000")])
def test_passkey_text(core, pk, text):
    assert core(16, f"k:{pk}") == [text]


# The address (bs_addr_static): random bytes, little-endian, become a static
# random address (top two bits of byte 5 set); the 46 random bits all 0 or all 1
# are refused so a new draw is taken.
@pytest.mark.parametrize("raw,expected", [
    ("112233445566", "1:1122334455e6"),
    ("000000000000", "0:0000000000c0"),
    ("0000000000c0", "0:0000000000c0"),
    ("ffffffffff3f", "0:ffffffffffff"),
    ("010000000000", "1:0100000000c0"),
    ("0000000000c1", "1:0000000000c1"),
    ("fffffffffffe", "1:fffffffffffe"),
])
def test_static_address(core, raw, expected):
    assert core(16, f"S:{raw}") == [expected]


# The stored address is used again only while a bond is held: with none (an
# erased flash, a factory reset, a forget) a phone's old pairing must not match.
@pytest.mark.parametrize("stored,bonds,keep", [
    (1, 1, 1), (1, 3, 1), (1, 0, 0), (0, 0, 0), (0, 2, 0),
])
def test_address_kept_only_while_bonded(core, stored, bonds, keep):
    assert core(16, f"K:{stored}:{bonds}") == [str(keep)]
