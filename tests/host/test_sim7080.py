"""Host tier: sim7080, the attach commands, network lock, PSM timers and grant, and the answer parsers."""

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim7080_core.h"

// argv: cops <plmn> | bits <s> <tau> | secs <bits> <tau> | cpsms <psm> <tau_s> <active_s>
//     | script <apn> <plmn> <rat> | cereg <answer> | keep <answer> <limit>
//     | csq <answer> | cclk <answer> | oper <answer> | mode <answer> | cgmr <answer>
//     | gnss <answer> | apn <apn> | plmn <plmn>
int main(int argc, char **argv)
{
    const char *op = argv[1];
    char buf[SIM7080_CMD_MAX];
    if (!strcmp(op, "cops")) {
        int n = sim7080_cops_cmd(argv[2], buf, sizeof buf);
        printf("%d %s\n", n, n >= 0 ? buf : "-");
    } else if (!strcmp(op, "bits")) {
        char b[9];
        int r = sim7080_timer_bits((uint32_t)atol(argv[2]), atoi(argv[3]), b);
        printf("%d %s\n", r, r == 0 ? b : "-");
    } else if (!strcmp(op, "secs")) {
        printf("%ld\n", sim7080_timer_s(argv[2], atoi(argv[3])));
    } else if (!strcmp(op, "cpsms")) {
        sim7080_attach_cfg_t c = {.psm = atoi(argv[2]), .psm_tau_s = (uint32_t)atol(argv[3]),
                                  .psm_active_s = (uint32_t)atol(argv[4])};
        int n = sim7080_cpsms_cmd(&c, buf, sizeof buf);
        printf("%d %s\n", n, n >= 0 ? buf : "-");
    } else if (!strcmp(op, "script")) {
        sim7080_attach_cfg_t c = {.apn = argv[2], .plmn = argv[3], .rat = atoi(argv[4])};
        char cmds[SIM7080_ATTACH_CMDS][SIM7080_CMD_MAX];
        int n = sim7080_attach_script(&c, cmds, SIM7080_ATTACH_CMDS);
        printf("= %d\n", n);
        for (int i = 0; i < n; i++) printf("%s\n", cmds[i]);
    } else if (!strcmp(op, "cereg") || !strcmp(op, "keep")) {
        sim7080_cereg_t c;
        int r = sim7080_parse_cereg(argv[2], &c);
        if (r != 0) {
            printf("%d\n", r);
        } else if (!strcmp(op, "cereg")) {
            printf("stat=%d reg=%d active=%ld tau=%ld\n", c.stat, sim7080_registered(&c),
                   c.active_s, c.tau_s);
        } else {
            printf("%s\n", sim7080_psm_keep(&c, atol(argv[3])) ? "psm" : "off");
        }
    } else if (!strcmp(op, "csq")) {
        int rssi = -5;
        int r = sim7080_parse_csq(argv[2], &rssi);
        printf("%d %d\n", r, rssi);
    } else if (!strcmp(op, "cclk")) {
        int64_t t = 0;
        int r = sim7080_parse_cclk(argv[2], &t);
        printf("%d %" PRId64 "\n", r, t);
    } else if (!strcmp(op, "oper") || !strcmp(op, "cgmr")) {
        char s[16];
        int r = !strcmp(op, "oper") ? sim7080_parse_cops(argv[2], s, sizeof s)
                                    : sim7080_parse_cgmr(argv[2], s, sizeof s);
        printf("%d %s\n", r, r >= 0 ? s : "-");
    } else if (!strcmp(op, "mode")) {
        int m = -9;
        int r = sim7080_parse_cops_mode(argv[2], &m);
        printf("%d %d\n", r, m);
    } else if (!strcmp(op, "gnss")) {
        sim7080_gnss_t g = {0};
        int r = sim7080_parse_cgnsinf(argv[2], &g);
        printf("%d %d %.5f %.5f\n", r, g.fix, g.fix ? g.lat : 0, g.fix ? g.lon : 0);
    } else if (!strcmp(op, "apn")) {
        printf("%d\n", sim7080_apn_valid(argv[2]));
    } else {
        printf("%d\n", sim7080_plmn_valid(argv[2]));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("sim7080", HARNESS, ["sim7080/sim7080_core.c"], ["sim7080/include"])


def grant(active, tau):
    """A registered +CEREG mode 4 report with the given timer octets, as the modem sends it."""
    timers = f',,,"{active}","{tau}"' if active else ""
    return f'\r\n+CEREG: 4,1,"2A1F","0B3C4D5E",7{timers}\r\n\r\nOK\r\n'


# FR-35.11 (core): the lock on a full attach, automatic choice when empty.
def test_lock_to_network(run):
    assert run("cops", "310410").split(" ", 1)[1].strip() == 'AT+COPS=1,2,"310410"'


def test_empty_plmn_undoes_the_lock(run):
    assert run("cops", "").split(" ", 1)[1].strip() == "AT+COPS=0"


@pytest.mark.parametrize("plmn", ["3104", "3104101", "31041a", '310"0'])
def test_bad_plmn_refused(run, plmn):
    assert run("cops", plmn).strip() == "-1 -"


# 3GPP TS 24.008 timer octets: T3324 units 2 s, 1 min, 6 min; T3412 ext 10 min,
# 1 h, 10 h, 2 s, 30 s, 1 min, 320 h.
@pytest.mark.parametrize("bits,tau,s", [
    ("00000101", 0, 10), ("00001000", 0, 16), ("00100101", 0, 300), ("01000010", 0, 720),
    ("11100000", 0, -1), ("01100001", 0, -1),
    ("00100001", 1, 3600), ("00011111", 1, 18600), ("01100101", 1, 10), ("11000001", 1, 1152000),
    ("11100000", 1, -1), ("0010000", 1, -1), ("0010000x", 1, -1),
])
def test_timer_seconds(run, bits, tau, s):
    assert int(run("secs", bits, tau)) == s


@pytest.mark.parametrize("s,tau,bits", [(10, 0, "00000101"), (60, 0, "00011110"),
                                        (300, 0, "00100101"), (3600, 1, "00000110"),
                                        (86400, 1, "00111000"), (61, 0, "00011111")])
def test_timer_bits(run, s, tau, bits):
    assert run("bits", s, tau).strip() == f"0 {bits}"


@pytest.mark.parametrize("s,tau", [(0, 0), (11161, 0), (40 * 1152000, 1)])
def test_timer_bits_out_of_range(run, s, tau):
    assert run("bits", s, tau).strip() == "-1 -"


def test_psm_request(run):
    # TAU 1 h and active time 10 s, as the evaluation asked.
    assert run("cpsms", 1, 3600, 10).split(" ", 1)[1].strip() == 'AT+CPSMS=1,,,"00000110","00000101"'


def test_psm_off_request(run):
    assert run("cpsms", 0, 0, 0).split(" ", 1)[1].strip() == "AT+CPSMS=0"


def test_attach_script(run):
    out = run("script", "iot.1nce.net", "", 0).splitlines()
    assert out == ["= 8", "AT+CFUN=0", "AT+CMEE=2", "AT+CNMP=38", "AT+CMNB=1",
                   'AT+CGDCONT=1,"IP","iot.1nce.net"', "AT+CEREG=4", "AT+CLTS=1", "AT+CFUN=1"]


@pytest.mark.parametrize("rat,cmd", [(1, "AT+CMNB=2"), (2, "AT+CMNB=3")])
def test_attach_script_rat(run, rat, cmd):
    assert run("script", "hologram", "", rat).splitlines()[4] == cmd


@pytest.mark.parametrize("apn,plmn,rat", [('a"b', "", 0), ("", "", 0), ("a" * 63, "", 0),
                                          ("iot.1nce.net", "12", 0), ("iot.1nce.net", "", 3)])
def test_attach_script_refuses_bad_settings(run, apn, plmn, rat):
    assert run("script", apn, plmn, rat).splitlines() == ["= -1"]


def test_apn_and_plmn_rules(run):
    assert run("apn", "iot.1nce.net").strip() == "1"
    assert run("apn", "a b").strip() == "0"
    assert run("plmn", "").strip() == "1"
    assert run("plmn", "311480").strip() == "1"


def test_cereg_with_grant(run):
    out = run("cereg", grant("00001000", "00100001")).strip()
    assert out == "stat=1 reg=1 active=16 tau=3600"


def test_cereg_without_timers(run):
    assert run("cereg", grant(None, None)).strip() == "stat=1 reg=1 active=-1 tau=-1"


def test_cereg_after_psm_resume(run):
    assert run("cereg", "+CEREG: 0,5\r\n\r\nOK").strip() == "stat=5 reg=1 active=-1 tau=-1"


def test_cereg_searching_and_denied(run):
    assert run("cereg", "+CEREG: 4,2\r\nOK").strip() == "stat=2 reg=0 active=-1 tau=-1"
    assert run("cereg", "+CEREG: 4,3\r\nOK").strip() == "stat=3 reg=0 active=-1 tau=-1"


def test_cereg_last_report_counts(run):
    answer = '+CEREG: 4,2\r\n' + grant("00000101", "00100001")
    assert run("cereg", answer).strip() == "stat=1 reg=1 active=10 tau=3600"


def test_cereg_missing(run):
    assert run("cereg", "OK").strip() == "-2"


# FR-35.1 (core): the grant decision with the 60 s limit.
@pytest.mark.parametrize("active,tau,mode", [
    (None, None, "off"),                    # no grant
    ("00100101", "00011111", "off"),        # 300 s active, TAU 18600 s (Verizon at the bench)
    ("00011110", "00100001", "psm"),        # 60 s active
    ("00001000", "00100001", "psm"),        # 16 s active, TAU 3600 s
    ("00011111", "00100001", "off"),        # 62 s active
    ("00001000", "11100000", "off"),        # TAU deactivated
])
def test_grant_decision(run, active, tau, mode):
    assert run("keep", grant(active, tau), 60).strip() == mode


@pytest.mark.parametrize("answer,out", [("+CSQ: 21,99\r\n\r\nOK", "0 21"),
                                        ("+CSQ: 99,99\r\nOK", "0 99"),
                                        ("+CSQ: 40,99\r\nOK", "-2 -5"), ("OK", "-2 -5")])
def test_csq(run, answer, out):
    assert run("csq", answer).strip() == out


@pytest.mark.parametrize("answer,unix", [
    ('+CCLK: "26/11/06,04:40:10+00"\r\n\r\nOK', 1793940010),
    ('+CCLK: "26/11/05,23:40:10-20"\r\nOK', 1793940010),   # EST, 5 h west
    ('+CCLK: "26/11/06,05:40:10+04"\r\nOK', 1793940010),   # 1 h east
])
def test_network_time(run, answer, unix):
    assert run("cclk", answer).strip() == f"0 {unix}"


@pytest.mark.parametrize("answer", ['+CCLK: "80/01/06,00:00:12+00"\r\nOK',
                                    '+CCLK: "23/12/31,23:59:59+00"\r\nOK',
                                    '+CCLK: "26/13/01,00:00:00+00"\r\nOK', "OK"])
def test_network_time_not_set(run, answer):
    assert run("cclk", answer).split()[0] == "-2"


def test_operator_numeric(run):
    assert run("oper", '+COPS: 0,2,"310410",9\r\n\r\nOK').strip() == "6 310410"


def test_operator_none(run):
    assert run("oper", "+COPS: 0\r\n\r\nOK").strip() == "-2 -"


def test_selection_mode(run):
    assert run("mode", '+COPS: 1,2,"311480",9\r\nOK').strip() == "0 1"


def test_firmware_revision(run):
    assert run("cgmr", "\r\nRevision:1951B16SIM7080\r\n\r\nOK").strip() == "14 1951B16SIM7080"


def test_gnss_fix(run):
    answer = "+CGNSINF: 1,1,20261106044010.000,40.71280,-74.00600,10.0,0.0,0.0,1,,1.1,1.4,0.9\r\nOK"
    assert run("gnss", answer).strip() == "0 1 40.71280 -74.00600"


@pytest.mark.parametrize("answer", ["+CGNSINF: 1,0,,,,,,,\r\nOK", "+CGNSINF: 0,,,,,\r\nOK"])
def test_gnss_no_fix(run, answer):
    assert run("gnss", answer).strip() == "0 0 0.00000 0.00000"
