#include "ntfy_tx.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_tls.h"

// Why a request that got no HTTP reply failed, from the TLS layer's last error
// (it also carries plain TCP's) and the client's own result.
static nt_reason_t no_reply(esp_http_client_handle_t client, esp_err_t err)
{
    int tls = 0, flags = 0;
    esp_http_client_get_and_clear_last_tls_error(client, &tls, &flags);
    switch (tls) {
    case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME:
        return NT_ERR_DNS;
    case ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET:
    case ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST:
        return NT_ERR_CONNECT;
    case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT:
        return NT_ERR_TIMEOUT;
    case ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED:
        return flags ? NT_ERR_CERT : NT_ERR_TLS;  // flags: the certificate check's findings
    case 0:
        break;
    default:
        return NT_ERR_TLS;
    }
    if (err == ESP_ERR_HTTP_EAGAIN || err == ESP_ERR_TIMEOUT) {
        return NT_ERR_TIMEOUT;
    }
    return err == ESP_ERR_HTTP_CONNECT ? NT_ERR_CONNECT : NT_ERR_REFUSED;
}

esp_err_t ntfy_tx_send(const ntfy_tx_server_t *server, const nt_msg_t *msg, nt_reason_t *why)
{
    static nt_req_t req;  // off the caller's stack
    *why = NT_ERR_REFUSED;
    if (!nt_build(server->url, server->topic, server->token, msg, &req)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_http_client_config_t cfg = {
        .url = req.endpoint,
        .method = HTTP_METHOD_POST,
        .timeout_ms = (int)server->timeout_ms,
        .disable_auto_redirect = true,
        .crt_bundle_attach = nt_url_https(server->url) ? esp_crt_bundle_attach : NULL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < req.headers; i++) {
        esp_http_client_set_header(client, req.header[i].name, req.header[i].value);
    }
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_post_field(client, req.body, (int)req.body_len);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        *why = nt_status(esp_http_client_get_status_code(client));
    } else {
        *why = no_reply(client, err);
    }
    esp_http_client_cleanup(client);
    if (*why == NT_OK) {
        return ESP_OK;
    }
    return *why == NT_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
}
