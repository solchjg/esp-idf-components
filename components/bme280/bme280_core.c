#include "bme280_core.h"

#define POLL_MS 1
#define POLL_MAX 10  // after the maximum measurement time, at most this many 1 ms polls

static uint16_t u16le(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static int16_t s16le(const uint8_t *b) { return (int16_t)u16le(b); }

void bme280_parse_calib(const uint8_t tp[BME280_CALIB_TP_LEN],
                        const uint8_t h[BME280_CALIB_H_LEN], bme280_calib_t *out)
{
    out->t1 = u16le(&tp[0]);
    out->t2 = s16le(&tp[2]);
    out->t3 = s16le(&tp[4]);
    out->p1 = u16le(&tp[6]);
    out->p2 = s16le(&tp[8]);
    out->p3 = s16le(&tp[10]);
    out->p4 = s16le(&tp[12]);
    out->p5 = s16le(&tp[14]);
    out->p6 = s16le(&tp[16]);
    out->p7 = s16le(&tp[18]);
    out->p8 = s16le(&tp[20]);
    out->p9 = s16le(&tp[22]);
    out->h1 = tp[25];  // 0xA1; 0xA0 is unused
    out->h2 = s16le(&h[0]);
    out->h3 = h[2];
    // H4 and H5 are signed 12-bit values sharing 0xE5: H4 = E4[7:0] E5[3:0],
    // H5 = E6[7:0] E5[7:4]. Multiplying keeps the sign of E4/E6 without
    // shifting a negative value.
    out->h4 = (int16_t)((int8_t)h[3] * 16 + (h[4] & 0x0F));
    out->h5 = (int16_t)((int8_t)h[5] * 16 + (h[4] >> 4));
    out->h6 = (int8_t)h[6];
}

void bme280_parse_raw(const uint8_t d[BME280_DATA_LEN], bme280_raw_t *out)
{
    out->adc_p = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    out->adc_t = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    out->adc_h = (int32_t)(((uint32_t)d[6] << 8) | d[7]);
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// The datasheet's formulas as the Bosch reference code writes them. Right
// shifts of negative values are arithmetic on every compiler this targets
// (GCC, Clang); left shifts of values that can be negative are multiplications.
static int32_t t_fine_of(const bme280_calib_t *c, int32_t adc_t)
{
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)c->t1 << 1))) * ((int32_t)c->t2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)c->t1)) * ((adc_t >> 4) - ((int32_t)c->t1))) >> 12) *
                    ((int32_t)c->t3)) >> 14;
    return var1 + var2;
}

static uint32_t press_q8(const bme280_calib_t *c, int32_t t_fine, int32_t adc_p)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)c->p6;
    var2 = var2 + ((var1 * (int64_t)c->p5) * 131072);
    var2 = var2 + ((int64_t)c->p4 * 34359738368LL);
    var1 = ((var1 * var1 * (int64_t)c->p3) >> 8) + ((var1 * (int64_t)c->p2) * 4096);
    var1 = ((140737488355328LL + var1) * (int64_t)c->p1) >> 33;
    if (var1 == 0) {
        return 30000u * 256u;  // avoids a division by zero (an unwritten calibration)
    }
    int64_t p = 1048576 - adc_p;
    p = (((p * 2147483648LL) - var2) * 3125) / var1;
    var1 = ((int64_t)c->p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)c->p7 * 16);
    if (p < 30000LL * 256) {
        p = 30000LL * 256;
    } else if (p > 110000LL * 256) {
        p = 110000LL * 256;
    }
    return (uint32_t)p;
}

static uint32_t hum_q10(const bme280_calib_t *c, int32_t t_fine, int32_t adc_h)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_h << 14) - ((int32_t)c->h4 * 1048576) - (((int32_t)c->h5) * v)) + 16384) >> 15) *
         (((((((v * ((int32_t)c->h6)) >> 10) * (((v * ((int32_t)c->h3)) >> 11) + 32768)) >> 10) +
            2097152) * ((int32_t)c->h2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)c->h1)) >> 4);
    v = clamp32(v, 0, 419430400);
    return (uint32_t)(v >> 12);
}

void bme280_compensate(const bme280_calib_t *c, const bme280_raw_t *raw, bme280_reading_t *out)
{
    int32_t t_fine = t_fine_of(c, raw->adc_t);
    out->temp_centi_c = clamp32((t_fine * 5 + 128) >> 8, -4000, 8500);
    out->press_q8_pa = press_q8(c, t_fine, raw->adc_p);
    out->hum_q10_pct = hum_q10(c, t_fine, raw->adc_h);
}

uint8_t bme280_ctrl_hum(const bme280_settings_t *s) { return s->osrs_h & 0x07; }

uint8_t bme280_ctrl_meas_forced(const bme280_settings_t *s)
{
    return (uint8_t)(((s->osrs_t & 0x07) << 5) | ((s->osrs_p & 0x07) << 2) | 0x01);
}

uint8_t bme280_config(const bme280_settings_t *s) { return (uint8_t)((s->filter & 0x07) << 2); }

// Oversampling factor for a register code: 0 skips, 1..5 = 1, 2, 4, 8, 16.
static uint32_t factor(uint8_t code)
{
    return code == 0 ? 0 : 1u << ((code > 5 ? 5 : code) - 1);
}

uint32_t bme280_max_measure_ms(const bme280_settings_t *s)
{
    // Appendix B, in µs: 1250 + 2300·T + (2300·P + 575) + (2300·H + 575).
    uint32_t us = 1250 + 2300 * factor(s->osrs_t);
    if (s->osrs_p) {
        us += 2300 * factor(s->osrs_p) + 575;
    }
    if (s->osrs_h) {
        us += 2300 * factor(s->osrs_h) + 575;
    }
    return (us + 999) / 1000;
}

int bme280_core_init(const bme280_bus_t *bus, const bme280_settings_t *s, bme280_calib_t *calib)
{
    uint8_t id = 0;
    int err = bus->read(bus->ctx, BME280_REG_ID, &id, 1);
    if (err) {
        return err;
    }
    if (id != BME280_CHIP_ID) {
        return BME280_E_CHIP_ID;
    }
    uint8_t tp[BME280_CALIB_TP_LEN], h[BME280_CALIB_H_LEN];
    if ((err = bus->read(bus->ctx, BME280_REG_CALIB_TP, tp, sizeof tp)) ||
        (err = bus->read(bus->ctx, BME280_REG_CALIB_H, h, sizeof h))) {
        return err;
    }
    bme280_parse_calib(tp, h, calib);
    // ctrl_hum takes effect at the next ctrl_meas write (the trigger); config
    // is written in sleep mode, where the chip is now.
    if ((err = bus->write(bus->ctx, BME280_REG_CTRL_HUM, bme280_ctrl_hum(s))) ||
        (err = bus->write(bus->ctx, BME280_REG_CONFIG, bme280_config(s)))) {
        return err;
    }
    return 0;
}

int bme280_core_read(const bme280_bus_t *bus, const bme280_settings_t *s,
                     const bme280_calib_t *calib, bme280_reading_t *out)
{
    int err = bus->write(bus->ctx, BME280_REG_CTRL_MEAS, bme280_ctrl_meas_forced(s));
    if (err) {
        return err;
    }
    bus->delay_ms(bus->ctx, bme280_max_measure_ms(s));
    for (int i = 0;; i++) {
        uint8_t status = 0;
        if ((err = bus->read(bus->ctx, BME280_REG_STATUS, &status, 1))) {
            return err;
        }
        if (!(status & BME280_STATUS_MEASURING)) {
            break;
        }
        if (i == POLL_MAX) {
            return BME280_E_BUSY;
        }
        bus->delay_ms(bus->ctx, POLL_MS);
    }
    uint8_t d[BME280_DATA_LEN];
    if ((err = bus->read(bus->ctx, BME280_REG_DATA, d, sizeof d))) {
        return err;
    }
    bme280_raw_t raw;
    bme280_parse_raw(d, &raw);
    bme280_compensate(calib, &raw, out);
    return 0;
}
