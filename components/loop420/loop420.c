#include "loop420.h"

float loop420_ma(int32_t shunt_uv, uint32_t shunt_mohm)
{
    if (shunt_mohm == 0) {
        return 0.0f;
    }
    // uV / mOhm = mA; in double so a 16-bit ADC's step is kept exactly.
    return (float)((double)shunt_uv / (double)shunt_mohm);
}

loop420_state_t loop420_check(float ma)
{
    if (!(ma >= LOOP420_FAULT_LOW_MA)) {
        return LOOP420_LOW;  // also NaN
    }
    return ma > LOOP420_FAULT_HIGH_MA ? LOOP420_HIGH : LOOP420_OK;
}

float loop420_value(const loop420_scale_t *scale, float ma)
{
    return (float)((double)scale->lo +
                   ((double)ma - 4.0) / 16.0 * ((double)scale->hi - (double)scale->lo));
}

bool loop420_convert(const loop420_scale_t *scale, int32_t shunt_uv, uint32_t shunt_mohm,
                     float *ma, float *value, loop420_state_t *state)
{
    *ma = loop420_ma(shunt_uv, shunt_mohm);
    *state = loop420_check(*ma);
    if (*state != LOOP420_OK) {
        return false;
    }
    *value = loop420_value(scale, *ma);
    return true;
}
