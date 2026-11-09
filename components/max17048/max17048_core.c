#include "max17048_core.h"

#include <stddef.h>

uint16_t max17048_mv(uint16_t vcell)
{
    return (uint16_t)((uint64_t)vcell * 78125u / 1000000u);
}

uint8_t max17048_pct(uint16_t soc)
{
    unsigned pct = soc >> 8;  // the high byte is whole percent
    return (uint8_t)(pct > 100 ? 100 : pct);
}

int max17048_core_check(const max17048_bus_t *bus)
{
    uint16_t v;
    int err = bus->read16(bus->ctx, MAX17048_REG_VERSION, &v);
    if (err != 0) {
        return err;
    }
    return (v & 0xFFF0) == 0x0010 ? 0 : MAX17048_E_ID;
}

int max17048_core_read(const max17048_bus_t *bus, max17048_reading_t *out)
{
    uint16_t vcell, soc;
    int err = bus->read16(bus->ctx, MAX17048_REG_VCELL, &vcell);
    if (err == 0) {
        err = bus->read16(bus->ctx, MAX17048_REG_SOC, &soc);
    }
    if (err == 0 && out != NULL) {
        out->mv = max17048_mv(vcell);
        out->pct = max17048_pct(soc);
    }
    return err;
}
