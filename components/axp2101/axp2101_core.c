#include "axp2101_core.h"

#include <stddef.h>

typedef struct {
    uint8_t vol_reg, onoff_reg, bit, field_mask;
} rail_regs_t;

static const rail_regs_t RAILS[] = {
    [AXP2101_DC3] = {AXP2101_REG_DC3_VOL, AXP2101_REG_DC_ONOFF, 2, 0x7F},
    [AXP2101_BLDO1] = {AXP2101_REG_BLDO1_VOL, AXP2101_REG_LDO_ONOFF0, 4, 0x1F},
    [AXP2101_BLDO2] = {AXP2101_REG_BLDO2_VOL, AXP2101_REG_LDO_ONOFF0, 5, 0x1F},
};

static bool known(axp2101_rail_t rail)
{
    return (unsigned)rail < sizeof RAILS / sizeof RAILS[0];
}

int axp2101_rail_code(axp2101_rail_t rail, unsigned mv)
{
    if (!known(rail)) {
        return AXP2101_E_ARG;
    }
    if (rail == AXP2101_DC3) {
        // 0.5 – 1.2 V in 10 mV steps (codes 0 – 70), 1.22 – 1.54 V in 20 mV
        // steps (71 – 87), 1.6 – 3.4 V in 100 mV steps (88 – 106).
        if (mv >= 500 && mv <= 1200 && mv % 10 == 0) {
            return (int)(mv - 500) / 10;
        }
        if (mv >= 1220 && mv <= 1540 && mv % 20 == 0) {
            return 71 + (int)(mv - 1220) / 20;
        }
        if (mv >= 1600 && mv <= 3400 && mv % 100 == 0) {
            return 88 + (int)(mv - 1600) / 100;
        }
        return AXP2101_E_ARG;
    }
    // BLDO1/2: 0.5 – 3.5 V in 100 mV steps.
    if (mv >= 500 && mv <= 3500 && mv % 100 == 0) {
        return (int)(mv - 500) / 100;
    }
    return AXP2101_E_ARG;
}

unsigned axp2101_vbat_decode(uint8_t high, uint8_t low)
{
    return ((unsigned)(high & 0x1F) << 8) | low;
}

axp2101_status_t axp2101_status_decode(uint8_t s1, uint8_t s2, uint8_t chg_ctrl)
{
    static const axp2101_charger_t chg[8] = {
        AXP2101_CHG_TRICKLE, AXP2101_CHG_PRE, AXP2101_CHG_CC,      AXP2101_CHG_CV,
        AXP2101_CHG_DONE,    AXP2101_CHG_OFF, AXP2101_CHG_UNKNOWN, AXP2101_CHG_UNKNOWN,
    };
    axp2101_status_t st = {
        .battery = (s1 & 0x08) != 0,
        .vbus = (s1 & 0x20) != 0,
        .charge_on = (chg_ctrl & 0x02) != 0,
        .current = (axp2101_current_t)((s2 >> 5) & 3),
        .charger = chg[s2 & 7],
    };
    return st;
}

const char *axp2101_current_name(axp2101_current_t c)
{
    static const char *const n[] = {"idle", "charging", "discharging", "?"};
    return (unsigned)c < 4 ? n[c] : "?";
}

const char *axp2101_charger_name(axp2101_charger_t c)
{
    static const char *const n[] = {"trickle", "pre", "cc", "cv", "done", "off", "?"};
    return (unsigned)c < 7 ? n[c] : "?";
}

static int update(const axp2101_bus_t *bus, uint8_t reg, uint8_t mask, uint8_t bits)
{
    uint8_t v;
    int err = bus->read8(bus->ctx, reg, &v);
    if (err == 0) {
        err = bus->write8(bus->ctx, reg, (uint8_t)((v & ~mask) | (bits & mask)));
    }
    return err;
}

int axp2101_core_rail(const axp2101_bus_t *bus, axp2101_rail_t rail, unsigned mv, bool on)
{
    if (!known(rail)) {
        return AXP2101_E_ARG;
    }
    const rail_regs_t *r = &RAILS[rail];
    if (on) {
        int code = axp2101_rail_code(rail, mv);
        if (code < 0) {
            return code;
        }
        int err = update(bus, r->vol_reg, r->field_mask, (uint8_t)code);
        if (err != 0) {
            return err;
        }
    }
    uint8_t bit = (uint8_t)(1u << r->bit);
    return update(bus, r->onoff_reg, bit, on ? bit : 0);
}

int axp2101_core_start(const axp2101_bus_t *bus)
{
    uint8_t id;
    int err = bus->read8(bus->ctx, AXP2101_REG_ID, &id);
    if (err != 0) {
        return err;
    }
    if (id != AXP2101_CHIP_ID) {
        return AXP2101_E_CHIP;
    }
    err = update(bus, AXP2101_REG_ADC_CTRL, 0x01, 0x01);
    if (err == 0) {
        err = update(bus, AXP2101_REG_BAT_DET, 0x01, 0x01);
    }
    return err;
}

int axp2101_core_vbat_mv(const axp2101_bus_t *bus, unsigned *mv)
{
    uint8_t h, l;
    int err = bus->read8(bus->ctx, AXP2101_REG_VBAT_H, &h);
    if (err == 0) {
        err = bus->read8(bus->ctx, AXP2101_REG_VBAT_H + 1, &l);
    }
    if (err == 0) {
        *mv = axp2101_vbat_decode(h, l);
    }
    return err;
}

int axp2101_core_status(const axp2101_bus_t *bus, axp2101_status_t *status)
{
    uint8_t s1, s2, cc;
    int err = bus->read8(bus->ctx, AXP2101_REG_STATUS1, &s1);
    if (err == 0) {
        err = bus->read8(bus->ctx, AXP2101_REG_STATUS2, &s2);
    }
    if (err == 0) {
        err = bus->read8(bus->ctx, AXP2101_REG_CHG_CTRL, &cc);
    }
    if (err == 0) {
        *status = axp2101_status_decode(s1, s2, cc);
    }
    return err;
}
