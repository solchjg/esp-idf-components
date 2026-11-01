#include "ble_setup_core.h"

#include <string.h>

void bs_init(bs_session_t *s, char *buf, size_t cap)
{
    s->buf = buf;
    s->cap = cap;
    bs_reset(s);
}

void bs_reset(bs_session_t *s)
{
    s->len = 0;
    s->buf[0] = '\0';
    s->read_off = 0;
    s->hist_line = 0;
}

bool bs_append(bs_session_t *s, const void *data, size_t len)
{
    if (len > s->cap - s->len) {
        s->len = 0;
        s->buf[0] = '\0';
        return false;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    s->buf[s->len] = '\0';
    return true;
}

static bool is(const char *v, size_t len, const char *word)
{
    return len == strlen(word) && memcmp(v, word, len) == 0;
}

bs_ctrl_t bs_control(const char *v, size_t len, uint32_t *arg)
{
    if (is(v, len, "apply")) {
        return BS_CTRL_APPLY;
    }
    if (is(v, len, "done")) {
        return BS_CTRL_DONE;
    }
    if (len >= 6 && memcmp(v, "range=", 6) == 0) {
        *arg = 6;
        return BS_CTRL_RANGE;
    }
    bool read = len > 5 && memcmp(v, "read=", 5) == 0;
    if ((read || (len > 5 && memcmp(v, "hist=", 5) == 0)) && len <= 5 + 9) {
        uint32_t n = 0;
        for (size_t i = 5; i < len; i++) {
            if (v[i] < '0' || v[i] > '9') {
                return BS_CTRL_BAD;
            }
            n = n * 10 + (uint32_t)(v[i] - '0');
        }
        *arg = n;
        return read ? BS_CTRL_READ : BS_CTRL_HIST;
    }
    return BS_CTRL_BAD;
}

size_t bs_page(const char *all, size_t all_len, uint32_t off, const char **start)
{
    if (off >= all_len) {
        *start = all + all_len;
        return 0;
    }
    size_t n = all_len - off;
    *start = all + off;
    return n > BS_ATTR_MAX ? BS_ATTR_MAX : n;
}

void bs_passkey_text(uint32_t passkey, char out[7])
{
    passkey %= 1000000;
    for (int i = 5; i >= 0; i--) {
        out[i] = (char)('0' + passkey % 10);
        passkey /= 10;
    }
    out[6] = '\0';
}
