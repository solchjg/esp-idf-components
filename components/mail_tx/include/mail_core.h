// Plain-text email over SMTP (RFC 5321, RFC 5322), pure core: the message
// text, Base64, and the client side of the SMTP dialogue as a state machine
// fed one server reply line at a time. No ESP-IDF headers; the transport
// (mail_tx.c) moves the bytes and runs TLS.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Standard Base64 of in[0, len) into out, terminated. Returns the encoded
// length, or 0 if it does not fit in cap (out then empty).
size_t mc_b64(const void *in, size_t len, char *out, size_t cap);

// The DATA payload: headers (Date, From, To, Subject, MIME plain text UTF-8),
// a blank line, the body with every line ending CRLF and lines starting with
// "." doubled, and the closing "\r\n.\r\n". `date` is an RFC 5322 date, e.g.
// "Fri, 30 Oct 2026 07:00:12 -0400". Returns the length, or 0 if it does not
// fit in cap.
size_t mc_message(char *out, size_t cap, const char *date, const char *from, const char *to,
                  const char *subject, const char *body);

typedef enum {
    MC_WAIT,      // reply not complete yet (a "250-" continuation line); feed the next line
    MC_SEND,      // send the command in mc_smtp_t.out
    MC_SEND_MSG,  // send the message given to mc_init
    MC_STARTTLS,  // run the TLS handshake on the open connection, then call mc_tls_up
    MC_DONE,      // the message was accepted: send mc_smtp_t.out (QUIT), then close
    MC_FAIL,      // see mc_smtp_t.err and .code; close the connection
} mc_action_t;

typedef enum {
    MC_E_NONE,
    MC_E_AUTH,     // the server refused the login (or offers neither PLAIN nor LOGIN)
    MC_E_REFUSED,  // a reply other than the one expected at any other step
    MC_E_PROTO,    // a line that is not an SMTP reply, or no STARTTLS offered on 587
} mc_err_t;

#define MC_LINE_MAX 512  // RFC 5321 §4.5.3.1.5: a reply line, CRLF included
#define MC_ADDR_MAX 64

typedef struct {
    int state;
    bool starttls;  // port 587: upgrade after the first EHLO
    bool plain, login;  // AUTH methods the last EHLO offered
    bool tls_offered;
    const char *user, *password, *from, *to, *helo;
    const char *msg;  // mc_message's text
    size_t msg_len;
    mc_err_t err;
    int code;  // the reply code that failed, 0 for none
    char out[MC_LINE_MAX];  // the next command, CRLF included
} mc_smtp_t;

// Start a session. All strings must outlive it. `helo` names this client in
// EHLO (e.g. the device's hostname).
void mc_init(mc_smtp_t *s, bool starttls, const char *helo, const char *user,
             const char *password, const char *from, const char *to, const char *msg,
             size_t msg_len);

// One server reply line, without its CRLF. Returns what the transport does next.
mc_action_t mc_line(mc_smtp_t *s, const char *line);

// After the STARTTLS handshake succeeded: the second EHLO.
mc_action_t mc_tls_up(mc_smtp_t *s);
