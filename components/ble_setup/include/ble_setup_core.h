// BLE setup, pure core: the staged settings body, the control commands, the
// read pages and the passkey text. No ESP-IDF headers, so the host tier
// compiles it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The ATT protocol caps one attribute value at 512 bytes, so a longer body is
// written in several writes and read in pages.
#define BS_ATTR_MAX 512

// The body being written: appended to by each `settings` write, handed to the
// app by a `control` write of `apply`, cleared then and on every connection.
typedef struct {
    char *buf;       // cap + 1 bytes, owned by the caller
    size_t cap;
    size_t len;
    uint32_t read_off;  // start of the page `settings` reads return
    bool read_form;     // `settings` reads return the form description, not the settings
    uint32_t hist_line;  // first line `history` reads return; 0 on each connection
} bs_session_t;

void bs_init(bs_session_t *s, char *buf, size_t cap);
void bs_reset(bs_session_t *s);  // empty body, settings read page 0, history line 0

// Append one write. False, with the body emptied, when it would pass cap.
bool bs_append(bs_session_t *s, const void *data, size_t len);

typedef enum {
    BS_CTRL_BAD = 0,  // anything else: answered Value Not Allowed
    BS_CTRL_APPLY,    // `apply`: validate and act on the staged body
    BS_CTRL_DONE,     // `done`: the person has finished
    BS_CTRL_READ,     // `read=<offset>`: later reads start at offset
    BS_CTRL_HIST,     // `hist=<n>`: later `history` reads start at line n
    BS_CTRL_RANGE,    // `range=<text>`: the app's filter for `history`
    BS_CTRL_FORGET,   // `forget`: erase every bond
    BS_CTRL_FORM,     // `form=<offset>`: later `settings` reads page the form description
    BS_CTRL_TIME,     // `time=<ms since 1970 UTC>`: the phone's clock, for the app
} bs_ctrl_t;

// Parse a `control` write. *arg gets the number of BS_CTRL_READ, BS_CTRL_FORM
// and BS_CTRL_HIST, and for BS_CTRL_RANGE and BS_CTRL_TIME the offset in v
// where <text> or the number begins.
bs_ctrl_t bs_control(const char *v, size_t len, uint32_t *arg);

// The number of a BS_CTRL_TIME write (1 – 15 digits, as bs_control checked).
int64_t bs_time_ms(const char *v, size_t len);

// The page of `all` that a `settings` read returns: at most BS_ATTR_MAX bytes
// from off (none past the end). Returns its length; *start points into all.
size_t bs_page(const char *all, size_t all_len, uint32_t off, const char **start);

// A passkey (0 – 999999) as six digits, zero-padded.
void bs_passkey_text(uint32_t passkey, char out[7]);

// The device's address (little-endian, byte 5 the most significant): a static
// random address, kept while the device holds a bond and replaced whenever it
// holds none, so a central that kept a pairing the device no longer has (an
// erased flash, a factory reset, a `forget`) meets a new device and pairs afresh instead of presenting a stale key.
// bs_addr_static() turns random bytes into a static random address in place;
// false when the 46 random bits are all 0 or all 1 (draw again).
bool bs_addr_static(uint8_t addr[6]);

// Whether the stored address (stored: one was read and is valid) is used
// again: only while a bond is held. Otherwise a new one is drawn and stored.
bool bs_addr_keep(bool stored, int bonds);
