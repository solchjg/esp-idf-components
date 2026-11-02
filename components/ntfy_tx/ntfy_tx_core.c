#include "ntfy_tx_core.h"

#include <stdio.h>
#include <string.h>

const char *nt_reason_name(nt_reason_t r)
{
    static const char *const names[] = {"ok",      "dns",   "connect", "tls",    "cert",
                                        "auth",    "refused", "limit", "server", "timeout"};
    return (unsigned)r < sizeof names / sizeof names[0] ? names[r] : "refused";
}

bool nt_url_https(const char *url)
{
    return url && strncmp(url, "https://", 8) == 0;
}

static bool host_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '-';
}

bool nt_url_valid(const char *url)
{
    if (!url) {
        return false;
    }
    size_t len = strlen(url);
    if (len < NT_URL_MIN || len > NT_URL_MAX) {
        return false;
    }
    const char *p;
    if (strncmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else if (nt_url_https(url)) {
        p = url + 8;
    } else {
        return false;
    }
    const char *host = p;
    while (host_char(*p)) {
        p++;
    }
    if (p == host) {
        return false;
    }
    if (*p == ':') {
        p++;
        long port = 0;
        const char *digits = p;
        while (*p >= '0' && *p <= '9' && p - digits < 6) {
            port = port * 10 + (*p++ - '0');
        }
        if (p == digits || port < 1 || port > 65535) {
            return false;
        }
    }
    return *p == '\0';
}

bool nt_topic_valid(const char *topic)
{
    if (!topic) {
        return false;
    }
    size_t len = strlen(topic);
    if (len < 1 || len > NT_TOPIC_MAX) {
        return false;
    }
    for (const char *p = topic; *p; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool nt_token_valid(const char *token)
{
    if (!token) {
        return false;
    }
    size_t len = strlen(token);
    if (len < 1 || len > NT_TOKEN_MAX) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)token; *p; p++) {
        if (*p < 0x21 || *p > 0x7e) {
            return false;
        }
    }
    return true;
}

// A header value: one line of printable ASCII, cut at max bytes.
static void clean(const char *in, char *out, size_t max)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n < max; p++) {
        out[n++] = *p < 0x20 || *p == 0x7f ? ' ' : *p > 0x7e ? '?' : (char)*p;
    }
    out[n] = '\0';
}

static void add(nt_req_t *r, const char *name, const char *value, size_t max)
{
    nt_header_t *h = &r->header[r->headers++];
    h->name = name;
    clean(value, h->value, max);
}

bool nt_build(const char *url, const char *topic, const char *token, const nt_msg_t *msg,
              nt_req_t *out)
{
    memset(out, 0, sizeof *out);
    bool has_token = token && token[0];
    if (!nt_url_valid(url) || !nt_topic_valid(topic) || (has_token && !nt_token_valid(token))) {
        return false;
    }
    snprintf(out->endpoint, sizeof out->endpoint, "%s/%s", url, topic);
    if (msg->title && msg->title[0]) {
        add(out, "Title", msg->title, NT_TITLE_MAX);
    }
    if (msg->priority >= 1 && msg->priority <= 5) {
        char p[2] = {(char)('0' + msg->priority), '\0'};
        add(out, "Priority", p, 1);
    }
    if (msg->tags && msg->tags[0]) {
        add(out, "Tags", msg->tags, NT_TAGS_MAX);
    }
    if (msg->click && msg->click[0]) {
        add(out, "Click", msg->click, NT_CLICK_MAX);
    }
    if (has_token) {
        char bearer[8 + NT_TOKEN_MAX];
        snprintf(bearer, sizeof bearer, "Bearer %s", token);
        add(out, "Authorization", bearer, sizeof bearer - 1);
    }
    out->body = msg->body ? msg->body : "";
    out->body_len = strlen(out->body);
    if (out->body_len > NT_BODY_MAX) {
        out->body_len = NT_BODY_MAX;
    }
    return true;
}

nt_reason_t nt_status(int status)
{
    if (status >= 200 && status <= 299) {
        return NT_OK;
    }
    if (status == 401 || status == 403) {
        return NT_ERR_AUTH;
    }
    if (status == 429) {
        return NT_ERR_LIMIT;
    }
    if (status >= 500 && status <= 599) {
        return NT_ERR_SERVER;
    }
    return NT_ERR_REFUSED;
}
