"""Host tier: notecard, the pure core that builds Notecard requests and reads answers."""

import json

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "notecard_core.h"

// argv: req <name> | hub_set <product> <out> <in> | transport <m>
//       | note <file> <body> <sync 0|1> | loc <mode> [lat lon] | small <name>
//       | str <json> <path> | num <json> <path> | err <json> | product <s>
//       | hub <json> <product> <out> <in> | is <json> <method>
int main(int argc, char **argv)
{
    static char buf[512];
    size_t n = 0;
    const char *c = argv[1];
    if (strcmp(c, "req") == 0) {
        n = nc_req(buf, sizeof buf, argv[2]);
    } else if (strcmp(c, "small") == 0) {
        n = nc_req(buf, 10, argv[2]);
    } else if (strcmp(c, "hub_set") == 0) {
        n = nc_req_hub_set(buf, sizeof buf, argv[2], atoi(argv[3]), atoi(argv[4]));
    } else if (strcmp(c, "transport") == 0) {
        n = nc_req_transport(buf, sizeof buf, argv[2]);
    } else if (strcmp(c, "note") == 0) {
        n = nc_req_note_add(buf, sizeof buf, argv[2], argv[3], atoi(argv[4]));
    } else if (strcmp(c, "loc") == 0) {
        double lat, lon;
        if (argc > 4) {
            lat = atof(argv[3]);
            lon = atof(argv[4]);
            n = nc_req_location_mode(buf, sizeof buf, argv[2], &lat, &lon);
        } else {
            n = nc_req_location_mode(buf, sizeof buf, argv[2], NULL, NULL);
        }
    } else if (strcmp(c, "str") == 0) {
        static char out[64];
        printf(nc_str(argv[2], argv[3], out, sizeof out) ? "%s" : "NONE", out);
        return 0;
    } else if (strcmp(c, "num") == 0) {
        double d;
        if (nc_num(argv[2], argv[3], &d)) {
            printf("%.6f", d);
        } else {
            printf("NONE");
        }
        return 0;
    } else if (strcmp(c, "err") == 0) {
        static char out[64];
        printf(nc_err(argv[2], out, sizeof out) ? "ERR %s" : "OK", out);
        return 0;
    } else if (strcmp(c, "product") == 0) {
        printf("%d", nc_product_valid(argv[2]));
        return 0;
    } else if (strcmp(c, "hub") == 0) {
        printf("%d", nc_hub_matches(argv[2], argv[3], atoi(argv[4]), atoi(argv[5])));
        return 0;
    } else if (strcmp(c, "is") == 0) {
        printf("%d", nc_transport_is(argv[2], argv[3]));
        return 0;
    }
    if (n == 0) {
        printf("INVALID");
    } else {
        printf("%zu %s", n, buf);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def nc(compile_harness):
    return compile_harness("notecard", HARNESS, ["notecard/notecard_core.c"], ["notecard/include"])


def request(nc, *args):
    out = nc(*args)
    assert out != "INVALID", args
    n, line = out.split(" ", 1)
    assert int(n) == len(line) and line.endswith("\n")
    return json.loads(line)


def test_simple_requests(nc):
    for name in ("card.version", "hub.get", "card.transport", "card.time", "card.location",
                 "card.wireless", "hub.sync"):
        assert request(nc, "req", name) == {"req": name}
    assert nc("req", 'card"x') == "INVALID"
    assert nc("req", "") == "INVALID"
    assert nc("small", "card.version") == "INVALID"   # does not fit in 10 bytes


def test_hub_set(nc):
    assert request(nc, "hub_set", "com.example.joe:weather", 60, 1440) == {
        "req": "hub.set", "product": "com.example.joe:weather", "mode": "periodic",
        "outbound": 60, "inbound": 1440}
    assert nc("hub_set", "bad product", 60, 1440) == "INVALID"
    assert nc("hub_set", "x" * 65, 60, 1440) == "INVALID"
    assert nc("hub_set", "p", 0, 1440) == "INVALID"


def test_transport_and_location(nc):
    assert request(nc, "transport", "cell") == {"req": "card.transport", "method": "cell"}
    assert request(nc, "loc", "continuous") == {"req": "card.location.mode", "mode": "continuous"}
    assert request(nc, "loc", "fixed", "40.712776", "-74.005974") == {
        "req": "card.location.mode", "mode": "fixed", "lat": 40.712776, "lon": -74.005974}
    assert nc("loc", "fixed", "91", "0") == "INVALID"


def test_note_add(nc):
    body = '{"t":[{"ts":1,"values":{"temp_f":70.25}}]}'
    assert request(nc, "note", "tb.qo", body, 0) == {
        "req": "note.add", "file": "tb.qo", "body": json.loads(body)}
    assert request(nc, "note", "alert.qo", '{"title":"x"}', 1)["sync"] is True
    assert nc("note", "tb.qo", "[1]", 0) == "INVALID"
    assert nc("note", 'tb"qo', "{}", 0) == "INVALID"


ANSWER = ('{"device":"dev:860322068012345","product":"com.example.joe:weather",'
          '"mode":"periodic","outbound":60,"inbound":1440,'
          '"net":{"rssi":-71,"bars":3,"band":"LTE BAND 2"},"note":"say \\"hi\\" \\\\ ok"}')


@pytest.mark.parametrize("path,want", [
    ("product", "com.example.joe:weather"),
    ("net.band", "LTE BAND 2"),
    ("note", 'say "hi" \\ ok'),
    ("outbound", "NONE"),          # a number, not a string
    ("missing", "NONE"),
    ("net.missing", "NONE"),
])
def test_str(nc, path, want):
    assert nc("str", ANSWER, path) == want


@pytest.mark.parametrize("path,want", [
    ("outbound", "60.000000"),
    ("net.bars", "3.000000"),
    ("net.rssi", "-71.000000"),
    ("product", "NONE"),
    ("net", "NONE"),
])
def test_num(nc, path, want):
    assert nc("num", ANSWER, path) == want


def test_answers_with_spaces_and_arrays(nc):
    a = '{ "list" : [1, {"a":"}"}, "]"] , "lat" : 40.712776 , "lon":-74.005974,"time":1730592000 }'
    assert nc("num", a, "lat") == "40.712776"
    assert nc("num", a, "time") == "1730592000.000000"
    assert nc("str", '{"a":', "a") == "NONE"         # cut short
    assert nc("str", "not json", "a") == "NONE"


def test_err(nc):
    assert nc("err", '{"err":"note-c: no answer {io}"}') == "ERR note-c: no answer {io}"
    assert nc("err", '{"total":1}') == "OK"


@pytest.mark.parametrize("s,want", [
    ("com.example.joe:weather", "1"),
    ("a", "1"),
    ("x" * 64, "1"),
    ("x" * 65, "0"),
    ("", "0"),
    ("has space", "0"),
    ("semi;colon", "0"),
])
def test_product_valid(nc, s, want):
    assert nc("product", s) == want


def test_hub_matches(nc):
    p = "com.example.joe:weather"
    assert nc("hub", ANSWER, p, 60, 1440) == "1"
    assert nc("hub", ANSWER, p, 30, 1440) == "0"
    assert nc("hub", ANSWER, "com.other:x", 60, 1440) == "0"
    assert nc("hub", ANSWER.replace('"periodic"', '"continuous"'), p, 60, 1440) == "0"
    assert nc("hub", '{"device":"dev:1"}', p, 60, 1440) == "0"   # a new Notecard
    assert nc("hub", '{"err":"x"}', p, 60, 1440) == "0"


def test_transport_is(nc):
    assert nc("is", '{"method":"cell"}', "cell") == "1"
    assert nc("is", '{"method":"wifi-cell"}', "cell") == "0"
    assert nc("is", "{}", "cell") == "0"
