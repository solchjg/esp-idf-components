#include "sim7080_core.h"

#include <stdio.h>
#include <string.h>

static bool digits(const char *s, size_t lo, size_t hi)
{
    size_t n = 0;
    for (; s[n]; n++) {
        if (s[n] < '0' || s[n] > '9') {
            return false;
        }
    }
    return n >= lo && n <= hi;
}

bool sim7080_apn_valid(const char *apn)
{
    if (apn == NULL) {
        return false;
    }
    size_t n = 0;
    for (; apn[n]; n++) {
        char c = apn[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return n >= 1 && n <= SIM7080_APN_MAX;
}

bool sim7080_plmn_valid(const char *plmn)
{
    return plmn != NULL && (plmn[0] == '\0' || digits(plmn, 5, 6));
}

static int put(size_t size, int n)
{
    return n < 0 || (size_t)n >= size ? SIM7080_E_ARG : n;
}

int sim7080_cops_cmd(const char *plmn, char *buf, size_t size)
{
    if (!sim7080_plmn_valid(plmn) || buf == NULL) {
        return SIM7080_E_ARG;
    }
    if (plmn[0] == '\0') {
        return put(size, snprintf(buf, size, "AT+COPS=0"));
    }
    return put(size, snprintf(buf, size, "AT+COPS=1,2,\"%s\"", plmn));
}

// Unit seconds by the 3 high bits; -1 for "deactivated" and unused codes.
static const long T3324_UNIT[8] = {2, 60, 360, -1, -1, -1, -1, -1};
static const long T3412_UNIT[8] = {600, 3600, 36000, 2, 30, 60, 1152000, -1};

long sim7080_timer_s(const char *bits, bool tau)
{
    if (bits == NULL) {
        return SIM7080_GRANT_NONE;
    }
    int v = 0;
    for (int i = 0; i < 8; i++) {
        if (bits[i] != '0' && bits[i] != '1') {
            return SIM7080_GRANT_NONE;
        }
        v = v << 1 | (bits[i] - '0');
    }
    if (bits[8] != '\0' && bits[8] != '"') {
        return SIM7080_GRANT_NONE;
    }
    long unit = (tau ? T3412_UNIT : T3324_UNIT)[v >> 5];
    return unit < 0 ? SIM7080_GRANT_NONE : unit * (v & 0x1F);
}

int sim7080_timer_bits(uint32_t seconds, bool tau, char bits[9])
{
    const long *units = tau ? T3412_UNIT : T3324_UNIT;
    int best = -1;
    long best_unit = 0;
    for (int code = 0; code < 8; code++) {
        long u = units[code];
        if (u <= 0) {
            continue;
        }
        long count = ((long)seconds + u - 1) / u;
        // The finest unit whose 31 counts reach seconds.
        if (count <= 31 && (best < 0 || u < best_unit)) {
            best = code;
            best_unit = u;
        }
    }
    if (best < 0 || seconds == 0) {
        return SIM7080_E_ARG;
    }
    int v = best << 5 | (int)(((long)seconds + best_unit - 1) / best_unit);
    for (int i = 0; i < 8; i++) {
        bits[i] = (char)('0' + ((v >> (7 - i)) & 1));
    }
    bits[8] = '\0';
    return 0;
}

int sim7080_cpsms_cmd(const sim7080_attach_cfg_t *cfg, char *buf, size_t size)
{
    if (cfg == NULL || buf == NULL) {
        return SIM7080_E_ARG;
    }
    if (!cfg->psm) {
        return put(size, snprintf(buf, size, "AT+CPSMS=0"));
    }
    char tau[9], active[9];
    if (sim7080_timer_bits(cfg->psm_tau_s, true, tau) != 0 ||
        sim7080_timer_bits(cfg->psm_active_s, false, active) != 0) {
        return SIM7080_E_ARG;
    }
    return put(size, snprintf(buf, size, "AT+CPSMS=1,,,\"%s\",\"%s\"", tau, active));
}

int sim7080_attach_script(const sim7080_attach_cfg_t *cfg, char cmds[][SIM7080_CMD_MAX], int max)
{
    static const char *const RAT[] = {"AT+CMNB=1", "AT+CMNB=2", "AT+CMNB=3"};
    if (cfg == NULL || !sim7080_apn_valid(cfg->apn) || !sim7080_plmn_valid(cfg->plmn) ||
        (unsigned)cfg->rat > SIM7080_RAT_BOTH || max < SIM7080_ATTACH_CMDS) {
        return SIM7080_E_ARG;
    }
    int n = 0;
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CFUN=0");
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CMEE=2");   // errors as text
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CNMP=38");  // LTE only
    snprintf(cmds[n++], SIM7080_CMD_MAX, "%s", RAT[cfg->rat]);
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CGDCONT=1,\"IP\",\"%s\"", cfg->apn);
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CEREG=4");  // the PSM timers in the report
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CLTS=1");   // the modem clock from the network
    snprintf(cmds[n++], SIM7080_CMD_MAX, "AT+CFUN=1");
    return n;
}

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

int sim7080_parse_cereg(const char *answer, sim7080_cereg_t *out)
{
    const char *p = last(answer, "+CEREG:");
    if (p == NULL || out == NULL) {
        return SIM7080_E_PARSE;
    }
    p += 7;
    int n, stat;
    if (sscanf(p, " %d,%d", &n, &stat) != 2) {
        return SIM7080_E_PARSE;
    }
    out->stat = stat;
    out->active_s = SIM7080_GRANT_NONE;
    out->tau_s = SIM7080_GRANT_NONE;
    // Mode 4 when registered: <n>,<stat>,<tac>,<ci>,<AcT>[,<cause>,<reject>
    // [,<active>,<tau>]]. The timers are the line's last two quoted 8-bit
    // fields; <ci> is quoted with 8 characters too, so one such field alone is
    // the cell, not a grant. Cause fields the modem leaves out change nothing.
    if (n == 4) {
        const char *q8[2] = {NULL, NULL};
        int count = 0;
        for (const char *q = p; *q && *q != '\r' && *q != '\n'; q++) {
            if (*q != '"') {
                continue;
            }
            const char *e = strchr(q + 1, '"');
            if (e == NULL) {
                break;
            }
            if (e - q - 1 == 8) {
                q8[0] = q8[1];
                q8[1] = q + 1;
                count++;
            }
            q = e;
        }
        if (count >= 2) {
            out->active_s = sim7080_timer_s(q8[0], false);
            out->tau_s = sim7080_timer_s(q8[1], true);
        }
    }
    return 0;
}

bool sim7080_registered(const sim7080_cereg_t *c)
{
    return c != NULL && (c->stat == 1 || c->stat == 5);
}

bool sim7080_psm_keep(const sim7080_cereg_t *grant, long limit_s)
{
    return grant != NULL && grant->active_s != SIM7080_GRANT_NONE &&
           grant->tau_s != SIM7080_GRANT_NONE && grant->active_s <= limit_s;
}

int sim7080_parse_csq(const char *answer, int *rssi)
{
    const char *p = last(answer, "+CSQ:");
    int r, ber;
    if (p == NULL || rssi == NULL || sscanf(p + 5, " %d,%d", &r, &ber) != 2 ||
        !((r >= 0 && r <= 31) || r == 99)) {
        return SIM7080_E_PARSE;
    }
    *rssi = r;
    return 0;
}

// Days from 1970-01-01 to y-m-d in the proleptic Gregorian calendar.
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - (int)(era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int sim7080_parse_cclk(const char *answer, int64_t *unix_s)
{
    const char *p = last(answer, "+CCLK: \"");
    int yy, mo, dd, hh, mi, ss, zz;
    char sign;
    if (p == NULL || unix_s == NULL ||
        sscanf(p + 8, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d", &yy, &mo, &dd, &hh, &mi, &ss, &sign, &zz) != 8 ||
        yy < 24 || yy >= 80 || mo < 1 || mo > 12 || dd < 1 || dd > 31 || hh > 23 || mi > 59 ||
        ss > 59 || (sign != '+' && sign != '-') || zz > 56) {
        return SIM7080_E_PARSE;
    }
    int64_t t = days_from_civil(2000 + yy, mo, dd) * 86400 + hh * 3600 + mi * 60 + ss;
    t -= (int64_t)(sign == '-' ? -zz : zz) * 15 * 60;
    *unix_s = t;
    return 0;
}

int sim7080_parse_cops_mode(const char *answer, int *mode)
{
    const char *p = last(answer, "+COPS:");
    int m;
    if (p == NULL || mode == NULL || sscanf(p + 6, " %d", &m) != 1) {
        return SIM7080_E_PARSE;
    }
    *mode = m;
    return 0;
}

int sim7080_parse_cops(const char *answer, char *oper, size_t size)
{
    const char *p = last(answer, "+COPS:");
    if (p == NULL || oper == NULL || size == 0) {
        return SIM7080_E_PARSE;
    }
    const char *q = strchr(p, '"');
    const char *eol = strpbrk(p, "\r\n");
    if (q == NULL || (eol != NULL && q > eol)) {
        return SIM7080_E_PARSE;
    }
    const char *e = strchr(q + 1, '"');
    if (e == NULL || e == q + 1 || (size_t)(e - q - 1) >= size) {
        return SIM7080_E_PARSE;
    }
    size_t n = (size_t)(e - q - 1);
    memcpy(oper, q + 1, n);
    oper[n] = '\0';
    return (int)n;
}

int sim7080_parse_cgmr(const char *answer, char *rev, size_t size)
{
    const char *p = last(answer, "Revision:");
    if (p == NULL || rev == NULL || size == 0) {
        return SIM7080_E_PARSE;
    }
    p += 9;
    while (*p == ' ') {
        p++;
    }
    size_t n = strcspn(p, "\r\n ");
    if (n == 0 || n >= size) {
        return SIM7080_E_PARSE;
    }
    memcpy(rev, p, n);
    rev[n] = '\0';
    return (int)n;
}

int sim7080_parse_cgnsinf(const char *answer, sim7080_gnss_t *out)
{
    const char *p = last(answer, "+CGNSINF:");
    int run, fix;
    char utc[24];
    double lat, lon;
    if (p == NULL || out == NULL || sscanf(p + 9, " %d", &run) != 1) {
        return SIM7080_E_PARSE;
    }
    out->fix = false;  // GNSS off ("0,,,...") or still seeking ("1,0,...")
    if (run == 1 && sscanf(p + 9, " %d,%d", &run, &fix) == 2 && fix == 1 &&
        sscanf(p + 9, " %d,%d,%23[^,],%lf,%lf", &run, &fix, utc, &lat, &lon) == 5 &&
        lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
        out->fix = true;
        out->lat = lat;
        out->lon = lon;
    }
    return 0;
}
