// SIMCom SIM7670G LTE Cat-1 modem over esp_modem, on a board that switches
// the modem's supply from a GPIO and holds PWRKEY low itself (the Waveshare
// ESP32-S3-SIM7670G-4G with its 4G switch off): the supply on and the first
// answer, the attach (APN, network lock, registration), the network's time,
// the modem's GNSS, the switch between AT commands and PPP, and AT+CPOF with
// the supply cut. Cat-1 has no power saving mode, so every send attaches
// afresh. Board-agnostic: pins and the APN come from sim7670_config_t. The PPP
// netif is the app's (esp_netif_new(ESP_NETIF_DEFAULT_PPP())), and so are the
// IP events: the app waits for IP_EVENT_PPP_GOT_IP after sim7670_ppp_up. One
// modem per firmware (the driver keeps a single instance).
//
// Every call reports its esp_err_t; none retries a failed send or aborts:
// what happens on failure is the caller's decision.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "sim7670_core.h"

typedef struct {
    int uart_port;      // e.g. 1
    int rx_gpio, tx_gpio;  // the ESP32-S3's side: RX takes the modem's TXD
    int baud;           // 115200
    int supply_gpio;    // high switches the modem's supply on
    int dtr_gpio;       // held low (UART awake); -1 when not wired
} sim7670_config_t;

// UART, GPIOs and the esp_modem device bound to ppp_netif. The supply is
// driven low (the modem off, as sim7670_power_off leaves it). apn is the
// PDP context esp_modem names on PPP dial.
esp_err_t sim7670_start(const sim7670_config_t *cfg, const char *apn, esp_netif_t *ppp_netif);

// The supply on, then AT every 200 ms for up to answer_ms; *ms (may be NULL)
// from the supply to the first answer (about 2.1 s on SLOT2). The modem's
// start-up draw can drop a USB console on the same board for a moment.
// ESP_ERR_TIMEOUT when it never answers; the supply stays on for the caller
// to cut with sim7670_power_off.
esp_err_t sim7670_power_on(uint32_t answer_ms, uint32_t *ms);

// One AT command; *answer (may be NULL) the modem's text until pass ("OK"
// when NULL) or ERROR, valid until the next call. ESP_FAIL on ERROR,
// ESP_ERR_TIMEOUT without either.
esp_err_t sim7670_at(const char *cmd, const char *pass, uint32_t timeout_ms, const char **answer);

// sim7670_attach_script, then the network lock (retried for a few seconds
// while the SIM comes up), then +CEREG until registered or limit_ms after the
// first command. *ms the time to registration. ESP_ERR_TIMEOUT when not
// registered in time; ESP_ERR_INVALID_ARG for a bad setting.
esp_err_t sim7670_attach(const sim7670_attach_cfg_t *cfg, uint32_t limit_ms, uint32_t *ms);

esp_err_t sim7670_csq(int *rssi);
esp_err_t sim7670_network(char *mcc_mnc, size_t size);  // AT+COPS=3,2 then AT+COPS?
esp_err_t sim7670_firmware(char *rev, size_t size);
esp_err_t sim7670_network_time(int64_t *unix_s);        // ESP_ERR_NOT_FOUND before the network set it

// GNSS: AT+CGNSSPWR=1 and its READY report (up to 30 s). With radio_off the
// LTE radio is turned off first (AT+CFUN=4) and on again by sim7670_gnss_off.
esp_err_t sim7670_gnss_on(bool radio_off);
esp_err_t sim7670_gnss_read(sim7670_gnss_t *fix);
esp_err_t sim7670_gnss_off(void);

// PPP: data mode (the netif dials) and back to command mode.
esp_err_t sim7670_ppp_up(void);
esp_err_t sim7670_ppp_down(void);

// AT+CPOF (the modem shuts down; about 3.5 s until it stops answering on
// SLOT2), then the supply cut and held low through deep sleep. The supply is
// cut even when the modem does not answer; the AT result is returned.
esp_err_t sim7670_power_off(void);

// Destroys the esp_modem device; the PPP netif stays the app's.
esp_err_t sim7670_stop(void);
