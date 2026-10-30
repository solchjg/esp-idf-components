// Provisioning Portal (FSD §13), pure form core. No ESP-IDF headers.
#pragma once

#include <stddef.h>

#define PC_SSID_MAX 32      // FR-13.4
#define PC_PASSWORD_MAX 63  // FR-13.4

typedef struct {
    char ssid[PC_SSID_MAX + 1];
    char password[PC_PASSWORD_MAX + 1];
} pc_creds_t;

typedef enum {
    PC_OK = 0,
    PC_BAD_SSID = 1,      // missing, empty or longer than 32 bytes
    PC_BAD_PASSWORD = 2,  // missing, 1 – 7 or more than 63 bytes
} pc_result_t;

// Decode an application/x-www-form-urlencoded body with fields `ssid` and
// `password` (any order, '+' and %XX decoded) and validate them (FR-13.4).
pc_result_t pc_parse_form(const char *body, size_t len, pc_creds_t *out);

// One field of an application/x-www-form-urlencoded body, decoded into out
// (at most cap bytes plus the terminator). Returns its decoded length; -1 if
// the field is absent; -2 if it is longer than cap or holds a malformed escape
// (out is then empty). For forms with fields beyond ssid and password.
int pc_form_field(const char *body, size_t len, const char *name, char *out, size_t cap);

// FR-13.7: copy `in` to `out` with & < > " ' escaped; cut to fit `cap`.
void pc_html_escape(const char *in, char *out, size_t cap);
