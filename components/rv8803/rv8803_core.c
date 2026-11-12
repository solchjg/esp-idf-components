#include "rv8803_core.h"

static int bcd(uint8_t v, int max)
{
    int hi = v >> 4, lo = v & 0x0F;
    if (hi > 9 || lo > 9) {
        return -1;
    }
    int n = hi * 10 + lo;
    return n > max ? -1 : n;
}

static uint8_t to_bcd(int n)
{
    return (uint8_t)(((n / 10) << 4) | (n % 10));
}

// Days since 1970-01-01 of a civil date (H. Hinnant's algorithm).
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static int month_days(int y, int m)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : days[m - 1];
}

bool rv8803_regs_to_ms(const uint8_t regs[8], int64_t *epoch_ms)
{
    int cs = bcd(regs[0], 99);
    int sec = bcd(regs[1] & 0x7F, 59);
    int min = bcd(regs[2] & 0x7F, 59);
    int hour = bcd(regs[3] & 0x3F, 23);
    int day = bcd(regs[5] & 0x3F, 31);
    int mon = bcd(regs[6] & 0x1F, 12);
    int year = bcd(regs[7], 99);
    if (cs < 0 || sec < 0 || min < 0 || hour < 0 || day < 1 || mon < 1 || year < 0) {
        return false;
    }
    if (day > month_days(2000 + year, mon)) {
        return false;
    }
    int64_t s = days_from_civil(2000 + year, mon, day) * 86400 + hour * 3600 + min * 60 + sec;
    *epoch_ms = s * 1000 + cs * 10;
    return true;
}

bool rv8803_regs_from_s(int64_t s, uint8_t regs[7])
{
    if (s < RV8803_EPOCH_MIN || s >= RV8803_EPOCH_END) {
        return false;
    }
    int64_t days = s / 86400;
    int rem = (int)(s % 86400);
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    int wday = (int)((days + 4) % 7);  // 1970-01-01 was a Thursday; 0 = Sunday
    regs[0] = to_bcd(rem % 60);
    regs[1] = to_bcd(rem / 60 % 60);
    regs[2] = to_bcd(rem / 3600);
    regs[3] = (uint8_t)(1u << wday);  // one bit per weekday
    regs[4] = to_bcd(d);
    regs[5] = to_bcd(m);
    regs[6] = to_bcd(y - 2000);
    return true;
}

int rv8803_offset_steps(uint8_t reg)
{
    int v = reg & 0x3F;
    return v >= 32 ? v - 64 : v;
}

uint8_t rv8803_offset_reg(int steps)
{
    if (steps < RV8803_STEP_MIN) {
        steps = RV8803_STEP_MIN;
    }
    if (steps > RV8803_STEP_MAX) {
        steps = RV8803_STEP_MAX;
    }
    return (uint8_t)(steps & 0x3F);
}

int rv8803_trim(int cur_steps, int64_t err_ms, int64_t elapsed_s)
{
    if (elapsed_s <= 0) {
        return cur_steps;
    }
    // Rate error in ppm × 10 000: err_ms / 1000 / elapsed_s × 1e6 × 1e4.
    int64_t ppm_x10000 = err_ms * 10000000LL / elapsed_s;
    int64_t half = RV8803_STEP_PPM_X10000 / 2;
    int64_t steps = (ppm_x10000 >= 0 ? ppm_x10000 + half : ppm_x10000 - half) /
                    RV8803_STEP_PPM_X10000;
    int64_t next = cur_steps + steps;  // a clock that gained is slowed down: more steps
    if (next < RV8803_STEP_MIN) {
        next = RV8803_STEP_MIN;
    }
    if (next > RV8803_STEP_MAX) {
        next = RV8803_STEP_MAX;
    }
    return (int)next;
}

bool rv8803_stale(uint8_t flag, uint8_t ctrl)
{
    return (flag & RV8803_FLAG_V2F) || (ctrl & RV8803_CTRL_RESET);
}
