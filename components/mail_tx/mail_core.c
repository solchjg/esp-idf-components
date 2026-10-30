#include "mail_core.h"

#include <stdio.h>
#include <string.h>

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t mc_b64(const void *in, size_t len, char *out, size_t cap)
{
    const unsigned char *p = in;
    size_t need = (len + 2) / 3 * 4;
    if (need + 1 > cap) {
        if (cap) {
            out[0] = '\0';
        }
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = p[i] << 16 | (i + 1 < len ? p[i + 1] << 8 : 0) | (i + 2 < len ? p[i + 2] : 0);
        out[n++] = B64[v >> 18 & 63];
        out[n++] = B64[v >> 12 & 63];
        out[n++] = i + 1 < len ? B64[v >> 6 & 63] : '=';
        out[n++] = i + 2 < len ? B64[v & 63] : '=';
    }
    out[n] = '\0';
    return n;
}

// Appends [s, s + len) to out at *n; false once it no longer fits (with room
// for the terminator).
static bool put(char *out, size_t cap, size_t *n, const char *s, size_t len)
{
    if (*n + len + 1 > cap) {
        return false;
    }
    memcpy(out + *n, s, len);
    *n += len;
    out[*n] = '\0';
    return true;
}

static bool puts_(char *out, size_t cap, size_t *n, const char *s)
{
    return put(out, cap, n, s, strlen(s));
}

size_t mc_message(char *out, size_t cap, const char *date, const char *from, const char *to,
                  const char *subject, const char *body)
{
    size_t n = 0;
    bool ok = puts_(out, cap, &n, "Date: ") && puts_(out, cap, &n, date) &&
              puts_(out, cap, &n, "\r\nFrom: <") && puts_(out, cap, &n, from) &&
              puts_(out, cap, &n, ">\r\nTo: <") && puts_(out, cap, &n, to) &&
              puts_(out, cap, &n, ">\r\nSubject: ") && puts_(out, cap, &n, subject) &&
              puts_(out, cap, &n, "\r\nMIME-Version: 1.0\r\n"
                                  "Content-Type: text/plain; charset=utf-8\r\n"
                                  "Content-Transfer-Encoding: 8bit\r\n\r\n");
    bool line_start = true;
    for (const char *p = body; ok && *p; p++) {
        if (line_start && *p == '.') {
            ok = put(out, cap, &n, ".", 1);  // RFC 5321 §4.5.2 transparency
        }
        if (*p == '\r') {
            continue;  // every line end becomes CRLF below
        }
        ok = ok && (*p == '\n' ? put(out, cap, &n, "\r\n", 2) : put(out, cap, &n, p, 1));
        line_start = *p == '\n';
    }
    ok = ok && puts_(out, cap, &n, line_start && n > 0 && out[n - 1] == '\n' ? ".\r\n"
                                                                             : "\r\n.\r\n");
    if (!ok) {
        if (cap) {
            out[0] = '\0';
        }
        return 0;
    }
    return n;
}

enum {
    S_GREET, S_EHLO1, S_STLS, S_TLS, S_EHLO2, S_AUTH_PLAIN, S_AUTH_L1, S_AUTH_L2, S_AUTH_L3,
    S_MAIL, S_RCPT, S_DATA, S_SENT, S_END,
};

void mc_init(mc_smtp_t *s, bool starttls, const char *helo, const char *user,
             const char *password, const char *from, const char *to, const char *msg,
             size_t msg_len)
{
    memset(s, 0, sizeof *s);
    s->starttls = starttls;
    s->helo = helo;
    s->user = user;
    s->password = password;
    s->from = from;
    s->to = to;
    s->msg = msg;
    s->msg_len = msg_len;
}

static mc_action_t fail(mc_smtp_t *s, mc_err_t err, int code)
{
    s->state = S_END;
    s->err = err;
    s->code = code;
    return MC_FAIL;
}

static mc_action_t cmd(mc_smtp_t *s, int next, const char *fmt, const char *arg)
{
    s->state = next;
    snprintf(s->out, sizeof s->out, fmt, arg);
    return MC_SEND;
}

static bool word_at(const char *p, const char *w)
{
    size_t n = strlen(w);
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != w[i]) {
            return false;
        }
    }
    return p[n] == '\0' || p[n] == ' ' || p[n] == '=';
}

// One EHLO keyword line (after the code and its separator).
static void ehlo_keyword(mc_smtp_t *s, const char *kw)
{
    if (word_at(kw, "STARTTLS")) {
        s->tls_offered = true;
    } else if (word_at(kw, "AUTH")) {
        for (const char *p = kw + 4; *p;) {
            while (*p == ' ' || *p == '=') {
                p++;
            }
            if (word_at(p, "PLAIN")) {
                s->plain = true;
            } else if (word_at(p, "LOGIN")) {
                s->login = true;
            }
            while (*p && *p != ' ') {
                p++;
            }
        }
    }
}

static mc_action_t auth(mc_smtp_t *s)
{
    if (s->plain) {
        // RFC 4616: authzid NUL authcid NUL passwd, in one Base64 argument.
        char raw[2 * MC_ADDR_MAX + 3], b64[sizeof s->out - 16];
        size_t ul = strlen(s->user), pl = strlen(s->password);
        if (ul + pl + 2 > sizeof raw) {
            return fail(s, MC_E_AUTH, 0);
        }
        raw[0] = '\0';
        memcpy(raw + 1, s->user, ul);
        raw[1 + ul] = '\0';
        memcpy(raw + 2 + ul, s->password, pl);
        if (mc_b64(raw, ul + pl + 2, b64, sizeof b64) == 0) {
            return fail(s, MC_E_AUTH, 0);
        }
        memset(raw, 0, sizeof raw);
        return cmd(s, S_AUTH_PLAIN, "AUTH PLAIN %s\r\n", b64);
    }
    if (s->login) {
        return cmd(s, S_AUTH_L1, "%s", "AUTH LOGIN\r\n");
    }
    return fail(s, MC_E_AUTH, 0);
}

static mc_action_t send_b64(mc_smtp_t *s, int next, const char *text)
{
    char b64[sizeof s->out - 2];
    if (mc_b64(text, strlen(text), b64, sizeof b64) == 0) {
        return fail(s, MC_E_AUTH, 0);
    }
    return cmd(s, next, "%s\r\n", b64);
}

mc_action_t mc_line(mc_smtp_t *s, const char *line)
{
    if (s->state == S_END || s->state == S_TLS) {
        return fail(s, MC_E_PROTO, 0);
    }
    if (line[0] < '2' || line[0] > '5' || line[1] < '0' || line[1] > '9' || line[2] < '0' ||
        line[2] > '9' || (line[3] != '\0' && line[3] != ' ' && line[3] != '-')) {
        return fail(s, MC_E_PROTO, 0);
    }
    int code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
    bool more = line[3] == '-';
    if ((s->state == S_EHLO1 || s->state == S_EHLO2) && code == 250 && line[3] != '\0') {
        ehlo_keyword(s, line + 4);
    }
    if (more) {
        return MC_WAIT;
    }
    switch (s->state) {
    case S_GREET:
        return code == 220 ? cmd(s, S_EHLO1, "EHLO %s\r\n", s->helo) : fail(s, MC_E_REFUSED, code);
    case S_EHLO1:
        if (code != 250) {
            return fail(s, MC_E_REFUSED, code);
        }
        if (s->starttls) {
            return s->tls_offered ? cmd(s, S_STLS, "%s", "STARTTLS\r\n") : fail(s, MC_E_PROTO, 0);
        }
        return auth(s);
    case S_STLS:
        if (code != 220) {
            return fail(s, MC_E_REFUSED, code);
        }
        s->state = S_TLS;
        return MC_STARTTLS;
    case S_EHLO2:
        return code == 250 ? auth(s) : fail(s, MC_E_REFUSED, code);
    case S_AUTH_L1:
        return code == 334 ? send_b64(s, S_AUTH_L2, s->user) : fail(s, MC_E_AUTH, code);
    case S_AUTH_L2:
        return code == 334 ? send_b64(s, S_AUTH_L3, s->password) : fail(s, MC_E_AUTH, code);
    case S_AUTH_PLAIN:
    case S_AUTH_L3:
        return code == 235 ? cmd(s, S_MAIL, "MAIL FROM:<%s>\r\n", s->from)
                           : fail(s, MC_E_AUTH, code);
    case S_MAIL:
        return code == 250 ? cmd(s, S_RCPT, "RCPT TO:<%s>\r\n", s->to) : fail(s, MC_E_REFUSED, code);
    case S_RCPT:
        return code == 250 || code == 251 ? cmd(s, S_DATA, "%s", "DATA\r\n")
                                          : fail(s, MC_E_REFUSED, code);
    case S_DATA:
        if (code != 354) {
            return fail(s, MC_E_REFUSED, code);
        }
        s->state = S_SENT;
        return MC_SEND_MSG;
    case S_SENT:
        if (code != 250) {
            return fail(s, MC_E_REFUSED, code);
        }
        snprintf(s->out, sizeof s->out, "QUIT\r\n");
        s->state = S_END;
        return MC_DONE;
    default:
        return fail(s, MC_E_PROTO, 0);
    }
}

mc_action_t mc_tls_up(mc_smtp_t *s)
{
    if (s->state != S_TLS) {
        return fail(s, MC_E_PROTO, 0);
    }
    s->plain = s->login = s->tls_offered = false;  // RFC 3207 §4.2: forget the first EHLO
    return cmd(s, S_EHLO2, "EHLO %s\r\n", s->helo);
}
