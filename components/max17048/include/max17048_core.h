// MAX17048 single-cell fuel gauge, pure core: the registers, their scales and
// the read sequence over an abstract bus. No ESP-IDF headers, so the host tier
// compiles it and drives it on a fake bus.
#pragma once

#include <stdint.h>

#define MAX17048_ADDR 0x36
#define MAX17048_REG_VCELL 0x02    // 78.125 uV per bit
#define MAX17048_REG_SOC 0x04      // 1/256 % per bit
#define MAX17048_REG_VERSION 0x08  // 0x001x on the MAX17048/MAX17049

// Core results besides 0 and the bus's own nonzero codes, which pass through.
#define MAX17048_E_ID (-1)  // VERSION is not a MAX17048's

// The bus as the core sees it: 16-bit registers, most significant byte first.
typedef struct {
    int (*read16)(void *ctx, uint8_t reg, uint16_t *value);
    void *ctx;
} max17048_bus_t;

typedef struct {
    uint16_t mv;  // cell voltage in mV, truncated
    uint8_t pct;  // state of charge in whole %, truncated, at most 100
} max17048_reading_t;

uint16_t max17048_mv(uint16_t vcell);
uint8_t max17048_pct(uint16_t soc);

// Reads VERSION: 0, MAX17048_E_ID, or the bus's error.
int max17048_core_check(const max17048_bus_t *bus);

// Reads VCELL and SOC into *out.
int max17048_core_read(const max17048_bus_t *bus, max17048_reading_t *out);
