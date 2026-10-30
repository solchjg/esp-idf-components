// BME280, pure core: calibration and sample parsing, the datasheet's integer
// compensation, and the forced-mode register sequence over an abstract bus.
// No ESP-IDF headers, so the host tier compiles it and drives it on a fake bus.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BME280_CHIP_ID 0x60

#define BME280_REG_CALIB_TP 0x88  // 26 bytes, 0x88 – 0xA1
#define BME280_REG_ID 0xD0
#define BME280_REG_CALIB_H 0xE1   // 7 bytes, 0xE1 – 0xE7
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS 0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_DATA 0xF7      // 8 bytes, 0xF7 – 0xFE

#define BME280_CALIB_TP_LEN 26
#define BME280_CALIB_H_LEN 7
#define BME280_DATA_LEN 8

#define BME280_STATUS_MEASURING 0x08

// Core results besides 0 (success) and the bus's own nonzero codes, which pass
// through unchanged. Negative, so they never collide with an esp_err_t.
#define BME280_E_CHIP_ID (-1)  // the chip answered with another ID than 0x60
#define BME280_E_BUSY (-2)     // the measurement did not finish in time

typedef struct {
    uint16_t t1;
    int16_t t2, t3;
    uint16_t p1;
    int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1;
    int16_t h2;
    uint8_t h3;
    int16_t h4, h5;
    int8_t h6;
} bme280_calib_t;

typedef struct {
    int32_t adc_t, adc_p, adc_h;
} bme280_raw_t;

typedef struct {
    int32_t temp_centi_c;  // 0.01 °C, −4000 – 8500
    uint32_t press_q8_pa;  // 1/256 Pa, 30000 – 110000 Pa
    uint32_t hum_q10_pct;  // 1/1024 %, 0 – 102400
} bme280_reading_t;

// The settings the driver writes: oversampling codes (1 = ×1 … 5 = ×16) and
// the IIR filter code (0 = off).
typedef struct {
    uint8_t osrs_t, osrs_p, osrs_h, filter;
} bme280_settings_t;

// The bus as the core sees it: each op returns 0 or a nonzero code the core
// passes through. ctx is the transport's own state.
typedef struct {
    int (*read)(void *ctx, uint8_t reg, uint8_t *buf, int len);
    int (*write)(void *ctx, uint8_t reg, uint8_t value);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} bme280_bus_t;

void bme280_parse_calib(const uint8_t tp[BME280_CALIB_TP_LEN],
                        const uint8_t h[BME280_CALIB_H_LEN], bme280_calib_t *out);
void bme280_parse_raw(const uint8_t data[BME280_DATA_LEN], bme280_raw_t *out);

// Datasheet §4.2.3 integer formulas, clamped to the sensor's ranges.
void bme280_compensate(const bme280_calib_t *c, const bme280_raw_t *raw,
                       bme280_reading_t *out);

// Register values for a forced measurement with these settings.
uint8_t bme280_ctrl_hum(const bme280_settings_t *s);
uint8_t bme280_ctrl_meas_forced(const bme280_settings_t *s);
uint8_t bme280_config(const bme280_settings_t *s);

// Maximum measurement time in ms (datasheet appendix B), rounded up.
uint32_t bme280_max_measure_ms(const bme280_settings_t *s);

// Check the chip ID, load the calibration, write ctrl_hum and config. The
// sensor stays in sleep mode.
int bme280_core_init(const bme280_bus_t *bus, const bme280_settings_t *s, bme280_calib_t *calib);

// Trigger one forced measurement, wait for it, read and compensate it. The
// sensor returns to sleep mode on its own.
int bme280_core_read(const bme280_bus_t *bus, const bme280_settings_t *s,
                     const bme280_calib_t *calib, bme280_reading_t *out);
