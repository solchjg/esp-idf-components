#include "flash_ring.h"

#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS "flash_ring"
#define SAVED_MAGIC 0x46524E47u

static bool read_flash(void *ctx, uint32_t offset, void *buf, size_t len)
{
    const esp_partition_t *part = ctx;
    return esp_partition_read(part, offset, buf, len) == ESP_OK;
}

esp_err_t flash_ring_open(flash_ring_t *r)
{
    if (r->part == NULL) {
        r->part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                           r->label);
        if (r->part == NULL) {
            return ESP_ERR_NOT_FOUND;
        }
        r->geom = (fr_geom_t){.sectors = r->part->size / FR_SECTOR_SIZE,
                              .record_size = r->payload_size + FR_TRAILER_SIZE};
        r->lock = xSemaphoreCreateMutex();
    }
    flash_ring_saved_t *s = r->saved;
    if (s->magic != SAVED_MAGIC || s->pos.sector >= r->geom.sectors ||
        s->pos.slot >= fr_per_sector(&r->geom)) {
        fr_find(&r->geom, read_flash, (void *)r->part, &s->pos);
        s->magic = SAVED_MAGIC;
    }
    return ESP_OK;
}

esp_err_t flash_ring_append(flash_ring_t *r, const void *payload)
{
    esp_err_t err = flash_ring_open(r);
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(r->lock, portMAX_DELAY);
    fr_pos_t *pos = &r->saved->pos;
    if (pos->slot == 0) {
        err = esp_partition_erase_range(r->part, pos->sector * FR_SECTOR_SIZE, FR_SECTOR_SIZE);
    }
    if (err == ESP_OK) {
        uint8_t rec[FR_RECORD_MAX];
        fr_encode(&r->geom, rec, payload, pos->seq);
        err = esp_partition_write(r->part, fr_offset(&r->geom, pos->sector, pos->slot), rec,
                                  r->geom.record_size);
    }
    // A slot that failed is skipped, as a torn record would be.
    fr_advance(&r->geom, pos);
    xSemaphoreGive(r->lock);
    return err;
}

static void set_clear_mark(const flash_ring_t *r, bool on)
{
    nvs_handle_t h;
    nvs_flash_init();
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (on) {
        nvs_set_u8(h, r->label, 1);
    } else {
        nvs_erase_key(h, r->label);
    }
    nvs_commit(h);
    nvs_close(h);
}

bool flash_ring_clear_pending(const flash_ring_t *r)
{
    nvs_handle_t h;
    uint8_t mark = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, r->label, &mark);
        nvs_close(h);
    }
    return mark != 0;
}

esp_err_t flash_ring_clear(flash_ring_t *r, void (*progress)(void))
{
    esp_err_t err = flash_ring_open(r);
    if (err != ESP_OK) {
        return err;
    }
    set_clear_mark(r, true);
    xSemaphoreTake(r->lock, portMAX_DELAY);
    static uint32_t buf[FR_SECTOR_SIZE / sizeof(uint32_t)];
    for (uint32_t s = 0; s < r->geom.sectors && err == ESP_OK; s++) {
        err = esp_partition_read(r->part, s * FR_SECTOR_SIZE, buf, sizeof buf);
        bool blank = err == ESP_OK;
        for (size_t i = 0; blank && i < sizeof buf / sizeof buf[0]; i++) {
            blank = buf[i] == 0xFFFFFFFFu;
        }
        if (err == ESP_OK && !blank) {
            err = esp_partition_erase_range(r->part, s * FR_SECTOR_SIZE, FR_SECTOR_SIZE);
        }
        if (progress) {
            progress();
        }
        vTaskDelay(1);  // the idle tasks are watched too
    }
    if (err == ESP_OK) {
        r->saved->pos = (fr_pos_t){.seq = 1};
        r->saved->magic = SAVED_MAGIC;
    }
    xSemaphoreGive(r->lock);
    if (err == ESP_OK) {
        set_clear_mark(r, false);
    }
    return err;
}

esp_err_t flash_ring_begin(flash_ring_t *r, fr_cursor_t *c)
{
    esp_err_t err = flash_ring_open(r);
    if (err == ESP_OK) {
        xSemaphoreTake(r->lock, portMAX_DELAY);
        fr_begin(&r->geom, c, &r->saved->pos);
        xSemaphoreGive(r->lock);
    }
    return err;
}

bool flash_ring_next(flash_ring_t *r, fr_cursor_t *c, void *payload)
{
    xSemaphoreTake(r->lock, portMAX_DELAY);
    bool more = fr_next(&r->geom, c, read_flash, (void *)r->part, payload);
    xSemaphoreGive(r->lock);
    return more;
}
