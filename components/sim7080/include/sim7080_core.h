// SIMCom SIM7080G LTE-M modem, pure core: the AT commands of a full attach,
// the network lock, the PSM request and grant, and the parsers for the answers
// the driver reads (+CEREG, +CSQ, +CCLK, +COPS, +CGNSINF, the firmware
// revision). No ESP-IDF headers, so the host tier compiles and drives it.
//
// Answers are the modem's text as the driver collects it: one string that may
// hold several lines and the final OK.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIM7080_CMD_MAX 96
#define SIM7080_ATTACH_CMDS 8   // most commands sim7080_attach_script writes
#define SIM7080_APN_MAX 63      // the longest APN the attach takes, as text
#define SIM7080_GRANT_NONE (-1)

// Core results.
#define SIM7080_E_ARG (-1)    // a setting the modem would not take, or a short buffer
#define SIM7080_E_PARSE (-2)  // the answer does not hold what was asked

// Radio access the attach locks the modem to.
typedef enum {
    SIM7080_RAT_CATM,  // LTE-M only
    SIM7080_RAT_NB,    // NB-IoT only
    SIM7080_RAT_BOTH,  // LTE-M and NB-IoT
} sim7080_rat_t;

typedef struct {
    const char *apn;     // letters, digits, '.', '-'; 1 – SIM7080_APN_MAX
    const char *plmn;    // "" for automatic choice, else MCC+MNC, 5 or 6 digits
    sim7080_rat_t rat;
    bool psm;            // ask for PSM (the timers below)
    uint32_t psm_tau_s;  // requested periodic TAU (T3412 extended)
    uint32_t psm_active_s;  // requested active time (T3324)
} sim7080_attach_cfg_t;

// True for an APN the attach accepts and a PLMN of "" or 5 – 6 digits.
bool sim7080_apn_valid(const char *apn);
bool sim7080_plmn_valid(const char *plmn);

// The network selection command: AT+COPS=0 (automatic, undoing an earlier
// lock) for an empty plmn, else AT+COPS=1,2,"<plmn>" (that network only, no
// fallback). SIM7080_E_ARG for a bad plmn or a short buffer; else its length.
int sim7080_cops_cmd(const char *plmn, char *buf, size_t size);

// A GPRS timer octet as 8 '0'/'1' characters and a NUL (3GPP TS 24.008
// 10.5.7.4a for T3324, 10.5.7.4a extended for T3412): the shortest unit that
// holds seconds within 5 bits, rounded up. SIM7080_E_ARG when it cannot.
int sim7080_timer_bits(uint32_t seconds, bool tau, char bits[9]);

// A timer octet's seconds, SIM7080_GRANT_NONE when deactivated or not 8 bits.
long sim7080_timer_s(const char *bits, bool tau);

// The PSM command: AT+CPSMS=1,,,"<tau>","<active>" or AT+CPSMS=0.
int sim7080_cpsms_cmd(const sim7080_attach_cfg_t *cfg, char *buf, size_t size);

// The full attach, in order, before CFUN=1: radio off, verbose errors, LTE
// only, the RAT, the PDP context with the APN, +CEREG mode 4 (the PSM timers
// in the report), the network clock; then CFUN=1. The network lock and the PSM
// request come after CFUN=1 (sim7080_cops_cmd, sim7080_cpsms_cmd), since the
// modem refuses them until its SIM is up. Returns the number of commands
// written to cmds, or SIM7080_E_ARG for a bad setting.
int sim7080_attach_script(const sim7080_attach_cfg_t *cfg, char cmds[][SIM7080_CMD_MAX], int max);

typedef struct {
    int stat;           // +CEREG <stat>: 1 home, 5 roaming, 2 searching, 3 denied, ...
    long active_s;      // the granted T3324, SIM7080_GRANT_NONE when not in the report
    long tau_s;         // the granted T3412 extended, likewise
} sim7080_cereg_t;

// The last "+CEREG:" in answer (mode 4 with or without the timers, or mode 0).
int sim7080_parse_cereg(const char *answer, sim7080_cereg_t *out);
bool sim7080_registered(const sim7080_cereg_t *c);  // stat 1 or 5

// FR-35.1: keep the modem in PSM only when the network granted an active time
// of at most limit_s (a TAU too); otherwise power it off between sends.
bool sim7080_psm_keep(const sim7080_cereg_t *grant, long limit_s);

// "+CSQ: <rssi>,<ber>": rssi 0 – 31, 99 when unknown.
int sim7080_parse_csq(const char *answer, int *rssi);

// "+CCLK: "yy/MM/dd,hh:mm:ss±zz"" as Unix seconds UTC; zz is quarter hours
// east of UTC. SIM7080_E_PARSE before the network has set the clock (the
// modem's default year 1980 reads "80"; years before 2024 are not taken).
int sim7080_parse_cclk(const char *answer, int64_t *unix_s);

// The network in "+COPS: <mode>,<format>,"<oper>"[,<act>]" (after AT+COPS=3,2
// the numeric MCC+MNC). Its length, or SIM7080_E_PARSE with no operator.
int sim7080_parse_cops(const char *answer, char *oper, size_t size);
int sim7080_parse_cops_mode(const char *answer, int *mode);

// The firmware revision from AT+CGMR ("Revision:1951B16SIM7080").
int sim7080_parse_cgmr(const char *answer, char *rev, size_t size);

typedef struct {
    bool fix;
    double lat, lon;
} sim7080_gnss_t;

// "+CGNSINF: <run>,<fix>,<utc>,<lat>,<lon>,...".
int sim7080_parse_cgnsinf(const char *answer, sim7080_gnss_t *out);
