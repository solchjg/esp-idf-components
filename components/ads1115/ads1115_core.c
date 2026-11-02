#include "ads1115_core.h"

#include <stddef.h>

#define POLLS_MAX 5  // OS-bit polls after the conversion time, 1 ms apart

static const int32_t full_scale_uv[] = {6144000, 4096000, 2048000, 1024000, 512000, 256000};
static const uint16_t sps[] = {8, 16, 32, 64, 128, 250, 475, 860};

uint16_t ads1115_config_single(const ads1115_settings_t *s)
{
    return (uint16_t)(ADS1115_OS | ((s->mux & 7u) << 12) | ((s->pga & 7u) << 9) |
                      ADS1115_MODE_SINGLE | ((s->dr & 7u) << 5) | ADS1115_COMP_OFF);
}

uint32_t ads1115_conversion_ms(ads1115_dr_t dr)
{
    uint32_t r = sps[dr & 7u];
    return (1000u + r - 1u) / r + 1u;
}

int32_t ads1115_uv(int16_t raw, ads1115_pga_t pga)
{
    int32_t fs = full_scale_uv[pga <= ADS1115_PGA_256 ? pga : ADS1115_PGA_256];
    return (int32_t)((int64_t)raw * fs / 32768);
}

static int one(const ads1115_bus_t *bus, const ads1115_settings_t *s, int16_t *raw)
{
    int err = bus->write16(bus->ctx, ADS1115_REG_CONFIG, ads1115_config_single(s));
    if (err) {
        return err;
    }
    bus->delay_ms(bus->ctx, ads1115_conversion_ms(s->dr));
    for (int i = 0;; i++) {
        uint16_t cfg;
        err = bus->read16(bus->ctx, ADS1115_REG_CONFIG, &cfg);
        if (err) {
            return err;
        }
        if (cfg & ADS1115_OS) {
            break;
        }
        if (i + 1 >= POLLS_MAX) {
            return ADS1115_E_BUSY;
        }
        bus->delay_ms(bus->ctx, 1);
    }
    uint16_t v;
    err = bus->read16(bus->ctx, ADS1115_REG_CONVERSION, &v);
    if (err == 0) {
        *raw = (int16_t)v;
    }
    return err;
}

int ads1115_core_read_mean(const ads1115_bus_t *bus, const ads1115_settings_t *s, int n,
                           int32_t *mean_uv)
{
    if (n < 1 || n > ADS1115_MEAN_MAX) {
        return ADS1115_E_ARG;
    }
    int32_t sum = 0;
    for (int i = 0; i < n; i++) {
        int16_t raw;
        int err = one(bus, s, &raw);
        if (err) {
            return err;
        }
        sum += raw;
    }
    // Scaled from the sum, so the mean keeps the resolution it gains.
    int32_t fs = full_scale_uv[s->pga <= ADS1115_PGA_256 ? s->pga : ADS1115_PGA_256];
    *mean_uv = (int32_t)((int64_t)sum * fs / (32768 * (int64_t)n));
    return 0;
}
