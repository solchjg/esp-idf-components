// RV-8803-C7 real-time clock, pure core: the time registers to and from Unix
// time, the flag bits, and the offset (rate trim) register. No ESP-IDF
// headers, so the host tier compiles it.
//
// The chip counts in UTC here: the app keeps the time zone. Its calendar runs
// from 2000 to 2099.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RV8803_ADDR 0x32  // fixed

#define RV8803_REG_100TH 0x00  // 0x00 - 0x07: 1/100 s, s, min, h, weekday, date, month, year
#define RV8803_REG_SEC 0x01
#define RV8803_REG_FLAG 0x0E
#define RV8803_REG_CTRL 0x0F
#define RV8803_REG_OFFSET 0x2C

#define RV8803_FLAG_V1F 0x01  // voltage low: temperature compensation may have stopped
#define RV8803_FLAG_V2F 0x02  // voltage too low: the time is lost
#define RV8803_CTRL_RESET 0x01  // holds the divider chain: set while the time is written, and
                                // by the chip itself after a supply dip; the time is frozen until cleared

// 2000-01-01T00:00:00Z and 2100-01-01T00:00:00Z: the chip's calendar.
#define RV8803_EPOCH_MIN 946684800LL
#define RV8803_EPOCH_END 4102444800LL

// One offset step in ppm (×10 000) and the step range of the 6-bit register.
#define RV8803_STEP_PPM_X10000 2384
#define RV8803_STEP_MIN (-32)
#define RV8803_STEP_MAX 31

// regs: the eight registers from RV8803_REG_100TH as read. True with
// *epoch_ms the time they hold; false when they hold no valid date (BCD out
// of range, a day the month does not have).
bool rv8803_regs_to_ms(const uint8_t regs[8], int64_t *epoch_ms);

// From the flag and control registers: true when the time registers are not
// to be trusted, because the backup ran out (V2F) or the divider is held
// (RESET), which freezes the time at the second it stopped.
bool rv8803_stale(uint8_t flag, uint8_t ctrl);

// The seven registers from RV8803_REG_SEC for whole second s. False, regs
// untouched, outside the chip's calendar.
bool rv8803_regs_from_s(int64_t s, uint8_t regs[7]);

// The offset register (bits 5:0, two's complement) as steps, and back
// (clamped to the register's range).
int rv8803_offset_steps(uint8_t reg);
uint8_t rv8803_offset_reg(int steps);

// The trim that takes out a measured error: the clock gained err_ms (ahead of
// the reference; negative when behind) over elapsed_s with trim cur_steps.
// Returns the new trim, clamped. A positive step speeds the clock up (Micro
// Crystal application manual, §3.11; checked by the commissioning probe).
int rv8803_trim(int cur_steps, int64_t err_ms, int64_t elapsed_s);
