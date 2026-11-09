#include "sim7670_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim7080_core.h"

static const char *last(const char *s, const char *tag)
{
    const char *p = NULL;
    if (s == NULL) {
        return NULL;
    }
    for (const char *q = s; (q = strstr(q, tag)) != NULL; q++) {
        p = q;
    }
    return p;
}

int sim7670_attach_script(const sim7670_attach_cfg_t *cfg, char cmds[][SIM7670_CMD_MAX], int max)
{
    if (cfg == NULL || cmds == NULL || max < SIM7670_ATTACH_CMDS || !sim7080_apn_valid(cfg->apn) ||
        !sim7080_plmn_valid(cfg->plmn)) {
        return SIM7670_E_ARG;
    }
    int n = 0;
    snprintf(cmds[n++], SIM7670_CMD_MAX, "ATE0");
    snprintf(cmds[n++], SIM7670_CMD_MAX, "AT+CMEE=2");
    snprintf(cmds[n++], SIM7670_CMD_MAX, "AT+CGDCONT=1,\"IP\",\"%s\"", cfg->apn);
    return n;
}

int sim7670_parse_cgmr(const char *answer, char *rev, size_t size)
{
    const char *p = last(answer, "+CGMR:");
    if (p == NULL || rev == NULL || size == 0) {
        return SIM7670_E_PARSE;
    }
    p += 6;
    while (*p == ' ') {
        p++;
    }
    size_t n = strcspn(p, "\r\n ");
    if (n == 0 || n >= size) {
        return SIM7670_E_PARSE;
    }
    memcpy(rev, p, n);
    rev[n] = '\0';
    return (int)n;
}

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - (int)(era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool civil_ok(int yy, int mo, int dd, int hh, int mi, int ss)
{
    return yy >= 24 && yy < 70 && mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && hh >= 0 && hh <= 23 &&
           mi >= 0 && mi <= 59 && ss >= 0 && ss <= 59;
}

int sim7670_parse_cclk(const char *answer, int64_t *unix_s)
{
    const char *p = last(answer, "+CCLK: \"");
    int yy, mo, dd, hh, mi, ss;
    if (p == NULL || unix_s == NULL ||
        sscanf(p + 8, "%2d/%2d/%2d,%2d:%2d:%2d", &yy, &mo, &dd, &hh, &mi, &ss) != 6 ||
        !civil_ok(yy, mo, dd, hh, mi, ss)) {
        return SIM7670_E_PARSE;
    }
    *unix_s = days_from_civil(2000 + yy, mo, dd) * 86400 + hh * 3600 + mi * 60 + ss;
    return 0;
}

#define FIELDS 24
#define FIELD_MAX 24

// The fields of one answer line, split at commas (empty ones kept).
static int split(const char *p, char f[FIELDS][FIELD_MAX])
{
    int n = 0;
    while (n < FIELDS) {
        size_t len = strcspn(p, ",\r\n");
        if (len >= FIELD_MAX) {
            return -1;
        }
        memcpy(f[n], p, len);
        f[n][len] = '\0';
        while (f[n][0] == ' ') {
            memmove(f[n], f[n] + 1, strlen(f[n]));
        }
        n++;
        p += len;
        if (*p != ',') {
            break;
        }
        p++;
    }
    return n;
}

// Decimal degrees, or (d)ddmm.mmmm when the integer part has at least
// dm_digits digits.
static bool degrees(const char *s, int dm_digits, double max, double *out)
{
    char *end;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || v < 0) {
        return false;
    }
    if ((int)strcspn(s, ".") >= dm_digits) {
        double d = (double)(long)(v / 100);
        double m = v - d * 100;
        if (m >= 60) {
            return false;
        }
        v = d + m / 60;
    }
    if (v > max) {
        return false;
    }
    *out = v;
    return true;
}

int sim7670_parse_cgnssinfo(const char *answer, sim7670_gnss_t *out)
{
    const char *p = last(answer, "+CGNSSINFO:");
    char f[FIELDS][FIELD_MAX];
    if (p == NULL || out == NULL) {
        return SIM7670_E_PARSE;
    }
    int n = split(p + 11, f);
    if (n < 1) {
        return SIM7670_E_PARSE;
    }
    memset(out, 0, sizeof *out);
    if (f[0][0] == '\0') {
        return 0;  // no fix
    }
    for (int i = 1; i + 5 < n; i++) {
        bool ns = !strcmp(f[i + 1], "N") || !strcmp(f[i + 1], "S");
        bool ew = !strcmp(f[i + 3], "E") || !strcmp(f[i + 3], "W");
        if (!ns || !ew) {
            continue;
        }
        double lat, lon;
        int dd, mo, yy, hh, mi, ss;
        if (!degrees(f[i], 4, 90, &lat) || !degrees(f[i + 2], 5, 180, &lon) ||
            sscanf(f[i + 4], "%2d%2d%2d", &dd, &mo, &yy) != 3 ||
            sscanf(f[i + 5], "%2d%2d%2d", &hh, &mi, &ss) != 3 || !civil_ok(yy, mo, dd, hh, mi, ss)) {
            return SIM7670_E_PARSE;
        }
        out->fix = true;
        out->lat = f[i + 1][0] == 'S' ? -lat : lat;
        out->lon = f[i + 3][0] == 'W' ? -lon : lon;
        out->utc = days_from_civil(2000 + yy, mo, dd) * 86400 + hh * 3600 + mi * 60 + ss;
        return 0;
    }
    return SIM7670_E_PARSE;
}
