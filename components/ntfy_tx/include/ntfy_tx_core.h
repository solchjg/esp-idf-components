// ntfy_tx, pure core: one ntfy publish (https://docs.ntfy.sh/publish/) as a
// POST to "<server>/<topic>" with the message in the body and the optional
// fields in headers. It checks the server URL, topic and token, builds the
// request, and reads the reply's HTTP status; it performs no I/O. No ESP-IDF
// headers, so the host tier compiles it.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define NT_URL_MIN 10    // "http://a:1" is the shortest useful URL
#define NT_URL_MAX 96
#define NT_TOPIC_MAX 64  // ntfy's own limit
#define NT_TOKEN_MAX 64
#define NT_TITLE_MAX 96  // kept per header value; longer text is cut
#define NT_TAGS_MAX 64
#define NT_CLICK_MAX 96
#define NT_BODY_MAX 384  // longer bodies are cut, so a request stays small
#define NT_HEADERS 5     // Title, Priority, Tags, Click, Authorization
#define NT_VALUE_MAX (NT_TITLE_MAX + 1)
#define NT_ENDPOINT_MAX (NT_URL_MAX + 1 + NT_TOPIC_MAX)

// Why a publish failed. NT_OK only for a 2xx reply.
typedef enum {
    NT_OK = 0,
    NT_ERR_DNS,      // the host name did not resolve
    NT_ERR_CONNECT,  // no TCP connection
    NT_ERR_TLS,      // the TLS handshake failed
    NT_ERR_CERT,     // the server certificate did not verify
    NT_ERR_AUTH,     // 401 or 403: token missing, wrong, or not allowed to publish
    NT_ERR_REFUSED,  // any other non-2xx reply, a cut reply, or a request that cannot be built
    NT_ERR_LIMIT,    // 429: the server's rate limit
    NT_ERR_SERVER,   // 5xx
    NT_ERR_TIMEOUT,  // no reply in time
} nt_reason_t;

// "ok", "dns", "connect", "tls", "cert", "auth", "refused", "limit", "server", "timeout".
const char *nt_reason_name(nt_reason_t r);

// A server base URL: "http://" or "https://", a host of letters, digits, '.'
// and '-', an optional ":port" (1 - 65535), nothing after; NT_URL_MIN to
// NT_URL_MAX bytes.
bool nt_url_valid(const char *url);
bool nt_url_https(const char *url);  // the URL starts with "https://"

// A topic: 1 - NT_TOPIC_MAX characters from A-Z a-z 0-9 _ -.
bool nt_topic_valid(const char *topic);

// An access token: 1 - NT_TOKEN_MAX printable ASCII characters, no space.
bool nt_token_valid(const char *token);

// One message. Every field but body may be NULL or empty to leave it out;
// priority 1 - 5, 0 to leave it out (ntfy's default is 3).
typedef struct {
    const char *title;
    const char *body;
    int priority;
    const char *tags;   // comma-separated emoji names or tags, e.g. "warning"
    const char *click;  // URL opened when the notification is tapped
} nt_msg_t;

typedef struct {
    const char *name;
    char value[NT_VALUE_MAX];
} nt_header_t;

// The request to send: POST endpoint, these headers, then body_len bytes of body.
typedef struct {
    char endpoint[NT_ENDPOINT_MAX + 1];
    int headers;
    nt_header_t header[NT_HEADERS];
    const char *body;
    size_t body_len;
} nt_req_t;

// Build the request for msg. token may be NULL or empty for none. Header values
// are single-line printable ASCII (control bytes become spaces, others '?') and
// cut to fit; the body is cut at NT_BODY_MAX. False when the URL, topic or token
// is not valid.
bool nt_build(const char *url, const char *topic, const char *token, const nt_msg_t *msg,
              nt_req_t *out);

// The outcome of a reply with this HTTP status.
nt_reason_t nt_status(int status);
