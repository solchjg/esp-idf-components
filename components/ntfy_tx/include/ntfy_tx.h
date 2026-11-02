// ntfy_tx: publish one message to an ntfy server (self-hosted or ntfy.sh) with
// one HTTP POST, over plain HTTP or HTTPS. HTTPS verifies the server
// certificate against the ESP-IDF CA bundle and the host name. Blocks the
// calling task until the server answers or the request fails; it never
// retries. Needs a network that is up.
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "ntfy_tx_core.h"

typedef struct {
    const char *url;      // server base, e.g. "http://192.168.2.37:8080"
    const char *topic;
    const char *token;    // NULL or empty: no Authorization header
    uint32_t timeout_ms;  // per network operation (connect, send, each read)
} ntfy_tx_server_t;

// ESP_OK once the server answered 2xx; otherwise ESP_FAIL, ESP_ERR_TIMEOUT,
// ESP_ERR_INVALID_ARG (a URL, topic or token that is not valid) or
// ESP_ERR_NO_MEM, with the cause in *why (NT_OK on success).
esp_err_t ntfy_tx_send(const ntfy_tx_server_t *server, const nt_msg_t *msg, nt_reason_t *why);
