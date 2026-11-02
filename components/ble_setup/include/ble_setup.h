// BLE setup: one GATT service that carries an app's settings as a
// URL-encoded form body (the portal's format), over a link paired by LE Secure
// Connections with a passkey the app shows. NimBLE, peripheral role only, one
// central at a time. The app decides when it runs (start/stop) and what the
// settings mean (the callbacks); the component never stores anything.
// Needs CONFIG_BT_NIMBLE_GATT_CLIENT=y (and so ROLE_CENTRAL=y): a phone's
// indications on connecting must be confirmed, or it drops the link at 30 s.
//
// Service 6e0a0001-7d3b-4c2a-9f1e-54445333424c, characteristics 6e0a0002..06:
//   info      read, open                 read_info()
//   settings  read/write, MITM-paired    write: append to the staged body;
//                                        read: a 512-byte page of read_settings()
//   result    read/notify, MITM-paired   what on_apply() answered ("none" first)
//   control   write, MITM-paired         "apply", "done", "read=<offset>",
//                                        "hist=<n>", "range=<text>"
//   history   read, MITM-paired          read_history() from line n (0 on connecting)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    BLE_SETUP_EV_CONNECTED,
    BLE_SETUP_EV_DISCONNECTED,
    BLE_SETUP_EV_PAIRED,       // link encrypted with an authenticated key
    BLE_SETUP_EV_PAIR_FAILED,  // pairing or encryption failed
    BLE_SETUP_EV_DONE,         // `control` written with "done"
} ble_setup_event_t;

// Every callback runs in the NimBLE host task; keep them short and post to
// the app's own task. NULL is allowed for any but on_apply.
typedef struct {
    const char *name;  // complete local name in the scan response, e.g. TDS3-48C3A2
    // Fill out (at most cap bytes, no terminator needed); return the length.
    size_t (*read_info)(char *out, size_t cap);
    size_t (*read_settings)(char *out, size_t cap);  // never a secret
    // The staged body (terminated) on "apply": validate it, write the answer
    // ("ok" or "invalid=<field>") into result (cap bytes, terminated).
    void (*on_apply)(const char *body, size_t len, char *result, size_t cap);
    // A passkey to show (shown true) or to take down (shown false).
    void (*on_passkey)(uint32_t passkey, bool shown);
    void (*on_event)(ble_setup_event_t ev);
    // Optional: an app's log of lines (for example a history of readings).
    // Fill out with as many whole lines from line `line` (0 the first) as fit
    // in cap bytes; 0 past the end. A read may be repeated for the same line.
    size_t (*read_history)(uint32_t line, char *out, size_t cap);
    // `control` "range=<text>": the filter later history reads apply; false
    // refuses the write. The text is not terminated.
    bool (*on_history_range)(const char *text, size_t len);
} ble_setup_config_t;

// Controller and host up, advertising. cfg must outlive the run.
// ESP_ERR_INVALID_STATE when already running.
esp_err_t ble_setup_start(const ble_setup_config_t *cfg);

// Drop any connection, stop advertising, deinit host and controller so their
// memory returns. Never call from a callback. ESP_OK when not running.
esp_err_t ble_setup_stop(void);

bool ble_setup_running(void);
