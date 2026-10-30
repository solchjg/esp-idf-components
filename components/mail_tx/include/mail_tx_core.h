// mail_tx, pure core: the client side of one SMTP submission (RFC 5321, AUTH
// per RFC 4954, STARTTLS per RFC 3207) and the plain-text message it carries.
// It reads the server's replies and says what to send next; it performs no I/O.
// No ESP-IDF headers, so the host tier compiles it and drives it with scripted
// replies.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MT_LINE_MAX 128  // reply text kept per line; the rest of a longer line is ignored

// Why a submission failed. MT_OK only for a message the server accepted.
typedef enum {
    MT_OK = 0,
    MT_ERR_DNS,      // the host name did not resolve
    MT_ERR_CONNECT,  // no TCP connection
    MT_ERR_TLS,      // TLS failed, or the server offers no STARTTLS
    MT_ERR_CERT,     // the server certificate did not verify
    MT_ERR_AUTH,     // login refused, or no AUTH mechanism this core speaks
    MT_ERR_REFUSED,  // any other unexpected reply, or the connection closed
    MT_ERR_TIMEOUT,  // no final reply in time
} mt_reason_t;

// "ok", "dns", "connect", "tls", "cert", "auth", "refused", "timeout".
const char *mt_reason_name(mt_reason_t r);

// One message. Every string must outlive the session. CR and LF in the header
// fields and addresses are sent as spaces. `date` (RFC 5322, e.g.
// "Fri, 30 Oct 2026 07:00:00 -0400") may be NULL to leave the header out.
typedef struct {
    const char *helo;      // EHLO name, e.g. the device's host name
    const char *user;      // login
    const char *password;
    const char *from;      // envelope and header sender
    const char *to;        // the one recipient
    const char *subject;
    const char *date;
    const char *body;      // plain text, LF or CRLF line ends
    bool starttls;         // plain connection, upgraded by STARTTLS before AUTH
} mt_msg_t;

typedef enum {
    MT_NEED_MORE,  // the reply is not complete: read more and feed it
    MT_SEND,       // send the out bytes, then read the next reply
    MT_START_TLS,  // run the TLS handshake on the connection, then call mt_tls_ready
    MT_DONE,       // accepted: send the out bytes (QUIT) if you like, then close
    MT_FAILED,     // see mt_t.reason; close
} mt_act_t;

typedef struct {
    const mt_msg_t *msg;
    int state;
    bool tls_up;
    bool cap_starttls, cap_plain, cap_login;  // from the last EHLO reply
    char line[MT_LINE_MAX];
    size_t line_len;
    int code;            // of the reply being read, 0 before its first line
    mt_reason_t reason;  // set on MT_FAILED
} mt_t;

// Start a session on a fresh connection (TLS already up unless msg->starttls).
void mt_begin(mt_t *s, const mt_msg_t *msg);

// Feed `len` received bytes. Stops at the end of the first complete reply;
// *used tells how many bytes were consumed, so feed the rest afterwards. On
// MT_SEND and MT_DONE, out holds *out_len bytes (cap must hold the whole
// command, or the whole message for DATA; if not, the session fails with
// MT_ERR_REFUSED).
mt_act_t mt_feed(mt_t *s, const char *in, size_t len, size_t *used,
                 char *out, size_t cap, size_t *out_len);

// After MT_START_TLS, once the handshake succeeded: the EHLO to send over TLS.
mt_act_t mt_tls_ready(mt_t *s, char *out, size_t cap, size_t *out_len);

// RFC 4648 base64 of `len` bytes; returns the encoded length, or 0 if it does
// not fit `cap` with its terminator.
size_t mt_base64(const unsigned char *in, size_t len, char *out, size_t cap);

// The message as sent after DATA: headers, a blank line, the body with CRLF
// line ends and leading dots doubled, then the terminating ".\r\n". Returns
// its length, or 0 if it does not fit `cap`.
size_t mt_message(const mt_msg_t *m, char *out, size_t cap);
