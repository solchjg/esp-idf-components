// Provisioning Portal (FSD §13): the HTTP side of PROVISIONING. The SoftAP itself
// is started by the composition root; this serves the form on its port 80.
#pragma once

#include "esp_err.h"
#include "portal_core.h"

// Called from the HTTP server task after a valid submission has been answered 200.
typedef void (*portal_submit_cb_t)(const pc_creds_t *creds);

// Fields an app adds to the form, e.g. settings it keeps beside the WiFi
// credentials. `html` goes into the form before the submit button. `take` runs
// in the HTTP server task for each submission whose ssid and password are
// valid, with the whole body (pc_field reads one field of it); it returns NULL
// to accept the submission, keeping what it needs, or the name of the first
// invalid field, answered 400 "invalid field: <name>" with on_submit not called.
typedef struct {
    const char *html;
    const char *(*take)(const char *body, size_t len);
} portal_extra_t;

// extra may be NULL (WiFi fields only); it must outlive the portal.
esp_err_t portal_start(portal_submit_cb_t on_submit);
esp_err_t portal_start_extra(portal_submit_cb_t on_submit, const portal_extra_t *extra);
void portal_stop(void);
