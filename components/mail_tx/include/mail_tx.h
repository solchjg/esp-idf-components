// mail_tx: send one plain-text email to an SMTP server over TLS — implicit TLS
// on port 465, STARTTLS on any other port — verifying the server certificate
// against the ESP-IDF CA bundle and the host name, then logging in with AUTH
// PLAIN or LOGIN. Blocks the calling task until the server accepts the message
// or the session fails; it never retries. Needs CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
// (on by default) and a network that is up.
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "mail_tx_core.h"

typedef struct {
    const char *host;     // e.g. "smtp.gmail.com"
    uint16_t port;        // 465: TLS from the first byte; else plain, then STARTTLS
    uint32_t timeout_ms;  // the whole session: DNS, connect, TLS and every reply
} mail_tx_server_t;

// Send msg (its `starttls` is set from the port). ESP_OK once the server
// answered the message with 250; otherwise ESP_FAIL, ESP_ERR_TIMEOUT or
// ESP_ERR_NO_MEM, with the cause in *why (MT_OK on success).
esp_err_t mail_tx_send(const mail_tx_server_t *server, const mt_msg_t *msg, mt_reason_t *why);
