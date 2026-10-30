// Send one plain-text email over SMTP with TLS: port 465 style (TLS from the
// first byte) or 587 style (STARTTLS). The server certificate is checked
// against the ESP-IDF CA bundle and the host name. Needs a network up.
//
// Every call reports its esp_err_t; none retries: whether to try again is the
// caller's decision. Blocks the calling task for at most cfg->timeout_ms.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    MAIL_TX_OK,
    MAIL_TX_DNS,      // the host name did not resolve
    MAIL_TX_CONNECT,  // no TCP connection, or it broke
    MAIL_TX_TLS,      // the TLS handshake failed for a reason other than the certificate
    MAIL_TX_CERT,     // the server certificate did not verify
    MAIL_TX_AUTH,     // the server refused the login
    MAIL_TX_REFUSED,  // the server refused another step (sender, recipient, message)
    MAIL_TX_TIMEOUT,  // the whole exchange took longer than timeout_ms
} mail_tx_result_t;

typedef struct {
    const char *host;
    uint16_t port;
    bool starttls;      // true: plain SMTP, then STARTTLS (587); false: TLS at once (465)
    const char *helo;   // this client's name in EHLO, e.g. the hostname
    const char *user, *password;
    const char *from, *to;  // envelope addresses (MAIL FROM, RCPT TO)
    const char *msg;        // mc_message's text (mail_core.h)
    size_t msg_len;
    uint32_t timeout_ms;    // from the start of the call to the final reply
} mail_tx_config_t;

// ESP_OK when the server accepted the message; ESP_FAIL otherwise, with the
// reason in *result (always set).
esp_err_t mail_tx_send(const mail_tx_config_t *cfg, mail_tx_result_t *result);

// Lower-case name of a result for log lines: "ok", "dns", "connect", "tls",
// "cert", "auth", "refused", "timeout".
const char *mail_tx_result_name(mail_tx_result_t r);
