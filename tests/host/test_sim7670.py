"""Host tier: sim7670, the commands before an attach and the answer parsers."""

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "sim7670_core.h"

// argv: script <apn> <plmn> | cgmr <answer> | cclk <answer> | gnss <answer>
int main(int argc, char **argv)
{
    const char *op = argv[1];
    if (!strcmp(op, "script")) {
        sim7670_attach_cfg_t c = {.apn = argv[2], .plmn = argv[3]};
        char cmds[SIM7670_ATTACH_CMDS][SIM7670_CMD_MAX];
        int n = sim7670_attach_script(&c, cmds, SIM7670_ATTACH_CMDS);
        printf("= %d\n", n);
        for (int i = 0; i < n; i++) printf("%s\n", cmds[i]);
    } else if (!strcmp(op, "cgmr")) {
        char s[40];
        int r = sim7670_parse_cgmr(argv[2], s, sizeof s);
        printf("%d %s\n", r, r >= 0 ? s : "-");
    } else if (!strcmp(op, "cclk")) {
        int64_t t = 0;
        int r = sim7670_parse_cclk(argv[2], &t);
        printf("%d %" PRId64 "\n", r, t);
    } else {
        sim7670_gnss_t g = {0};
        int r = sim7670_parse_cgnssinfo(argv[2], &g);
        printf("%d %d %.5f %.5f %lld\n", r, g.fix, g.lat, g.lon, (long long)g.utc);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("sim7670", HARNESS,
                           ["sim7670/sim7670_core.c", "sim7080/sim7080_core.c"],
                           ["sim7670/include", "sim7080/include"])


def test_attach_script(run):
    assert run("script", "hologram", "").splitlines() == [
        "= 3", "ATE0", "AT+CMEE=2", 'AT+CGDCONT=1,"IP","hologram"']


@pytest.mark.parametrize("apn,plmn", [('a"b', ""), ("", ""), ("a" * 64, ""), ("hologram", "12")])
def test_attach_script_refuses_bad_settings(run, apn, plmn):
    assert run("script", apn, plmn).splitlines() == ["= -1"]


def test_cgmr_as_seen_on_slot2(run):
    answer = "\r\n+CGMR: 2374B01SIM767XM5A_M_CUS_WEIXUE\r\n\r\nOK\r\n"
    assert run("cgmr", answer).strip() == "30 2374B01SIM767XM5A_M_CUS_WEIXUE"


def test_cgmr_missing(run):
    assert run("cgmr", "OK").strip() == "-2 -"


def test_cclk_is_utc(run):
    # SLOT2, 2026-11-07 01:11:17 UTC: the zone (-20, EST) is reported, not applied.
    assert run("cclk", '+CCLK: "26/11/07,01:11:17-20"\r\n\r\nOK').strip() == "0 1794013877"


@pytest.mark.parametrize("answer", ['+CCLK: "70/01/01,00:00:13+00"', '+CCLK: "26/13/07,01:11:17-20"', "OK"])
def test_cclk_unset_or_bad(run, answer):
    assert run("cclk", answer).split()[0] == "-2"


def test_gnss_no_fix(run):
    assert run("gnss", "+CGNSSINFO: ,,,,,,,,\r\n\r\nOK").strip() == "0 0 0.00000 0.00000 0"


def test_gnss_decimal_degrees(run):
    # mode, GPS, GLONASS, Galileo, BeiDou counts, then the place.
    answer = ("+CGNSSINFO: 3,14,,03,00,40.7127753,N,74.0059728,W,071126,011500.00,"
              "12.3,0.0,0.0,1.2,0.9,0.8\r\n\r\nOK")
    assert run("gnss", answer).strip() == "0 1 40.71278 -74.00597 1794014100"


def test_gnss_degrees_minutes(run):
    # Older firmware: ddmm.mmmmmm and dddmm.mmmmmm, one satellite field fewer.
    answer = "+CGNSSINFO: 2,09,05,00,4042.766518,N,07400.358368,W,071126,011500.0,12.3,0.0,0.0\r\nOK"
    assert run("gnss", answer).strip() == "0 1 40.71278 -74.00597 1794014100"


def test_gnss_south_east(run):
    answer = "+CGNSSINFO: 3,10,,02,01,33.8688,S,151.2093,E,010126,000000.0,5.0,0.0,0.0,1.0,1.0,1.0"
    assert run("gnss", answer).strip() == "0 1 -33.86880 151.20930 1767225600"


@pytest.mark.parametrize("answer", [
    "OK",
    "+CGNSSINFO: 3,14,,03,00,95.1,N,74.0,W,071126,011500.00",     # latitude out of range
    "+CGNSSINFO: 3,14,,03,00,40.7,X,74.0,W,071126,011500.00",     # no N/S
    "+CGNSSINFO: 3,14,,03,00,40.7,N,74.0,W,071326,011500.00",     # month 13
    "+CGNSSINFO: 2,09,05,00,4072.0,N,07400.0,W,071126,011500.0",  # 72 minutes
])
def test_gnss_bad(run, answer):
    assert run("gnss", answer).split()[0] == "-2"
