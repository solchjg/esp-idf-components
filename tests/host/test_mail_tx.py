"""Host tier: mail_tx, the pure core (Base64, the message text, the SMTP dialogue)."""

import base64

import pytest

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "mail_core.h"

static void show(mc_smtp_t *s, mc_action_t a)
{
    size_t n = strlen(s->out);
    switch (a) {
    case MC_WAIT: puts("WAIT"); break;
    case MC_SEND: printf("SEND %.*s|\n", (int)(n >= 2 ? n - 2 : n), s->out); break;
    case MC_SEND_MSG: puts("MSG"); break;
    case MC_STARTTLS: puts("TLS"); break;
    case MC_DONE: printf("DONE %.*s|\n", (int)(n >= 2 ? n - 2 : n), s->out); break;
    case MC_FAIL: printf("FAIL %d %d\n", (int)s->err, s->code); break;
    }
}

int main(int argc, char **argv)
{
    if (strcmp(argv[1], "b64") == 0) {
        char out[64];
        size_t n = mc_b64(argv[2], strlen(argv[2]), out, (size_t)atoi(argv[3]));
        printf("%zu|%s", n, out);
    } else if (strcmp(argv[1], "msg") == 0) {
        static char out[4096];
        size_t n = mc_message(out, (size_t)atoi(argv[3]), "Fri, 30 Oct 2026 07:00:12 -0400",
                              "dev@example.com", "joe@example.com", "tds3-ea483c summary",
                              argv[2]);
        printf("%zu|", n);
        for (size_t i = 0; i < n; i++) {
            putchar(out[i] == '\r' ? '~' : out[i]);  // stdout is read as text: keep CR visible
        }
    } else {
        // smtp <starttls 0|1> <server line>...
        mc_smtp_t s;
        static const char msg[] = "Subject: x\r\n\r\nhi\r\n.\r\n";
        mc_init(&s, argv[2][0] == '1', "tds3-ea483c", "dev@example.com", "app pass 16chars",
                "dev@example.com", "joe@example.com", msg, sizeof msg - 1);
        for (int i = 3; i < argc; i++) {
            mc_action_t a = mc_line(&s, argv[i]);
            show(&s, a);
            if (a == MC_STARTTLS) {
                show(&s, mc_tls_up(&s));
            }
        }
    }
    return 0;
}
"""
HARNESS = HARNESS.replace("#include <string.h>", "#include <stdlib.h>\n#include <string.h>")

E_AUTH, E_REFUSED, E_PROTO = 1, 2, 3
PLAIN = base64.b64encode(b"\0dev@example.com\0app pass 16chars").decode()


@pytest.fixture(scope="module")
def core(compile_harness):
    run = compile_harness("mail_tx", HARNESS, ["mail_tx/mail_core.c"], ["mail_tx/include"])
    return lambda *a: run(*a).splitlines()


@pytest.mark.parametrize("text", ["", "f", "fo", "foo", "foob", "fooba", "foobar",
                                  "dev@example.com"])
def test_b64_matches_the_standard(core, text):
    n, out = core("b64", text, 64)[0].split("|")
    assert out == base64.b64encode(text.encode()).decode()
    assert int(n) == len(out)


def test_b64_refuses_what_does_not_fit(core):
    assert core("b64", "foobar", 8) == ["0|"], "8 bytes + terminator needs 9"
    assert core("b64", "foobar", 9) == ["8|Zm9vYmFy"]


@pytest.fixture(scope="module")
def message(compile_harness):
    run = compile_harness("mail_msg", HARNESS, ["mail_tx/mail_core.c"], ["mail_tx/include"])

    def raw(body, cap=4096):
        n, text = run("msg", body, cap).split("|", 1)
        return int(n), text.replace("~", "\r")
    return raw


def test_message_headers_and_dot_stuffing(message):
    _, text = message("Period 07:00 - 13:00\n.leading dot\nlast")
    assert text.startswith("Date: Fri, 30 Oct 2026 07:00:12 -0400\r\n")
    assert "\r\nFrom: <dev@example.com>\r\nTo: <joe@example.com>\r\n" in text
    assert "\r\nSubject: tds3-ea483c summary\r\n" in text
    assert "\r\nContent-Type: text/plain; charset=utf-8\r\n" in text
    assert "\r\n\r\nPeriod 07:00 - 13:00\r\n..leading dot\r\nlast\r\n.\r\n" in text


def test_message_is_crlf_terminated_by_a_lone_dot(message):
    for body in ("a\nb", "a\nb\n", "a\r\nb\r\n", ""):
        n, text = message(body)
        assert n == len(text.encode())
        assert text.endswith("\r\n.\r\n")
        assert "\n" not in text.replace("\r\n", ""), "a bare LF left in " + repr(body)
    assert message("a\nb")[1].endswith("\r\n\r\na\r\nb\r\n.\r\n")


def test_message_refuses_what_does_not_fit(message):
    n, _ = message("x" * 50)
    assert message("x" * 50, n)[0] == 0, "no room for the terminator"
    assert message("x" * 50, n + 1)[0] == n


EHLO_465 = ["250-smtp.example.com", "250-SIZE 35882577", "250-AUTH LOGIN PLAIN XOAUTH2",
            "250 SMTPUTF8"]


def test_465_plain_auth_full_dialogue(core):
    out = core("smtp", 0, "220 smtp.example.com ESMTP", *EHLO_465, "235 2.7.0 Accepted",
               "250 2.1.0 OK", "250 2.1.5 OK", "354 Go ahead", "250 2.0.0 OK queued")
    assert out == ["SEND EHLO tds3-ea483c|", "WAIT", "WAIT", "WAIT",
                   f"SEND AUTH PLAIN {PLAIN}|",
                   "SEND MAIL FROM:<dev@example.com>|", "SEND RCPT TO:<joe@example.com>|",
                   "SEND DATA|", "MSG", "DONE QUIT|"]


def test_587_starttls_then_login_auth(core):
    user = base64.b64encode(b"dev@example.com").decode()
    pw = base64.b64encode(b"app pass 16chars").decode()
    out = core("smtp", 1, "220 mail.own.example ESMTP", "250-mail.own.example",
               "250-AUTH PLAIN LOGIN", "250 STARTTLS", "220 2.0.0 Ready to start TLS",
               "250-mail.own.example", "250 AUTH=LOGIN", "334 VXNlcm5hbWU6", "334 UGFzc3dvcmQ6",
               "235 OK", "250 OK", "251 forwarded", "354 go", "250 OK")
    assert out == ["SEND EHLO tds3-ea483c|", "WAIT", "WAIT", "SEND STARTTLS|", "TLS",
                   "SEND EHLO tds3-ea483c|", "WAIT", "SEND AUTH LOGIN|", f"SEND {user}|",
                   f"SEND {pw}|", "SEND MAIL FROM:<dev@example.com>|",
                   "SEND RCPT TO:<joe@example.com>|", "SEND DATA|", "MSG", "DONE QUIT|"], \
        "the AUTH offered before STARTTLS must be forgotten (only LOGIN after it)"


def test_587_without_starttls_offered_fails(core):
    out = core("smtp", 1, "220 hi", "250-hi", "250 AUTH PLAIN")
    assert out[-1] == f"FAIL {E_PROTO} 0", "never log in over plain text"


def test_refused_login(core):
    out = core("smtp", 0, "220 hi", "250 AUTH PLAIN", "535 5.7.8 Bad credentials")
    assert out[-1] == f"FAIL {E_AUTH} 535"


def test_no_usable_auth_method(core):
    assert core("smtp", 0, "220 hi", "250 AUTH XOAUTH2")[-1] == f"FAIL {E_AUTH} 0"


@pytest.mark.parametrize("step,reply", [(0, "554 no service"), (2, "550 sender refused"),
                                        (3, "550 no such user"), (4, "451 later"),
                                        (5, "552 too big")])
def test_refusals_at_each_step(core, step, reply):
    ok = ["220 hi", "250 AUTH PLAIN", "235 OK", "250 OK", "250 OK", "354 go", "250 OK"]
    lines = ok[:step + (1 if step else 0)] + [reply] if step else [reply]
    out = core("smtp", 0, *lines)
    assert out[-1] == f"FAIL {E_REFUSED} {reply[:3]}"
    assert "DONE QUIT|" not in out


def test_not_a_reply_line(core):
    assert core("smtp", 0, "HTTP/1.1 400 Bad Request")[-1] == f"FAIL {E_PROTO} 0"
    assert core("smtp", 0, "22")[-1] == f"FAIL {E_PROTO} 0"


def test_success_only_on_the_final_250(core):
    out = core("smtp", 0, "220 hi", "250 AUTH PLAIN", "235 OK", "250 OK", "250 OK", "354 go")
    assert out[-1] == "MSG" and "DONE QUIT|" not in out
