// SIMCom SIM7080G LTE-M modem over esp_modem: power and PSM wake by PWRKEY,
// the full attach (network lock, PSM request), the PSM resume, the network's
// time and the modem's GNSS, and the switch between AT commands and PPP.
// Board-agnostic: pins and the APN come from sim7080_config_t. The PPP netif
// is the app's (esp_netif_new(ESP_NETIF_DEFAULT_PPP())), and so are the IP
// events: the app waits for IP_EVENT_PPP_GOT_IP after sim7080_ppp_up. One
// modem per firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries a failed send or aborts:
// what happens on failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "sim7080_core.h"

typedef struct {
    int uart_port;           // e.g. 1
    int rx_gpio, tx_gpio;    // the ESP32-S3's side: RX takes the modem's TXD
    int baud;                // 115200
    int pwrkey_gpio;         // driven low at rest, pulsed high
    int dtr_gpio;            // held low (UART awake); -1 when not wired
} sim7080_config_t;

typedef struct {
    bool pulsed;       // PWRKEY was pulsed (the modem was off or in PSM)
    uint32_t ms;       // from the call to the first answer
    int tries;         // ATs sent
} sim7080_wake_t;

// UART, GPIOs and the esp_modem device bound to ppp_netif. Does not wake the
// modem. apn is the PDP context esp_modem names on PPP dial.
esp_err_t sim7080_start(const sim7080_config_t *cfg, const char *apn, esp_netif_t *ppp_netif);

// Asks AT every 100 ms for ask_ms (200 when the modem should be in PSM, so
// a modem that is on is never pulsed off: 1500 otherwise); without an answer
// pulses PWRKEY and asks for up to answer_ms more. ESP_ERR_TIMEOUT when it
// never answers.
esp_err_t sim7080_wake(uint32_t ask_ms, uint32_t answer_ms, sim7080_wake_t *info);

// One AT command; *answer (may be NULL) the modem's text until pass ("OK"
// when NULL) or ERROR, valid until the next call. ESP_FAIL on ERROR,
// ESP_ERR_TIMEOUT without either.
esp_err_t sim7080_at(const char *cmd, const char *pass, uint32_t timeout_ms, const char **answer);

// The full attach of sim7080_attach_script, then the network lock and the
// PSM request (each retried for a few seconds while the SIM comes up), then
// +CEREG until registered or limit_ms after CFUN=1. *grant the network's PSM
// timers, *ms the time from CFUN=1 to registration. ESP_ERR_TIMEOUT when not
// registered in time; ESP_ERR_INVALID_ARG for a bad setting.
esp_err_t sim7080_attach(const sim7080_attach_cfg_t *cfg, uint32_t limit_ms, sim7080_cereg_t *grant,
                         uint32_t *ms);

// After a PSM wake: asks +CEREG for up to window_ms (the modem answers AT a
// moment before it reports its kept registration). ESP_ERR_NOT_FOUND when it
// has none, so a full attach is next.
esp_err_t sim7080_resume(uint32_t window_ms, uint32_t *ms);

esp_err_t sim7080_csq(int *rssi);
esp_err_t sim7080_network(char *mcc_mnc, size_t size);    // AT+COPS=3,2 then AT+COPS?
esp_err_t sim7080_firmware(char *rev, size_t size);
esp_err_t sim7080_network_time(int64_t *unix_s);          // ESP_ERR_NOT_FOUND before the network set it

// GNSS shares the RF path with LTE: gnss_on turns the radio off (CFUN=0)
// first; the next sim7080_attach turns it on again.
esp_err_t sim7080_gnss_on(void);
esp_err_t sim7080_gnss_read(sim7080_gnss_t *fix);
esp_err_t sim7080_gnss_off(void);

// PPP: data mode (the netif dials) and back to command mode.
esp_err_t sim7080_ppp_up(void);
esp_err_t sim7080_ppp_down(void);

// AT+CPOWD=1: the modem powers down; the next send attaches cold. The app
// switches its supply rail off after this.
esp_err_t sim7080_power_off(void);

// Before deep sleep: PWRKEY held low, so the modem stays in PSM or off.
void sim7080_sleep_hold(void);

// Destroys the esp_modem device; the PPP netif stays the app's.
esp_err_t sim7080_stop(void);
