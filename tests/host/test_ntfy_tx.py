"""Host tier: ntfy_tx, the pure core of one ntfy publish (checks, request, status)."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ntfy_tx_core.h"

// argv: url <u> | topic <t> | token <t> | status <n>
//       | build <url> <topic> <token|-> <title|-> <priority> <tags|-> <click|-> <body>
int main(int argc, char **argv)
{
    if (strcmp(argv[1], "url") == 0) {
        printf("%d %d", nt_url_valid(argv[2]), nt_url_https(argv[2]));
    } else if (strcmp(argv[1], "topic") == 0) {
        printf("%d", nt_topic_valid(argv[2]));
    } else if (strcmp(argv[1], "token") == 0) {
        printf("%d", nt_token_valid(argv[2]));
    } else if (strcmp(argv[1], "status") == 0) {
        printf("%s", nt_reason_name(nt_status(atoi(argv[2]))));
    } else if (strcmp(argv[1], "name") == 0) {
        printf("%s", nt_reason_name((nt_reason_t)atoi(argv[2])));
    } else {
#define ARG(i) (strcmp(argv[i], "-") == 0 ? NULL : argv[i])
        nt_msg_t m = {.title = ARG(5), .priority = atoi(argv[6]), .tags = ARG(7),
                      .click = ARG(8), .body = argv[9]};
        static nt_req_t r;
        if (!nt_build(argv[2], argv[3], ARG(4), &m, &r)) {
            printf("INVALID\n");
            return 0;
        }
        size_t size = strlen(r.endpoint) + r.body_len;
        printf("POST %s\n", r.endpoint);
        for (int i = 0; i < r.headers; i++) {
            printf("%s: %s\n", r.header[i].name, r.header[i].value);
            size += strlen(r.header[i].name) + strlen(r.header[i].value) + 4;
        }
        printf("BODY %.*s\nSIZE %zu\n", (int)r.body_len, r.body, size);
    }
    return 0;
}
"""

URL = "http://192.168.2.37:8080"


@pytest.fixture(scope="module")
def nt(compile_harness):
    return compile_harness("ntfy_tx", HARNESS, ["ntfy_tx/ntfy_tx_core.c"], ["ntfy_tx/include"])


@pytest.mark.parametrize("url,want", [
    (URL, "1 0"),
    ("https://ntfy.sh", "1 1"),
    ("https://ntfy.example.com:443", "1 1"),
    ("http://a:1", "1 0"),
    ("http://a", "0 0"),                      # 8 bytes: shorter than 10
    ("ftp://ntfy.example.com", "0 0"),
    ("http://ntfy.example.com/", "0 0"),      # trailing slash
    ("http://ntfy.example.com/x", "0 0"),     # a path
    ("http://ntfy.example.com?x", "0 0"),     # a query
    ("http://host:0", "0 0"),
    ("http://host:65536", "0 0"),
    ("http://host:", "0 0"),
    ("http://ho st.com", "0 0"),
    ("http://:8080abc", "0 0"),
    ("http://" + "a" * 89, "1 0"),            # 96 bytes
    ("http://" + "a" * 90, "0 0"),            # 97 bytes
    ("", "0 0"),
])
def test_url(nt, url, want):
    assert nt("url", url) == want


@pytest.mark.parametrize("topic,ok", [
    ("tds3-alerts", 1), ("A_b-9", 1), ("a" * 64, 1), ("a" * 65, 0), ("", 0),
    ("a b", 0), ("a/b", 0), ("a.b", 0),
])
def test_topic(nt, topic, ok):
    assert nt("topic", topic) == str(ok)


@pytest.mark.parametrize("token,ok", [
    ("tk_abc123", 1), ("x" * 64, 1), ("x" * 65, 0), ("", 0), ("tk abc", 0), ("tk\x01", 0),
])
def test_token(nt, token, ok):
    assert nt("token", token) == str(ok)


@pytest.mark.parametrize("status,want", [
    (200, "ok"), (201, "ok"), (299, "ok"), (301, "refused"), (400, "refused"), (401, "auth"),
    (403, "auth"), (404, "refused"), (413, "refused"), (429, "limit"), (500, "server"),
    (503, "server"), (0, "refused"), (-1, "refused"),
])
def test_status(nt, status, want):
    assert nt("status", status) == want


def test_reason_names(nt):
    names = [nt("name", i) for i in range(10)]
    assert names == ["ok", "dns", "connect", "tls", "cert", "auth", "refused", "limit",
                     "server", "timeout"]


def test_build_with_token(nt):
    out = nt("build", URL, "tds3-alerts", "tk_secret", "tds3-a1b2c3 temperature high", 4,
             "warning", "http://tds3-a1b2c3.local/", "Temp 91.2 F, limit 90.0 F, 2026-11-01 14:05")
    assert out.splitlines() == [
        "POST http://192.168.2.37:8080/tds3-alerts",
        "Title: tds3-a1b2c3 temperature high",
        "Priority: 4",
        "Tags: warning",
        "Click: http://tds3-a1b2c3.local/",
        "Authorization: Bearer tk_secret",
        "BODY Temp 91.2 F, limit 90.0 F, 2026-11-01 14:05",
        out.splitlines()[-1],
    ]


def test_build_without_token_or_optional_fields(nt):
    out = nt("build", URL, "t", "-", "-", 0, "-", "-", "just a body")
    assert out.splitlines()[:2] == ["POST http://192.168.2.37:8080/t", "BODY just a body"]
    assert "Authorization" not in out and "tk_" not in out


def test_token_never_in_title_or_body(nt):
    out = nt("build", URL, "t", "tk_marker", "title", 3, "-", "-", "body")
    lines = [ln for ln in out.splitlines() if "tk_marker" in ln]
    assert lines == ["Authorization: Bearer tk_marker"]


@pytest.mark.parametrize("args", [
    ("ftp://x.example", "t", "-"),
    (URL, "bad topic", "-"),
    (URL, "t", "bad token"),
])
def test_build_refuses_invalid(nt, args):
    assert nt("build", *args, "-", 0, "-", "-", "b") == "INVALID\n"


def test_header_values_are_one_line_and_cut(nt):
    out = nt("build", URL, "t", "-", "line1\r\nInjected: yes\xc3\xa9" + "x" * 200, 9, "-", "-", "b")
    title = [ln for ln in out.splitlines() if ln.startswith("Title: ")]
    assert len(title) == 1 and "Injected" in title[0]
    assert "\r" not in title[0] and len(title[0]) == len("Title: ") + 96
    assert "Priority" not in out  # 9 is out of range: left out


def test_longest_request_stays_small(nt):
    topic = "a" * 64
    out = nt("build", "http://" + "h" * 89, topic, "x" * 64, "T" * 200, 5, "g" * 100,
             "http://" + "c" * 200, "B" * 2000)
    size = int(out.splitlines()[-1].split()[1])
    assert size < 1024
    assert out.splitlines()[-2] == "BODY " + "B" * 384
