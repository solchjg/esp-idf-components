// Notecard requests and answers, pure C: builds the JSON request lines the
// device sends and reads the fields it needs from the answers. No ESP-IDF
// headers, so tests/host compiles it with the host cc.
//
// Requests are one line of JSON ending in '\n', as the Notecard expects. Each
// builder returns the length written (without the terminating NUL), or 0 when
// an argument is invalid or the request does not fit in buf.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define NC_PRODUCT_MAX 64  // ProductUID length limit (FSD §33.3)

// {"req":"<name>"}: card.version, hub.get, card.transport, card.time,
// card.location, card.wireless, hub.sync, ... name: letters, digits, '.', '_'.
size_t nc_req(char *buf, size_t n, const char *name);

// hub.set with product, mode "periodic", outbound and inbound in minutes.
size_t nc_req_hub_set(char *buf, size_t n, const char *product, int outbound_min,
                      int inbound_min);

// card.transport with a method, e.g. "cell".
size_t nc_req_transport(char *buf, size_t n, const char *method);

// note.add to file (e.g. "tb.qo") with body, a JSON object written by the
// caller, and "sync":true when sync.
size_t nc_req_note_add(char *buf, size_t n, const char *file, const char *body_json, bool sync);

// card.location.mode with mode ("off", "continuous", "fixed", ...); lat and
// lon (degrees, 6 decimals) only when both are given, as "fixed" needs.
size_t nc_req_location_mode(char *buf, size_t n, const char *mode, const double *lat,
                            const double *lon);

// Finds the value at path in a JSON object answer: a key ("time") or keys
// joined by '.' ("net.bars"). *val points at its first character, *len is its
// length (a string's quotes included). False when absent or malformed.
bool nc_find(const char *json, const char *path, const char **val, size_t *len);

// A string value, unescaped for \" and \\ (other escapes kept as written),
// into out. False when absent, not a string, or longer than n - 1.
bool nc_str(const char *json, const char *path, char *out, size_t n);

// A number value. False when absent or not a number.
bool nc_num(const char *json, const char *path, double *out);

// True when the answer carries "err"; its text goes to out (may be NULL).
bool nc_err(const char *json, char *out, size_t n);

// True when an "err" answer says the Notecard can hold no more notes: {full},
// {file-storage-full}, {file-write-failure} or {note-max} in its text.
bool nc_err_full(const char *json);

// s as a JSON string, quotes included: '"' and '\\' escaped, \n \r \t as
// such, other control bytes as \u00XX, every other byte (UTF-8) as is. The
// length written, or 0 when it does not fit in buf with its NUL.
size_t nc_json_str(char *buf, size_t n, const char *s);

// FSD §33.3: 1 – 64 bytes of letters, digits, '.', ':', '-' and '_'.
bool nc_product_valid(const char *s);

// FR-33.4: true when a hub.get answer already holds product, mode
// "periodic", outbound and inbound, so no hub.set is needed.
bool nc_hub_matches(const char *hub_get, const char *product, int outbound_min,
                    int inbound_min);

// FR-33.14: true when a card.transport answer reports exactly method.
bool nc_transport_is(const char *answer, const char *method);
