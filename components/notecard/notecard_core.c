#include "notecard_core.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- requests

static size_t put(char *buf, size_t n, const char *fmt, ...)
{
    if (buf == NULL || n == 0) {
        return 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    if (len < 0 || (size_t)len >= n) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)len;
}

// Names and values this core writes unquoted inside "...": no quote,
// backslash or control character can reach the request.
static bool word_ok(const char *s, const char *extra)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s) && strchr(extra, *s) == NULL) {
            return false;
        }
    }
    return true;
}

size_t nc_req(char *buf, size_t n, const char *name)
{
    if (!word_ok(name, "._")) {
        return 0;
    }
    return put(buf, n, "{\"req\":\"%s\"}\n", name);
}

bool nc_product_valid(const char *s)
{
    return word_ok(s, ".:-_") && strlen(s) <= NC_PRODUCT_MAX;
}

size_t nc_req_hub_set(char *buf, size_t n, const char *product, int outbound_min,
                      int inbound_min)
{
    if (!nc_product_valid(product) || outbound_min < 1 || inbound_min < 1) {
        return 0;
    }
    return put(buf, n,
               "{\"req\":\"hub.set\",\"product\":\"%s\",\"mode\":\"periodic\","
               "\"outbound\":%d,\"inbound\":%d}\n",
               product, outbound_min, inbound_min);
}

size_t nc_req_transport(char *buf, size_t n, const char *method)
{
    if (!word_ok(method, "-")) {
        return 0;
    }
    return put(buf, n, "{\"req\":\"card.transport\",\"method\":\"%s\"}\n", method);
}

size_t nc_req_note_add(char *buf, size_t n, const char *file, const char *body_json, bool sync)
{
    if (!word_ok(file, "._-") || body_json == NULL || body_json[0] != '{') {
        return 0;
    }
    return put(buf, n, "{\"req\":\"note.add\",\"file\":\"%s\",\"body\":%s%s}\n", file, body_json,
               sync ? ",\"sync\":true" : "");
}

size_t nc_req_location_mode(char *buf, size_t n, const char *mode, const double *lat,
                            const double *lon)
{
    if (!word_ok(mode, "-")) {
        return 0;
    }
    if (lat != NULL && lon != NULL) {
        if (*lat < -90 || *lat > 90 || *lon < -180 || *lon > 180) {
            return 0;
        }
        return put(buf, n,
                   "{\"req\":\"card.location.mode\",\"mode\":\"%s\",\"lat\":%.6f,\"lon\":%.6f}\n",
                   mode, *lat, *lon);
    }
    return put(buf, n, "{\"req\":\"card.location.mode\",\"mode\":\"%s\"}\n", mode);
}

// ----------------------------------------------------------------- answers

static const char *ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

// End of the string starting at p (the opening quote), past its closing
// quote; NULL if unterminated.
static const char *skip_string(const char *p)
{
    for (p++; *p; p++) {
        if (*p == '\\') {
            if (*++p == '\0') {
                return NULL;
            }
        } else if (*p == '"') {
            return p + 1;
        }
    }
    return NULL;
}

// End of the value starting at p; NULL if malformed.
static const char *skip_value(const char *p, int depth)
{
    if (depth > 16) {
        return NULL;
    }
    p = ws(p);
    if (*p == '"') {
        return skip_string(p);
    }
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        p = ws(p + 1);
        if (*p == close) {
            return p + 1;
        }
        for (;;) {
            if (close == '}') {
                if (*p != '"' || (p = skip_string(p)) == NULL) {
                    return NULL;
                }
                p = ws(p);
                if (*p++ != ':') {
                    return NULL;
                }
            }
            if ((p = skip_value(p, depth + 1)) == NULL) {
                return NULL;
            }
            p = ws(p);
            if (*p == ',') {
                p = ws(p + 1);
            } else if (*p == close) {
                return p + 1;
            } else {
                return NULL;
            }
        }
    }
    const char *start = p;
    while (*p && strchr(",}] \t\r\n", *p) == NULL) {
        p++;
    }
    return p == start ? NULL : p;
}

// The value of key (len bytes) in the object at p.
static bool find_key(const char *p, const char *key, size_t klen, const char **val,
                     size_t *vlen)
{
    p = ws(p);
    if (*p != '{') {
        return false;
    }
    p = ws(p + 1);
    while (*p == '"') {
        const char *k = p + 1;
        const char *kend = skip_string(p);
        if (kend == NULL) {
            return false;
        }
        p = ws(kend);
        if (*p++ != ':') {
            return false;
        }
        p = ws(p);
        const char *end = skip_value(p, 0);
        if (end == NULL) {
            return false;
        }
        if ((size_t)(kend - 1 - k) == klen && memcmp(k, key, klen) == 0) {
            *val = p;
            *vlen = (size_t)(end - p);
            return true;
        }
        p = ws(end);
        if (*p == ',') {
            p = ws(p + 1);
        }
    }
    return false;
}

bool nc_find(const char *json, const char *path, const char **val, size_t *len)
{
    if (json == NULL || path == NULL || *path == '\0') {
        return false;
    }
    const char *v = json;
    size_t vl = 0;
    for (;;) {
        const char *dot = strchr(path, '.');
        size_t klen = dot ? (size_t)(dot - path) : strlen(path);
        if (!find_key(v, path, klen, &v, &vl)) {
            return false;
        }
        if (dot == NULL) {
            break;
        }
        path = dot + 1;
    }
    *val = v;
    *len = vl;
    return true;
}

bool nc_str(const char *json, const char *path, char *out, size_t n)
{
    const char *v;
    size_t len;
    if (out == NULL || n == 0 || !nc_find(json, path, &v, &len) || len < 2 || v[0] != '"') {
        return false;
    }
    size_t o = 0;
    for (size_t i = 1; i + 1 < len; i++) {
        char c = v[i];
        if (c == '\\' && (v[i + 1] == '"' || v[i + 1] == '\\') && i + 2 < len) {
            c = v[++i];
        }
        if (o + 1 >= n) {
            out[0] = '\0';
            return false;
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

bool nc_num(const char *json, const char *path, double *out)
{
    const char *v;
    size_t len;
    if (out == NULL || !nc_find(json, path, &v, &len) || len == 0 || len > 40) {
        return false;
    }
    if (!(isdigit((unsigned char)v[0]) || v[0] == '-')) {
        return false;
    }
    char tmp[41];
    memcpy(tmp, v, len);
    tmp[len] = '\0';
    char *end;
    double d = strtod(tmp, &end);
    if (end != tmp + len) {
        return false;
    }
    *out = d;
    return true;
}

bool nc_err(const char *json, char *out, size_t n)
{
    const char *v;
    size_t len;
    if (!nc_find(json, "err", &v, &len)) {
        return false;
    }
    if (out != NULL && n > 0 && !nc_str(json, "err", out, n)) {
        out[0] = '\0';
    }
    return true;
}

static bool num_is(const char *json, const char *path, int want)
{
    double d;
    return nc_num(json, path, &d) && d == (double)want;
}

static bool str_is(const char *json, const char *path, const char *want)
{
    char s[NC_PRODUCT_MAX + 1];
    return nc_str(json, path, s, sizeof s) && strcmp(s, want) == 0;
}

bool nc_hub_matches(const char *hub_get, const char *product, int outbound_min,
                    int inbound_min)
{
    return product != NULL && !nc_err(hub_get, NULL, 0) && str_is(hub_get, "product", product) &&
           str_is(hub_get, "mode", "periodic") && num_is(hub_get, "outbound", outbound_min) &&
           num_is(hub_get, "inbound", inbound_min);
}

bool nc_transport_is(const char *answer, const char *method)
{
    return method != NULL && !nc_err(answer, NULL, 0) && str_is(answer, "method", method);
}
