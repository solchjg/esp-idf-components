// Pure core of flash_ring: fixed-size records in a ring of 4 KB sectors, finding
// the end after a boot and reading oldest first. No ESP-IDF headers.
//
// A record is the caller's payload, then a sequence number and a checksum. Writes
// only append; a sector is erased when the ring comes round to it. Erased flash
// reads as all 0xFF, which never passes the checksum.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FR_SECTOR_SIZE 4096
#define FR_TRAILER_SIZE 8
#define FR_RECORD_MAX 256

typedef struct {
    uint32_t sectors;
    uint32_t record_size;  // payload + FR_TRAILER_SIZE; at most FR_RECORD_MAX
} fr_geom_t;

// Reads len bytes at a byte offset of the ring; false on a failed read.
typedef bool (*fr_read_fn)(void *ctx, uint32_t offset, void *buf, size_t len);

// Where the next record goes; slot 0 means its sector is erased first.
typedef struct {
    uint32_t sector, slot, seq;
} fr_pos_t;

typedef struct {
    uint32_t left;  // slots still to read
    uint32_t sector, slot;
} fr_cursor_t;

uint32_t fr_per_sector(const fr_geom_t *g);
uint32_t fr_offset(const fr_geom_t *g, uint32_t sector, uint32_t slot);

// Builds a record of g->record_size bytes in out.
void fr_encode(const fr_geom_t *g, void *out, const void *payload, uint32_t seq);
bool fr_valid(const fr_geom_t *g, const void *record);
bool fr_erased(const fr_geom_t *g, const void *record);

// Finds the end of the ring, past any torn record.
void fr_find(const fr_geom_t *g, fr_read_fn read, void *ctx, fr_pos_t *pos);

// The position after a record written at *pos.
void fr_advance(const fr_geom_t *g, fr_pos_t *pos);

// Reads every valid record oldest first; fr_next is false at the end. payload
// receives g->record_size - FR_TRAILER_SIZE bytes.
void fr_begin(const fr_geom_t *g, fr_cursor_t *c, const fr_pos_t *pos);
bool fr_next(const fr_geom_t *g, fr_cursor_t *c, fr_read_fn read, void *ctx, void *payload);
