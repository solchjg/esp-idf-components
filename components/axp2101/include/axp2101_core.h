// AXP2101 power management IC, pure core: the register map this board uses,
// the voltage codes of its rails, the battery ADC and status decoding, and the
// rail sequences over an abstract bus. No ESP-IDF headers, so the host tier
// compiles it and drives it on a fake bus.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AXP2101_ADDR 0x34
#define AXP2101_CHIP_ID 0x4A

#define AXP2101_REG_STATUS1 0x00   // bit 5 VBUS good, bit 3 battery present
#define AXP2101_REG_STATUS2 0x01   // bits 6..5 battery current, bits 2..0 charger state
#define AXP2101_REG_ID 0x03
#define AXP2101_REG_CHG_CTRL 0x18  // bit 1 charging on
#define AXP2101_REG_ADC_CTRL 0x30  // bit 0 battery voltage ADC on
#define AXP2101_REG_VBAT_H 0x34    // 13 bits over 0x34 (bits 12..8) and 0x35, 1 mV a count
#define AXP2101_REG_BAT_DET 0x68   // bit 0 battery detection on
#define AXP2101_REG_DC_ONOFF 0x80
#define AXP2101_REG_DC3_VOL 0x84
#define AXP2101_REG_LDO_ONOFF0 0x90
#define AXP2101_REG_BLDO1_VOL 0x96
#define AXP2101_REG_BLDO2_VOL 0x97

// Core results besides 0 and the bus's own nonzero codes, which pass through.
#define AXP2101_E_ARG (-1)    // a voltage the rail cannot make, or an unknown rail
#define AXP2101_E_CHIP (-2)   // the chip answered with another ID

// The rails this core switches; others are left as the board's OTP sets them.
typedef enum {
    AXP2101_DC3,    // 0.5 – 3.4 V
    AXP2101_BLDO1,  // 0.5 – 3.5 V
    AXP2101_BLDO2,  // 0.5 – 3.5 V
} axp2101_rail_t;

typedef enum {
    AXP2101_CUR_IDLE,
    AXP2101_CUR_CHARGING,
    AXP2101_CUR_DISCHARGING,
    AXP2101_CUR_UNKNOWN,
} axp2101_current_t;

typedef enum {
    AXP2101_CHG_TRICKLE,
    AXP2101_CHG_PRE,
    AXP2101_CHG_CC,
    AXP2101_CHG_CV,
    AXP2101_CHG_DONE,
    AXP2101_CHG_OFF,
    AXP2101_CHG_UNKNOWN,
} axp2101_charger_t;

typedef struct {
    bool battery;   // a battery is present (needs detection on)
    bool vbus;      // USB or the solar input is good
    bool charge_on; // charging is enabled
    axp2101_current_t current;
    axp2101_charger_t charger;
} axp2101_status_t;

// Register access the core runs on: 0 or the bus's own nonzero error.
typedef struct {
    int (*read8)(void *ctx, uint8_t reg, uint8_t *value);
    int (*write8)(void *ctx, uint8_t reg, uint8_t value);
    void *ctx;
} axp2101_bus_t;

// The rail's voltage code for mv: AXP2101_E_ARG when the rail cannot make it
// (outside its range or between its steps), else the code (0 – 127).
int axp2101_rail_code(axp2101_rail_t rail, unsigned mv);

// The battery voltage in mV from the two ADC registers.
unsigned axp2101_vbat_decode(uint8_t high, uint8_t low);

// The battery and charger state from status 1, status 2 and charge control.
axp2101_status_t axp2101_status_decode(uint8_t s1, uint8_t s2, uint8_t chg_ctrl);

const char *axp2101_current_name(axp2101_current_t c);  // "idle", "charging", ...
const char *axp2101_charger_name(axp2101_charger_t c);  // "trickle", "cc", "done", ...

// Sets the rail's voltage (when on) and switches it; bits outside the voltage
// field and the other rails' switches are kept.
int axp2101_core_rail(const axp2101_bus_t *bus, axp2101_rail_t rail, unsigned mv, bool on);

// Checks the chip ID, turns on the battery ADC and battery detection.
int axp2101_core_start(const axp2101_bus_t *bus);

int axp2101_core_vbat_mv(const axp2101_bus_t *bus, unsigned *mv);
int axp2101_core_status(const axp2101_bus_t *bus, axp2101_status_t *status);
