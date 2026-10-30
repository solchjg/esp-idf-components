#include "mail_tx.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "lwip/sockets.h"

#define IMPLICIT_TLS_PORT 465
#define OUT_HEADROOM 1024  // headers and commands besides the body
#define IN_MAX 256

typedef struct {
    esp_tls_t *tls;
    bool secure;       // TLS is up
    int64_t deadline;  // esp_timer time the session must end by
} conn_t;

static int remaining_ms(const conn_t *c)
{
    int64_t ms = (c->deadline - esp_timer_get_time()) / 1000;
    return ms > 0 ? (int)ms : 0;
}

// Why a connect or handshake failed, from esp-tls's last error.
static mt_reason_t tls_reason(esp_tls_t *tls, bool handshake)
{
    esp_tls_error_handle_t h = NULL;
    int code = 0, flags = 0;
    esp_err_t last = ESP_FAIL;
    if (esp_tls_get_error_handle(tls, &h) == ESP_OK && h) {
        last = esp_tls_get_and_clear_last_error(h, &code, &flags);
    }
    if (flags != 0) {
        return MT_ERR_CERT;
    }
    switch (last) {
    case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME:
        return MT_ERR_DNS;
    case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT:
    case ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT:
        return MT_ERR_TIMEOUT;
    case ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET:
    case ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST:
    case ESP_ERR_ESP_TLS_SOCKET_SETOPT_FAILED:
    case ESP_ERR_ESP_TLS_UNSUPPORTED_PROTOCOL_FAMILY:
        return handshake ? MT_ERR_TLS : MT_ERR_CONNECT;
    default:
        return MT_ERR_TLS;
    }
}

// Reads and sends block at most until the deadline.
static void arm_timeout(const conn_t *c)
{
    int fd;
    if (esp_tls_get_conn_sockfd(c->tls, &fd) != ESP_OK || fd < 0) {
        return;
    }
    int ms = remaining_ms(c);
    struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000 + (ms ? 0 : 1)};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

static mt_reason_t send_all(conn_t *c, const char *p, size_t len)
{
    while (len > 0) {
        if (remaining_ms(c) == 0) {
            return MT_ERR_TIMEOUT;
        }
        arm_timeout(c);
        ssize_t n = esp_tls_conn_write(c->tls, p, len);
        if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (n <= 0) {
            return remaining_ms(c) == 0 ? MT_ERR_TIMEOUT
                                        : c->secure ? MT_ERR_TLS : MT_ERR_CONNECT;
        }
        p += n;
        len -= (size_t)n;
    }
    return MT_OK;
}

// Connect plain or with TLS, verifying the certificate (FSD §25 FR-25.5).
static int tls_connect(conn_t *c, const char *host, uint16_t port, bool plain)
{
    esp_tls_cfg_t cfg = {
        .timeout_ms = remaining_ms(c),
        .is_plain_tcp = plain,
        .crt_bundle_attach = plain ? NULL : esp_crt_bundle_attach,
    };
    if (cfg.timeout_ms == 0) {
        return -1;
    }
    return esp_tls_conn_new_sync(host, (int)strlen(host), port, &cfg, c->tls);
}

static mt_reason_t session(conn_t *c, const mail_tx_server_t *server, mt_msg_t *m,
                           char *out, size_t cap)
{
    if (tls_connect(c, server->host, server->port, m->starttls) != 1) {
        return remaining_ms(c) == 0 ? MT_ERR_TIMEOUT : tls_reason(c->tls, false);
    }
    c->secure = !m->starttls;
    mt_t s;
    mt_begin(&s, m);
    char in[IN_MAX];
    size_t in_len = 0;
    for (;;) {
        size_t used = 0, out_len = 0;
        mt_act_t act = MT_NEED_MORE;
        if (in_len > 0) {
            act = mt_feed(&s, in, in_len, &used, out, cap, &out_len);
            memmove(in, in + used, in_len - used);
            in_len -= used;
        }
        switch (act) {
        case MT_NEED_MORE: {
            if (remaining_ms(c) == 0) {
                return MT_ERR_TIMEOUT;
            }
            arm_timeout(c);
            ssize_t n = esp_tls_conn_read(c->tls, in + in_len, sizeof in - in_len);
            if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE ||
                (n < 0 && remaining_ms(c) > 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                continue;
            }
            if (n < 0) {
                return remaining_ms(c) == 0 ? MT_ERR_TIMEOUT
                                            : c->secure ? MT_ERR_TLS : MT_ERR_CONNECT;
            }
            if (n == 0) {
                return MT_ERR_REFUSED;  // the server closed the connection
            }
            in_len += (size_t)n;
            break;
        }
        case MT_SEND: {
            mt_reason_t r = send_all(c, out, out_len);
            if (r != MT_OK) {
                return r;
            }
            break;
        }
        case MT_START_TLS: {
            // RFC 3207: the same connection carries the handshake; nothing read
            // before it counts afterwards.
            in_len = 0;
            arm_timeout(c);
            if (esp_tls_set_conn_state(c->tls, ESP_TLS_CONNECTING) != ESP_OK ||
                tls_connect(c, server->host, server->port, false) != 1) {
                return remaining_ms(c) == 0 ? MT_ERR_TIMEOUT : tls_reason(c->tls, true);
            }
            c->secure = true;
            act = mt_tls_ready(&s, out, cap, &out_len);
            mt_reason_t r = act == MT_SEND ? send_all(c, out, out_len) : s.reason;
            if (r != MT_OK) {
                return r;
            }
            break;
        }
        case MT_DONE:
            if (out_len > 0) {
                send_all(c, out, out_len);  // QUIT; the message is already accepted
            }
            return MT_OK;
        case MT_FAILED:
            return s.reason;
        }
    }
}

esp_err_t mail_tx_send(const mail_tx_server_t *server, const mt_msg_t *msg, mt_reason_t *why)
{
    *why = MT_ERR_CONNECT;
    mt_msg_t m = *msg;
    m.starttls = server->port != IMPLICIT_TLS_PORT;
    size_t cap = 2 * strlen(m.body ? m.body : "") + OUT_HEADROOM;
    char *out = malloc(cap);
    conn_t c = {.tls = esp_tls_init(),
                .deadline = esp_timer_get_time() + (int64_t)server->timeout_ms * 1000};
    if (!out || !c.tls) {
        free(out);
        if (c.tls) {
            esp_tls_conn_destroy(c.tls);
        }
        return ESP_ERR_NO_MEM;
    }
    *why = session(&c, server, &m, out, cap);
    memset(out, 0, cap);  // AUTH lines held the password
    free(out);
    esp_tls_conn_destroy(c.tls);
    return *why == MT_OK ? ESP_OK : *why == MT_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
}
