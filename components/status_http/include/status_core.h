// Status HTTP Server (FSD §14), pure rendering core. No ESP-IDF headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SC_PAGE_MAX 1536
#define SC_EXTRA_MAX 384  // the app's own HTML after the table, terminator included

// The six §14.2 fields; strings are not escaped by the caller.
typedef struct {
    const char *fw_version;
    uint32_t free_heap;
    int rssi_dbm;
    const char *ip;
    const char *hostname;
    uint32_t requests;  // FR-14.6, including the request being answered
    const char *extra_html;  // the app's markup placed after the table; NULL for none
} sc_status_t;

// FR-14.8: one JSON object with exactly the six keys.
void sc_render_json(const sc_status_t *st, char *out, size_t len);

// FR-14.7: an HTML page showing every field, and no uptime, then extra_html.
void sc_render_html(const sc_status_t *st, char *out, size_t len);
