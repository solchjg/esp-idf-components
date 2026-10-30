#include "mail_tx.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "mail_core.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"

typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    bool tls;
    int64_t deadline_us;
    char rx[MC_LINE_MAX + 1];
    size_t rx_len;
} conn_t;

static uint32_t left_ms(const conn_t *c)
{
    int64_t us = c->deadline_us - esp_timer_get_time();
    return us <= 0 ? 0 : (uint32_t)(us / 1000) + 1;
}

// TCP connect bounded by the deadline (a blocking connect can outlast it).
static mail_tx_result_t tcp_connect(conn_t *c, const char *host, uint16_t port)
{
    char service[6];
    snprintf(service, sizeof service, "%u", port);
    const struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, service, &hints, &res) != 0 || res == NULL) {
        return MAIL_TX_DNS;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return MAIL_TX_CONNECT;
    }
    c->net.fd = fd;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) {
        return MAIL_TX_CONNECT;
    }
    if (r != 0) {
        uint32_t ms = left_ms(c);
        struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = ms % 1000 * 1000};
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        r = select(fd + 1, NULL, &w, NULL, &tv);
        if (r == 0) {
            return MAIL_TX_TIMEOUT;
        }
        int err = 0;
        socklen_t len = sizeof err;
        if (r < 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            return MAIL_TX_CONNECT;
        }
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    return MAIL_TX_OK;
}

static mail_tx_result_t io_write(conn_t *c, const char *buf, size_t len)
{
    while (len > 0) {
        if (left_ms(c) == 0) {
            return MAIL_TX_TIMEOUT;
        }
        int r = c->tls ? mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf, len)
                       : mbedtls_net_send(&c->net, (const unsigned char *)buf, len);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (r <= 0) {
            return MAIL_TX_CONNECT;
        }
        buf += r;
        len -= (size_t)r;
    }
    return MAIL_TX_OK;
}

// Next reply line without its line end, into line (MC_LINE_MAX + 1 bytes).
static mail_tx_result_t read_line(conn_t *c, char *line)
{
    for (;;) {
        char *nl = memchr(c->rx, '\n', c->rx_len);
        if (nl != NULL) {
            size_t n = (size_t)(nl - c->rx);
            size_t keep = n > 0 && c->rx[n - 1] == '\r' ? n - 1 : n;
            memcpy(line, c->rx, keep);
            line[keep] = '\0';
            c->rx_len -= n + 1;
            memmove(c->rx, nl + 1, c->rx_len);
            return MAIL_TX_OK;
        }
        if (c->rx_len == MC_LINE_MAX) {
            return MAIL_TX_REFUSED;  // longer than any SMTP reply line
        }
        uint32_t ms = left_ms(c);
        if (ms == 0) {
            return MAIL_TX_TIMEOUT;
        }
        int r;
        unsigned char *dst = (unsigned char *)c->rx + c->rx_len;
        size_t room = MC_LINE_MAX - c->rx_len;
        if (c->tls) {
            mbedtls_ssl_conf_read_timeout(&c->conf, ms);
            r = mbedtls_ssl_read(&c->ssl, dst, room);
        } else {
            r = mbedtls_net_recv_timeout(&c->net, dst, room, ms);
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_TIMEOUT) {
            return MAIL_TX_TIMEOUT;
        }
        if (r <= 0) {
            return MAIL_TX_CONNECT;
        }
        c->rx_len += (size_t)r;
    }
}

static mail_tx_result_t tls_start(conn_t *c, const char *host)
{
    if (mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0 ||
        esp_crt_bundle_attach(&c->conf) != ESP_OK) {
        return MAIL_TX_TLS;
    }
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_read_timeout(&c->conf, left_ms(c));
    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0 || mbedtls_ssl_set_hostname(&c->ssl, host) != 0) {
        return MAIL_TX_TLS;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
    c->rx_len = 0;  // RFC 3207 §4: nothing sent before the handshake counts
    int r;
    while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (left_ms(c) == 0) {
                return MAIL_TX_TIMEOUT;
            }
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_TIMEOUT) {
            return MAIL_TX_TIMEOUT;
        }
        return r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? MAIL_TX_CERT : MAIL_TX_TLS;
    }
    c->tls = true;
    return MAIL_TX_OK;
}

static mail_tx_result_t dialogue(conn_t *c, const mail_tx_config_t *cfg, mc_smtp_t *s)
{
    char line[MC_LINE_MAX + 1];
    mc_init(s, cfg->starttls, cfg->helo, cfg->user, cfg->password, cfg->from, cfg->to, cfg->msg,
            cfg->msg_len);
    for (;;) {
        mail_tx_result_t r = read_line(c, line);
        if (r != MAIL_TX_OK) {
            return r;
        }
        mc_action_t a = mc_line(s, line);
        if (a == MC_STARTTLS) {
            r = tls_start(c, cfg->host);
            if (r != MAIL_TX_OK) {
                return r;
            }
            a = mc_tls_up(s);
        }
        switch (a) {
        case MC_WAIT:
            break;
        case MC_SEND:
            r = io_write(c, s->out, strlen(s->out));
            break;
        case MC_SEND_MSG:
            r = io_write(c, s->msg, s->msg_len);
            break;
        case MC_DONE:
            io_write(c, s->out, strlen(s->out));  // QUIT; the message is already accepted
            return MAIL_TX_OK;
        default:
            return s->err == MC_E_AUTH ? MAIL_TX_AUTH : MAIL_TX_REFUSED;
        }
        if (r != MAIL_TX_OK) {
            return r;
        }
    }
}

esp_err_t mail_tx_send(const mail_tx_config_t *cfg, mail_tx_result_t *result)
{
    static conn_t c;  // one send at a time; keeps the TLS state off the caller's stack
    static mc_smtp_t s;
    memset(&c, 0, sizeof c);
    c.deadline_us = esp_timer_get_time() + (int64_t)cfg->timeout_ms * 1000;
    mbedtls_net_init(&c.net);
    mbedtls_ssl_init(&c.ssl);
    mbedtls_ssl_config_init(&c.conf);

    mail_tx_result_t r = tcp_connect(&c, cfg->host, cfg->port);
    if (r == MAIL_TX_OK && !cfg->starttls) {
        r = tls_start(&c, cfg->host);
    }
    if (r == MAIL_TX_OK) {
        r = dialogue(&c, cfg, &s);
    }
    if (c.tls) {
        mbedtls_ssl_close_notify(&c.ssl);
    }
    mbedtls_ssl_free(&c.ssl);
    mbedtls_ssl_config_free(&c.conf);
    mbedtls_net_free(&c.net);
    memset(&s, 0, sizeof s);  // the Base64 login was in s.out
    *result = r;
    return r == MAIL_TX_OK ? ESP_OK : ESP_FAIL;
}

const char *mail_tx_result_name(mail_tx_result_t r)
{
    static const char *const names[] = {"ok", "dns", "connect", "tls", "cert", "auth", "refused",
                                        "timeout"};
    return (unsigned)r < sizeof names / sizeof names[0] ? names[r] : "?";
}
