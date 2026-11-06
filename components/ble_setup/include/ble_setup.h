// BLE setup: one GATT service that carries an app's settings as a
// URL-encoded form body (the portal's format), over a link paired by LE Secure
// Connections with a passkey the app shows. NimBLE, peripheral role only, one
// central at a time. The app decides when it runs (start/stop) and what the
// settings mean (the callbacks). The component stores nothing but, with
// CONFIG_BLE_SETUP_BOND, the bonds NimBLE keeps (CONFIG_BT_NIMBLE_NVS_PERSIST
// for them to outlive a restart), and its own static random address (NVS
// namespace "ble_setup"), replaced whenever it holds no bond: after an erased
// flash, a factory reset or a `forget`, a phone that kept its half of an
// earlier pairing meets a new device and pairs afresh.
// Needs CONFIG_BT_NIMBLE_GATT_CLIENT=y (and so ROLE_CENTRAL=y): a phone's
// indications on connecting must be confirmed, or it drops the link at 30 s.
//
// Service 6e0a0001-7d3b-4c2a-9f1e-54445333424c, characteristics 6e0a0002..06:
//   info      read, open                 read_info()
//   settings  read/write, MITM-paired    write: append to the staged body;
//                                        read: a 512-byte page of read_settings(),
//                                        or of read_form() after "form=<offset>"
//   result    read/notify, MITM-paired   what on_apply() answered ("none" first)
//   control   write, MITM-paired         "apply", "done", "read=<offset>", "form=<offset>",
//                                        "hist=<n>", "range=<text>", "forget"
//                                        (erase every bond, this central's too),
//                                        "time=<ms since 1970 UTC>" (the page
//                                        sends the phone's clock on connecting)
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
    BLE_SETUP_EV_FORGOTTEN,    // `control` "forget": every bond erased
} ble_setup_event_t;

// Every callback runs in the NimBLE host task; keep them short and post to
// the app's own task. NULL is allowed for any but on_apply.
typedef struct {
    const char *name;  // complete local name in the scan response, e.g. TDS3-48C3A2
    // Fill out (at most cap bytes, no terminator needed); return the length.
    size_t (*read_info)(char *out, size_t cap);
    size_t (*read_settings)(char *out, size_t cap);  // never a secret
    // Optional: the rules the page checks each field against before it writes
    // (NULL: `control` "form=<offset>" is refused and the page checks nothing).
    // A form body with one `<name>=<rules>` per field read_settings shows (a
    // secret by its name, without "_set"); <rules> is itself a form body:
    //   label=<text>  required=1  type=email|time|number|password
    //   minlength= maxlength= min= max= step= pattern=<regex, whole value>
    //   placeholder=<text>  inputmode=<mode>  unit=<text after the box>
    //   options=<a,b>  (a choice; a leading empty item, ",a,b", allows none)
    //   labels=<text of a|text of b>  (what each option shows)
    //   extras=<x,y>  extra_labels=<text|text>  extras_intro=<text>
    //     (checkboxes at the end of the section; the value is the choice
    //     then the ticked extras, comma separated)
    //   group=<g>  need=1  (required once any field of group g is filled)
    //   section=<heading>  (a new section starts at this field)
    //   intro=<text>  (a paragraph before this field)  hint=<text>  (after it)
    // Fields show in read_settings order, so the page can match a portal form.
    // The device still validates every write; the rules only spare a round trip.
    size_t (*read_form)(char *out, size_t cap);
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
    // Optional: `control` "time=<ms>", the phone's clock in ms since 1970 UTC,
    // written by the page right after pairing (NULL refuses the write; the
    // page carries on).
    void (*on_time)(int64_t epoch_ms);
} ble_setup_config_t;

// Controller and host up, advertising. cfg must outlive the run.
// ESP_ERR_INVALID_STATE when already running.
esp_err_t ble_setup_start(const ble_setup_config_t *cfg);

// Drop any connection, stop advertising, deinit host and controller so their
// memory returns. Never call from a callback. ESP_OK when not running.
esp_err_t ble_setup_stop(void);

bool ble_setup_running(void);

// Forget every bonded central (a factory reset). Works whether or not BLE
// is running.
esp_err_t ble_setup_forget(void);
