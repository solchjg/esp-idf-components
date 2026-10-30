"""Host tier: mail_tx, the pure SMTP client core against scripted server replies."""

import base64

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mail_tx_core.h"

static mt_msg_t msg = {
    .helo = "tds3-a1b2c3", .user = "me@example.com", .password = "s3cret pw",
    .from = "me@example.com", .to = "you@example.org", .subject = "tds3 summary",
    .date = "Fri, 30 Oct 2026 07:00:00 -0400", .body = "line one\n.dot\nlast",
};

static char out[4096];

static void show(const char *what, size_t n)
{
    printf("%s ", what);  // CR and LF shown escaped, one sent chunk per line
    for (size_t i = 0; i < n; i++) {
        if (out[i] == '\r') fputs("\\r", stdout);
        else if (out[i] == '\n') fputs("\\n", stdout);
        else putchar(out[i]);
    }
    putchar('\n');
}

// argv: session <starttls 0|1> <chunk> <reply>... | message <body> | b64 <text>
int main(int argc, char **argv)
{
    if (strcmp(argv[1], "b64") == 0) {
        char enc[256];
        size_t n = mt_base64((const unsigned char *)argv[2], strlen(argv[2]), enc, sizeof enc);
        printf("%zu %s", n, enc);
        return 0;
    }
    if (strcmp(argv[1], "message") == 0) {
        msg.body = argv[2];
        show("M:", mt_message(&msg, out, sizeof out));
        return 0;
    }
    msg.starttls = atoi(argv[2]) != 0;
    if (strcmp(argv[4], "-") != 0) msg.password = argv[4];  // a password override
    if (getenv("MT_TO")) msg.to = getenv("MT_TO");
    size_t chunk = (size_t)atoi(argv[3]);
    mt_t s;
    mt_begin(&s, &msg);
    for (int a = 5; a < argc; a++) {
        const char *r = argv[a];
        size_t len = strlen(r), off = 0;
        while (off < len) {
            size_t n = len - off < chunk ? len - off : chunk, used, out_len;
            mt_act_t act = mt_feed(&s, r + off, n, &used, out, sizeof out, &out_len);
            off += used;
            switch (act) {
            case MT_NEED_MORE: break;
            case MT_SEND: show("C:", out_len); break;
            case MT_START_TLS:
                printf("TLS\n");
                act = mt_tls_ready(&s, out, sizeof out, &out_len);
                if (act == MT_SEND) show("C:", out_len);
                break;
            case MT_DONE: show("DONE", out_len); return 0;
            case MT_FAILED: printf("FAIL %s\n", mt_reason_name(s.reason)); return 0;
            }
        }
    }
    printf("WAIT\n");
    return 0;
}
"""

USER, PASSWORD = "me@example.com", "s3cret pw"
PLAIN = base64.b64encode(f"\0{USER}\0{PASSWORD}".encode()).decode()

GREETING = "220 smtp.example.com ESMTP ready\r\n"
EHLO_TLS = ("250-smtp.example.com at your service\r\n250-SIZE 35882577\r\n"
            "250-AUTH LOGIN PLAIN XOAUTH2\r\n250 SMTPUTF8\r\n")
EHLO_LOGIN_ONLY = "250-smtp.example.com\r\n250-AUTH=LOGIN\r\n250 8BITMIME\r\n"
EHLO_PLAIN_587 = "250-smtp.example.com\r\n250-STARTTLS\r\n250 8BITMIME\r\n"
OK = "250 2.1.0 OK\r\n"


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("mail_tx", HARNESS, ["mail_tx/mail_tx_core.c"], ["mail_tx/include"])


def session(core, starttls, *replies, chunk=4096, password="-"):
    out = core("session", int(starttls), chunk, password, *replies)
    return out.splitlines()


def message(core, body):
    out = core("message", body)
    assert out.startswith("M: ") and out.endswith("\n")
    return out[3:-1].replace("\\r", "\r").replace("\\n", "\n")


def happy_replies(ehlo):
    return [GREETING, ehlo, "235 2.7.0 Accepted\r\n", OK, OK,
            "354 Go ahead\r\n", "250 2.0.0 OK queued\r\n"]


# TC-MAIL-01 · FR-25.5 (core): port 465, TLS from the start, AUTH PLAIN offered
def test_implicit_tls_with_auth_plain_runs_the_rfc_5321_sequence(core):
    lines = session(core, False, *happy_replies(EHLO_TLS))
    cmds = [ln for ln in lines if ln.startswith("C:")]
    assert cmds[:5] == ["C: EHLO tds3-a1b2c3\\r\\n", f"C: AUTH PLAIN {PLAIN}\\r\\n",
                        "C: MAIL FROM:<me@example.com>\\r\\n", "C: RCPT TO:<you@example.org>\\r\\n",
                        "C: DATA\\r\\n"]
    assert lines[-1] == "DONE QUIT\\r\\n"
    assert "TLS" not in lines


# TC-MAIL-02 · FR-25.5 (core): port 587, STARTTLS before any login, EHLO again over TLS
def test_starttls_upgrades_before_login_and_says_ehlo_again(core):
    lines = session(core, True, GREETING, EHLO_PLAIN_587, "220 2.0.0 Ready to start TLS\r\n",
                    *happy_replies(EHLO_TLS)[1:])
    assert lines[:4] == ["C: EHLO tds3-a1b2c3\\r\\n", "C: STARTTLS\\r\\n", "TLS",
                         "C: EHLO tds3-a1b2c3\\r\\n"]
    assert lines[4] == f"C: AUTH PLAIN {PLAIN}\\r\\n"
    assert lines[-1] == "DONE QUIT\\r\\n"


def test_starttls_missing_fails_as_tls_without_a_plain_login(core):
    lines = session(core, True, GREETING, "250-smtp.example.com\r\n250 AUTH PLAIN LOGIN\r\n")
    assert lines == ["C: EHLO tds3-a1b2c3\\r\\n", "FAIL tls"]


# TC-MAIL-03 · FR-25.5 (core): AUTH LOGIN when only LOGIN is offered (old AUTH= form)
def test_auth_login_sends_user_and_password_in_base64(core):
    b64 = lambda s: base64.b64encode(s.encode()).decode()  # noqa: E731
    lines = session(core, False, GREETING, EHLO_LOGIN_ONLY, "334 VXNlcm5hbWU6\r\n",
                    "334 UGFzc3dvcmQ6\r\n", "235 ok\r\n", OK, OK, "354 go\r\n", "250 ok\r\n")
    assert lines[1:4] == ["C: AUTH LOGIN\\r\\n", f"C: {b64(USER)}\\r\\n", f"C: {b64(PASSWORD)}\\r\\n"]
    assert lines[-1] == "DONE QUIT\\r\\n"


def test_no_known_auth_mechanism_fails_as_auth(core):
    lines = session(core, False, GREETING, "250-x\r\n250 AUTH XOAUTH2 CRAM-MD5\r\n")
    assert lines[-1] == "FAIL auth"


# TC-MAIL-04 · FR-25.5, FR-25.7 (core): a refused login is reported, never success
@pytest.mark.parametrize("reply", ["535 5.7.8 Username and Password not accepted\r\n",
                                   "454 4.7.0 Temporary failure\r\n"])
def test_refused_login_fails_as_auth(core, reply):
    lines = session(core, False, GREETING, EHLO_TLS, reply)
    assert lines[-1] == "FAIL auth"
    assert not any(ln.startswith("C: MAIL") for ln in lines)


@pytest.mark.parametrize("step", [0, 3, 4, 5, 6])
def test_any_other_unexpected_reply_fails_as_refused(core, step):
    replies = happy_replies(EHLO_TLS)
    replies[step] = "554 5.7.1 No\r\n"
    lines = session(core, False, *replies)
    assert lines[-1] == "FAIL refused"
    assert not any(ln.startswith("DONE") for ln in lines)


def test_nothing_is_sent_before_the_greeting(core):
    assert session(core, False) == ["WAIT"]
    assert session(core, False, "220 almost") == ["WAIT"]  # line not ended yet


def test_garbage_from_the_server_fails_as_refused(core):
    assert session(core, False, "HTTP/1.1 400 Bad Request\r\n") == ["FAIL refused"]


# TC-MAIL-05 · FR-25.5 (core): replies split across reads give the same dialogue
@pytest.mark.parametrize("chunk", [1, 2, 7])
def test_split_replies_give_the_same_dialogue(core, chunk):
    whole = session(core, True, GREETING, EHLO_PLAIN_587, "220 go\r\n",
                    *happy_replies(EHLO_TLS)[1:])
    split = session(core, True, GREETING, EHLO_PLAIN_587, "220 go\r\n",
                    *happy_replies(EHLO_TLS)[1:], chunk=chunk)
    assert split == whole


def test_a_line_longer_than_the_buffer_is_still_read(core):
    lines = session(core, False, "220 " + "x" * 400 + "\r\n", EHLO_TLS)
    assert lines[1] == f"C: AUTH PLAIN {PLAIN}\\r\\n"


# TC-MAIL-06 · FR-25.5, FR-25.10 (core): the message — headers, dot-stuffing, end
def test_message_has_headers_crlf_and_doubled_leading_dots(core):
    msg = message(core, "first\n.second\r\n..third\nlast")
    head, body = msg.split("\r\n\r\n", 1)
    assert head.split("\r\n") == [
        "Date: Fri, 30 Oct 2026 07:00:00 -0400", "From: me@example.com",
        "To: you@example.org", "Subject: tds3 summary", "MIME-Version: 1.0",
        "Content-Type: text/plain; charset=utf-8", "Content-Transfer-Encoding: 8bit"]
    assert body == "first\r\n..second\r\n...third\r\nlast\r\n.\r\n"


def test_message_body_ending_in_a_newline_gets_one_crlf(core):
    assert message(core, "a\n").endswith("\r\n\r\na\r\n.\r\n")


def test_the_password_is_never_in_the_message(core):
    lines = session(core, False, *happy_replies(EHLO_TLS))
    data = next(i for i, ln in enumerate(lines) if ln == "C: DATA\\r\\n")
    assert PASSWORD not in "\n".join(lines[data + 1:])


def test_crlf_in_an_address_cannot_inject_a_command(core, monkeypatch):
    monkeypatch.setenv("MT_TO", "you@example.org>\r\nRCPT TO:<other@example.net")
    lines = session(core, False, GREETING, EHLO_TLS, "235 ok\r\n", OK)
    assert lines[-2] == "C: RCPT TO:<you@example.org>  RCPT TO:<other@example.net>\\r\\n"


@pytest.mark.parametrize("text,enc", [("", ""), ("f", "Zg=="), ("fo", "Zm8="),
                                      ("foo", "Zm9v"), ("foobar", "Zm9vYmFy")])
def test_base64_matches_rfc_4648(core, text, enc):
    assert core("b64", text) == f"{len(enc)} {enc}"


def test_two_replies_in_one_read_are_taken_one_at_a_time(core):
    one_by_one = session(core, False, *happy_replies(EHLO_TLS))
    together = session(core, False, "".join(happy_replies(EHLO_TLS)))
    assert together == one_by_one
