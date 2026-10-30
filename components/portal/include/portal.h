// Provisioning Portal (FSD §13): the HTTP side of PROVISIONING. The SoftAP itself
// is started by the composition root; this serves the form on its port 80.
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "portal_core.h"

// Called from the HTTP server task after a valid submission has been answered 200.
typedef void (*portal_submit_cb_t)(const pc_creds_t *creds);

// The app's own fields on the same form (optional). `form_html` is inserted
// before the submit button. `check` runs on the raw body after ssid and
// password passed (FR-13.4): it returns NULL to accept the submission (the app
// keeps what it parsed, for its submit callback) or the name of the first
// invalid field, which is answered 400 like ssid and password.
typedef const char *(*portal_check_cb_t)(const char *body, size_t len);

typedef struct {
    const char *form_html;
    portal_check_cb_t check;
} portal_extra_t;

esp_err_t portal_start(portal_submit_cb_t on_submit);

// portal_start with the app's fields; `extra` must outlive the portal.
esp_err_t portal_start_extra(portal_submit_cb_t on_submit, const portal_extra_t *extra);
void portal_stop(void);
