#include "mail_tx_core.h"

#include <string.h>

enum {
    S_GREETING,  // 220 from the server
    S_EHLO,      // 250, with the extensions
    S_STARTTLS,  // 220: go ahead with TLS
    S_TLS_WAIT,  // the handshake runs outside the core
    S_AUTH_USER, // 334 after AUTH LOGIN
    S_AUTH_PASS, // 334 after the user name
    S_AUTH_DONE, // 235
    S_MAIL,      // 250 after MAIL FROM
    S_RCPT,      // 250 or 251 after RCPT TO
    S_DATA,      // 354
    S_SENT,      // 250 after the message: accepted
    S_END,
};

const char *mt_reason_name(mt_reason_t r)
{
    static const char *const names[] = {"ok", "dns", "connect", "tls", "cert",
                                        "auth", "refused", "timeout"};
    return (unsigned)r < sizeof names / sizeof names[0] ? names[r] : "refused";
}

// ── Output buffer ────────────────────────────────────────────────────────

typedef struct {
    char *p;
    size_t cap, n;
    bool over;
} buf_t;

static void put_n(buf_t *b, const char *s, size_t len)
{
    if (b->over || b->n + len >= b->cap) {
        b->over = true;
        return;
    }
    memcpy(b->p + b->n, s, len);
    b->n += len;
    b->p[b->n] = '\0';
}

static void put(buf_t *b, const char *s)
{
    put_n(b, s, strlen(s));
}

// A header value or an address: CR and LF become spaces, so nothing the user
// typed can start a new command or header.
static void put_clean(buf_t *b, const char *s)
{
    for (; s && *s; s++) {
        char c = (*s == '\r' || *s == '\n') ? ' ' : *s;
        put_n(b, &c, 1);
    }
}

size_t mt_base64(const unsigned char *in, size_t len, char *out, size_t cap)
{
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = (len + 2) / 3 * 4;
    if (need + 1 > cap) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        if (i + 1 < len) {
            v |= (unsigned)in[i + 1] << 8;
        }
        if (i + 2 < len) {
            v |= in[i + 2];
        }
        out[n++] = tab[(v >> 18) & 63];
        out[n++] = tab[(v >> 12) & 63];
        out[n++] = i + 1 < len ? tab[(v >> 6) & 63] : '=';
        out[n++] = i + 2 < len ? tab[v & 63] : '=';
    }
    out[n] = '\0';
    return n;
}

size_t mt_message(const mt_msg_t *m, char *out, size_t cap)
{
    buf_t b = {out, cap, 0, false};
    if (m->date) {
        put(&b, "Date: ");
        put_clean(&b, m->date);
        put(&b, "\r\n");
    }
    put(&b, "From: ");
    put_clean(&b, m->from);
    put(&b, "\r\nTo: ");
    put_clean(&b, m->to);
    put(&b, "\r\nSubject: ");
    put_clean(&b, m->subject);
    put(&b, "\r\nMIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Transfer-Encoding: 8bit\r\n\r\n");
    bool line_start = true;
    for (const char *p = m->body; p && *p; p++) {
        if (*p == '\r') {
            continue;  // CRLF and LF alike end a line
        }
        if (*p == '\n') {
            put(&b, "\r\n");
            line_start = true;
            continue;
        }
        if (line_start && *p == '.') {
            put(&b, ".");  // RFC 5321 4.5.2: a leading dot is doubled
        }
        put_n(&b, p, 1);
        line_start = false;
    }
    if (!line_start) {
        put(&b, "\r\n");
    }
    put(&b, ".\r\n");
    return b.over ? 0 : b.n;
}

// ── Session ──────────────────────────────────────────────────────────────

void mt_begin(mt_t *s, const mt_msg_t *msg)
{
    memset(s, 0, sizeof *s);
    s->msg = msg;
    s->state = S_GREETING;
}

static mt_act_t fail(mt_t *s, mt_reason_t r)
{
    s->reason = r;
    s->state = S_END;
    return MT_FAILED;
}

static bool word_is(const char *w, size_t len, const char *kw)
{
    if (strlen(kw) != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = w[i] >= 'a' && w[i] <= 'z' ? (char)(w[i] - 32) : w[i];
        if (c != kw[i]) {
            return false;
        }
    }
    return true;
}

// One line of an EHLO reply after the code: an extension keyword and its
// parameters, e.g. "AUTH LOGIN PLAIN XOAUTH2" or the old "AUTH=LOGIN PLAIN".
static void ehlo_line(mt_t *s, const char *t)
{
    bool auth = false;
    for (int i = 0; *t; i++) {
        while (*t == ' ') {
            t++;
        }
        size_t len = strcspn(t, " =");
        if (len == 0) {
            if (*t == '=') {
                t++;
                continue;
            }
            break;
        }
        if (i == 0) {
            auth = word_is(t, len, "AUTH");
            s->cap_starttls |= word_is(t, len, "STARTTLS");
        } else if (auth) {
            s->cap_plain |= word_is(t, len, "PLAIN");
            s->cap_login |= word_is(t, len, "LOGIN");
        }
        t += len;
    }
}

static mt_act_t send_cmd(buf_t *b, mt_t *s, size_t *out_len, int next)
{
    if (b->over) {
        return fail(s, MT_ERR_REFUSED);  // out too small: the app's buffer, never the server
    }
    *out_len = b->n;
    s->state = next;
    return MT_SEND;
}

static mt_act_t ehlo(mt_t *s, buf_t *b, size_t *out_len)
{
    s->cap_starttls = s->cap_plain = s->cap_login = false;
    put(b, "EHLO ");
    put_clean(b, s->msg->helo);
    put(b, "\r\n");
    return send_cmd(b, s, out_len, S_EHLO);
}

static mt_act_t auth(mt_t *s, buf_t *b, size_t *out_len)
{
    const mt_msg_t *m = s->msg;
    if (s->cap_plain) {
        // RFC 4616: authzid (empty) NUL authcid NUL password
        unsigned char raw[2 * 64 + 3];
        size_t ul = strlen(m->user), pl = strlen(m->password);
        if (ul + pl + 2 > sizeof raw) {
            return fail(s, MT_ERR_AUTH);
        }
        raw[0] = 0;
        memcpy(raw + 1, m->user, ul);
        raw[1 + ul] = 0;
        memcpy(raw + 2 + ul, m->password, pl);
        char enc[(sizeof raw + 2) / 3 * 4 + 1];
        mt_base64(raw, ul + pl + 2, enc, sizeof enc);
        memset(raw, 0, sizeof raw);
        put(b, "AUTH PLAIN ");
        put(b, enc);
        put(b, "\r\n");
        memset(enc, 0, sizeof enc);
        return send_cmd(b, s, out_len, S_AUTH_DONE);
    }
    if (s->cap_login) {
        put(b, "AUTH LOGIN\r\n");
        return send_cmd(b, s, out_len, S_AUTH_USER);
    }
    return fail(s, MT_ERR_AUTH);
}

static mt_act_t put_b64_line(mt_t *s, buf_t *b, size_t *out_len, const char *text, int next)
{
    char enc[(64 + 2) / 3 * 4 + 1];
    size_t len = strlen(text);
    if (len > 64) {
        return fail(s, MT_ERR_AUTH);
    }
    mt_base64((const unsigned char *)text, len, enc, sizeof enc);
    put(b, enc);
    put(b, "\r\n");
    memset(enc, 0, sizeof enc);
    return send_cmd(b, s, out_len, next);
}

// A complete reply with `code`: what to send next.
static mt_act_t reply(mt_t *s, int code, buf_t *b, size_t *out_len)
{
    const mt_msg_t *m = s->msg;
    switch (s->state) {
    case S_GREETING:
        return code == 220 ? ehlo(s, b, out_len) : fail(s, MT_ERR_REFUSED);
    case S_EHLO:
        if (code != 250) {
            return fail(s, MT_ERR_REFUSED);
        }
        if (m->starttls && !s->tls_up) {
            if (!s->cap_starttls) {
                return fail(s, MT_ERR_TLS);  // never fall back to a plain login
            }
            put(b, "STARTTLS\r\n");
            return send_cmd(b, s, out_len, S_STARTTLS);
        }
        return auth(s, b, out_len);
    case S_STARTTLS:
        if (code != 220) {
            return fail(s, MT_ERR_TLS);
        }
        s->state = S_TLS_WAIT;
        return MT_START_TLS;
    case S_AUTH_USER:
        return code == 334 ? put_b64_line(s, b, out_len, m->user, S_AUTH_PASS)
                           : fail(s, MT_ERR_AUTH);
    case S_AUTH_PASS:
        return code == 334 ? put_b64_line(s, b, out_len, m->password, S_AUTH_DONE)
                           : fail(s, MT_ERR_AUTH);
    case S_AUTH_DONE:
        if (code != 235) {
            return fail(s, MT_ERR_AUTH);
        }
        put(b, "MAIL FROM:<");
        put_clean(b, m->from);
        put(b, ">\r\n");
        return send_cmd(b, s, out_len, S_MAIL);
    case S_MAIL:
        if (code != 250) {
            return fail(s, MT_ERR_REFUSED);
        }
        put(b, "RCPT TO:<");
        put_clean(b, m->to);
        put(b, ">\r\n");
        return send_cmd(b, s, out_len, S_RCPT);
    case S_RCPT:
        if (code != 250 && code != 251) {
            return fail(s, MT_ERR_REFUSED);
        }
        put(b, "DATA\r\n");
        return send_cmd(b, s, out_len, S_DATA);
    case S_DATA: {
        if (code != 354) {
            return fail(s, MT_ERR_REFUSED);
        }
        size_t n = mt_message(m, b->p, b->cap);
        if (n == 0) {
            return fail(s, MT_ERR_REFUSED);
        }
        b->n = n;
        return send_cmd(b, s, out_len, S_SENT);
    }
    case S_SENT:
        if (code != 250) {
            return fail(s, MT_ERR_REFUSED);
        }
        put(b, "QUIT\r\n");
        *out_len = b->over ? 0 : b->n;
        s->state = S_END;
        return MT_DONE;
    default:
        return fail(s, MT_ERR_REFUSED);  // a reply nobody asked for
    }
}

// A complete line in s->line: its code, and whether it ends the reply.
static mt_act_t line_done(mt_t *s, buf_t *b, size_t *out_len, bool *more)
{
    const char *l = s->line;
    *more = false;
    if (s->line_len < 3 || l[0] < '2' || l[0] > '5' || l[1] < '0' || l[1] > '9' ||
        l[2] < '0' || l[2] > '9' || (s->line_len > 3 && l[3] != ' ' && l[3] != '-')) {
        return fail(s, MT_ERR_REFUSED);  // not SMTP
    }
    int code = (l[0] - '0') * 100 + (l[1] - '0') * 10 + (l[2] - '0');
    if (s->code == 0) {
        s->code = code;
    }
    if (s->state == S_EHLO && s->line_len > 4) {
        ehlo_line(s, l + 4);
    }
    if (s->line_len > 3 && l[3] == '-') {
        *more = true;
        return MT_NEED_MORE;
    }
    code = s->code;
    s->code = 0;
    return reply(s, code, b, out_len);
}

mt_act_t mt_feed(mt_t *s, const char *in, size_t len, size_t *used,
                 char *out, size_t cap, size_t *out_len)
{
    buf_t b = {out, cap, 0, false};
    *out_len = 0;
    if (cap) {
        out[0] = '\0';
    }
    size_t i = 0;
    mt_act_t act = MT_NEED_MORE;
    if (s->state == S_END || s->state == S_TLS_WAIT) {
        *used = len;
        return s->state == S_END ? MT_FAILED : MT_NEED_MORE;
    }
    while (i < len) {
        char c = in[i++];
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (s->line_len < MT_LINE_MAX - 1) {
                s->line[s->line_len++] = c;
            }
            continue;
        }
        s->line[s->line_len] = '\0';
        bool more;
        act = line_done(s, &b, out_len, &more);
        s->line_len = 0;
        if (!more) {
            break;
        }
    }
    *used = i;
    return act;
}

mt_act_t mt_tls_ready(mt_t *s, char *out, size_t cap, size_t *out_len)
{
    buf_t b = {out, cap, 0, false};
    *out_len = 0;
    if (s->state != S_TLS_WAIT) {
        return fail(s, MT_ERR_TLS);
    }
    s->tls_up = true;
    return ehlo(s, &b, out_len);
}
