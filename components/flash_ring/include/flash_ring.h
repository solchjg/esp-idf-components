// flash_ring: an append-only log of fixed-size records in a data partition. When
// full it reuses its oldest sector. A torn record is skipped, never fatal.
//
// Hold a flash_ring_t per ring. Every call returns its esp_err_t and never retries
// or aborts. Point `saved` at RTC memory to skip the scan on a deep-sleep wake.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"
#include "flash_ring_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    uint32_t magic;
    fr_pos_t pos;
} flash_ring_saved_t;

typedef struct {
    const char *label;     // the partition; also the key of its clear mark
    size_t payload_size;   // at most FR_RECORD_MAX - FR_TRAILER_SIZE
    flash_ring_saved_t *saved;  // kept across deep sleep; caller-owned, never NULL
    // Private.
    const esp_partition_t *part;
    fr_geom_t geom;
    SemaphoreHandle_t lock;
} flash_ring_t;

// ESP_ERR_NOT_FOUND: no such partition (for example a table older than the ring).
esp_err_t flash_ring_open(flash_ring_t *r);
esp_err_t flash_ring_append(flash_ring_t *r, const void *payload);

// Erases every sector in use; `progress` runs after each. A power loss before the
// end leaves a mark in NVS that flash_ring_clear_pending sees on the next boot.
esp_err_t flash_ring_clear(flash_ring_t *r, void (*progress)(void));
bool flash_ring_clear_pending(const flash_ring_t *r);

// Reads oldest first. Each call takes the lock for one record only.
esp_err_t flash_ring_begin(flash_ring_t *r, fr_cursor_t *c);
bool flash_ring_next(flash_ring_t *r, fr_cursor_t *c, void *payload);
