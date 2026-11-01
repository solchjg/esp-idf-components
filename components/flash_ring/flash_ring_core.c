#include "flash_ring_core.h"

#include <string.h>

static uint32_t crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

static uint32_t record_seq(const fr_geom_t *g, const uint8_t *r)
{
    uint32_t seq;
    memcpy(&seq, r + g->record_size - FR_TRAILER_SIZE, sizeof seq);
    return seq;
}

uint32_t fr_per_sector(const fr_geom_t *g)
{
    return FR_SECTOR_SIZE / g->record_size;
}

uint32_t fr_offset(const fr_geom_t *g, uint32_t sector, uint32_t slot)
{
    return sector * FR_SECTOR_SIZE + slot * g->record_size;
}

void fr_encode(const fr_geom_t *g, void *out, const void *payload, uint32_t seq)
{
    uint8_t *r = out;
    size_t n = g->record_size - FR_TRAILER_SIZE;
    memcpy(r, payload, n);
    memcpy(r + n, &seq, sizeof seq);
    uint32_t crc = crc32(r, g->record_size - sizeof crc);
    memcpy(r + g->record_size - sizeof crc, &crc, sizeof crc);
}

bool fr_valid(const fr_geom_t *g, const void *record)
{
    const uint8_t *r = record;
    uint32_t crc;
    memcpy(&crc, r + g->record_size - sizeof crc, sizeof crc);
    return crc == crc32(r, g->record_size - sizeof crc);
}

bool fr_erased(const fr_geom_t *g, const void *record)
{
    const uint8_t *r = record;
    for (uint32_t i = 0; i < g->record_size; i++) {
        if (r[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

// The first valid sequence number in a sector, looking past a torn slot 0.
static bool sector_seq(const fr_geom_t *g, fr_read_fn read, void *ctx, uint32_t sector,
                       uint32_t *seq)
{
    uint8_t r[FR_RECORD_MAX];
    for (uint32_t slot = 0; slot < fr_per_sector(g); slot++) {
        if (!read(ctx, fr_offset(g, sector, slot), r, g->record_size)) {
            return false;
        }
        if (fr_valid(g, r)) {
            *seq = record_seq(g, r);
            return true;
        }
        if (slot == 0 && fr_erased(g, r)) {
            return false;
        }
    }
    return false;
}

void fr_find(const fr_geom_t *g, fr_read_fn read, void *ctx, fr_pos_t *pos)
{
    bool any = false;
    uint32_t head = 0, best = 0;
    for (uint32_t s = 0; s < g->sectors; s++) {
        uint32_t seq;
        if (sector_seq(g, read, ctx, s, &seq) && (!any || seq > best)) {
            any = true;
            best = seq;
            head = s;
        }
    }
    *pos = (fr_pos_t){.sector = 0, .slot = 0, .seq = 1};
    if (!any) {
        return;
    }
    // The next slot is past the last one written, valid or torn.
    uint32_t top = best, used = 0;
    uint8_t r[FR_RECORD_MAX];
    for (uint32_t slot = 0; slot < fr_per_sector(g); slot++) {
        if (!read(ctx, fr_offset(g, head, slot), r, g->record_size)) {
            continue;
        }
        if (!fr_erased(g, r)) {
            used = slot + 1;
        }
        if (fr_valid(g, r) && record_seq(g, r) > top) {
            top = record_seq(g, r);
        }
    }
    *pos = (fr_pos_t){.sector = head, .slot = used, .seq = top + 1};
    if (used == fr_per_sector(g)) {
        pos->slot = 0;
        pos->sector = (head + 1) % g->sectors;
    }
}

void fr_advance(const fr_geom_t *g, fr_pos_t *pos)
{
    pos->seq++;
    if (++pos->slot == fr_per_sector(g)) {
        pos->slot = 0;
        pos->sector = (pos->sector + 1) % g->sectors;
    }
}

void fr_begin(const fr_geom_t *g, fr_cursor_t *c, const fr_pos_t *pos)
{
    // Oldest first: from the sector after the one being filled, ending with its
    // written slots. At slot 0 the next sector to reuse is itself the oldest.
    uint32_t first = pos->slot ? (pos->sector + 1) % g->sectors : pos->sector;
    uint32_t whole = pos->slot ? g->sectors - 1 : g->sectors;
    *c = (fr_cursor_t){.sector = first, .slot = 0,
                       .left = whole * fr_per_sector(g) + pos->slot};
}

bool fr_next(const fr_geom_t *g, fr_cursor_t *c, fr_read_fn read, void *ctx, void *payload)
{
    uint8_t r[FR_RECORD_MAX];
    while (c->left > 0) {
        bool ok = read(ctx, fr_offset(g, c->sector, c->slot), r, g->record_size);
        if (ok && c->slot == 0 && fr_erased(g, r)) {
            // Slots fill in order, so an erased first slot means an empty sector.
            uint32_t skip = fr_per_sector(g);
            c->left -= skip < c->left ? skip : c->left;
            c->sector = (c->sector + 1) % g->sectors;
            continue;
        }
        c->left--;
        if (++c->slot == fr_per_sector(g)) {
            c->slot = 0;
            c->sector = (c->sector + 1) % g->sectors;
        }
        if (ok && fr_valid(g, r)) {
            memcpy(payload, r, g->record_size - FR_TRAILER_SIZE);
            return true;
        }
    }
    return false;
}
