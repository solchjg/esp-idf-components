// SIMCom SIM7670G LTE Cat-1 modem, pure core: the AT commands before an
// attach, and the parsers for the answers that differ from the SIM7080G's
// (the firmware revision, the network clock, +CGNSSINFO). +CEREG, +CSQ,
// +COPS and the network lock are 3GPP's and come from sim7080_core. No
// ESP-IDF headers, so the host tier compiles and drives it.
//
// Answers are the modem's text as the driver collects it: one string that may
// hold several lines and the final OK.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIM7670_CMD_MAX 96
#define SIM7670_ATTACH_CMDS 3  // most commands sim7670_attach_script writes

// Core results.
#define SIM7670_E_ARG (-1)    // a setting the modem would not take, or a short buffer
#define SIM7670_E_PARSE (-2)  // the answer does not hold what was asked

typedef struct {
    const char *apn;   // as sim7080_apn_valid
    const char *plmn;  // "" for automatic choice, else MCC+MNC, 5 or 6 digits
} sim7670_attach_cfg_t;

// The commands before registration is awaited, in order: echo off, verbose
// errors, the PDP context with the APN. The modem starts with its radio on
// and registers by itself; the network lock (sim7080_cops_cmd) follows once
// its SIM is up. Returns the number of commands written to cmds, or
// SIM7670_E_ARG for a bad setting.
int sim7670_attach_script(const sim7670_attach_cfg_t *cfg, char cmds[][SIM7670_CMD_MAX], int max);

// The revision from AT+CGMR ("+CGMR: 2374B01SIM767XM5A_M_CUS_WEIXUE"). Its
// length, or SIM7670_E_PARSE.
int sim7670_parse_cgmr(const char *answer, char *rev, size_t size);

// "+CCLK: "yy/MM/dd,hh:mm:ss±zz"" as Unix seconds. The SIM7670G keeps its
// clock in UTC and reports the network's zone beside it (seen on SLOT2:
// "26/11/07,01:11:17-20" at 01:11 UTC), so the zone is not applied.
// SIM7670_E_PARSE before the network has set the clock (a year before 2024
// or from 2070, so a clock reset to 1970 or 1980 is not taken).
int sim7670_parse_cclk(const char *answer, int64_t *unix_s);

typedef struct {
    bool fix;
    double lat, lon;  // degrees, south and west negative
    int64_t utc;      // the fix's UTC time as Unix seconds; 0 without a fix
} sim7670_gnss_t;

// "+CGNSSINFO: <mode>,<satellite counts…>,<lat>,<N|S>,<lon>,<E|W>,<ddmmyy>,
// <hhmmss.s>,…"; the number of satellite fields differs between firmware
// versions, so the place is found by its N/S and E/W fields. Latitude and
// longitude come as decimal degrees or, in older firmware, as (d)ddmm.mmmm;
// four or more integer digits (five for the longitude) mean the latter.
// "+CGNSSINFO: ,,,,,,,," is no fix (0, fix false).
int sim7670_parse_cgnssinfo(const char *answer, sim7670_gnss_t *out);
