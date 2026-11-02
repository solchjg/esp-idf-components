// ADS1115 16-bit ADC, pure core: the config word, the conversion's scale, and
// the single-shot register sequence over an abstract bus. No ESP-IDF headers,
// so the host tier compiles it and drives it on a fake bus.
#pragma once

#include <stdint.h>

#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG 0x01

#define ADS1115_OS 0x8000           // write: start a conversion; read: 1 = idle
#define ADS1115_MODE_SINGLE 0x0100  // single shot, power-down between
#define ADS1115_COMP_OFF 0x0003     // comparator disabled, ALERT/RDY high-Z

// Input multiplexer (bits 14:12): differential pairs, then single-ended.
typedef enum {
    ADS1115_MUX_A0_A1 = 0,
    ADS1115_MUX_A0_A3 = 1,
    ADS1115_MUX_A1_A3 = 2,
    ADS1115_MUX_A2_A3 = 3,
    ADS1115_MUX_A0 = 4,
    ADS1115_MUX_A1 = 5,
    ADS1115_MUX_A2 = 6,
    ADS1115_MUX_A3 = 7,
} ads1115_mux_t;

// Full-scale range (bits 11:9).
typedef enum {
    ADS1115_PGA_6144 = 0,  // ±6.144 V
    ADS1115_PGA_4096 = 1,  // ±4.096 V
    ADS1115_PGA_2048 = 2,
    ADS1115_PGA_1024 = 3,
    ADS1115_PGA_512 = 4,
    ADS1115_PGA_256 = 5,
} ads1115_pga_t;

// Data rate (bits 7:5): 8, 16, 32, 64, 128, 250, 475, 860 samples per second.
typedef enum {
    ADS1115_DR_8 = 0,
    ADS1115_DR_16 = 1,
    ADS1115_DR_32 = 2,
    ADS1115_DR_64 = 3,
    ADS1115_DR_128 = 4,
    ADS1115_DR_250 = 5,
    ADS1115_DR_475 = 6,
    ADS1115_DR_860 = 7,
} ads1115_dr_t;

// Core results besides 0 and the bus's own nonzero codes, which pass through.
#define ADS1115_E_BUSY (-1)  // the conversion did not finish in time
#define ADS1115_E_ARG (-2)   // n outside 1 – ADS1115_MEAN_MAX

#define ADS1115_MEAN_MAX 16

typedef struct {
    ads1115_mux_t mux;
    ads1115_pga_t pga;
    ads1115_dr_t dr;
} ads1115_settings_t;

// The bus as the core sees it: 16-bit registers, most significant byte first.
typedef struct {
    int (*read16)(void *ctx, uint8_t reg, uint16_t *value);
    int (*write16)(void *ctx, uint8_t reg, uint16_t value);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} ads1115_bus_t;

// The config word that starts one single-shot conversion.
uint16_t ads1115_config_single(const ads1115_settings_t *s);

// One conversion's time in ms at the data rate, rounded up, plus 1 ms margin.
uint32_t ads1115_conversion_ms(ads1115_dr_t dr);

// A conversion result in microvolts at the range.
int32_t ads1115_uv(int16_t raw, ads1115_pga_t pga);

// n single-shot conversions, each started, waited for (the OS bit polled after
// the conversion time) and read; *mean_uv their mean in microvolts.
int ads1115_core_read_mean(const ads1115_bus_t *bus, const ads1115_settings_t *s, int n,
                           int32_t *mean_uv);
