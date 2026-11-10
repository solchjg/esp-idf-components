#include "rv8803.h"

#include <stddef.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_try.h"

#define XFER_TIMEOUT_MS 50

typedef struct {
    i2c_master_bus_handle_t bus;
    bool own_bus;
    i2c_master_dev_handle_t dev;
} link_t;

// The master bus another driver already opened on port; none is a normal
// case, so the I2C driver's error log for it is held back.
static bool shared_bus(int port, i2c_master_bus_handle_t *bus)
{
    esp_log_level_t was = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    bool ok = i2c_master_get_bus_handle(port, bus) == ESP_OK;
    esp_log_level_set("i2c.master", was);
    return ok;
}

static void link_close(link_t *l)
{
    if (l->dev) {
        i2c_master_bus_rm_device(l->dev);
    }
    if (l->bus && l->own_bus) {
        i2c_del_master_bus(l->bus);
    }
    *l = (link_t){0};
}

static esp_err_t link_open(const rv8803_config_t *cfg, link_t *l)
{
    *l = (link_t){0};
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!shared_bus(cfg->i2c_port, &l->bus)) {
        i2c_master_bus_config_t bc = {
            .i2c_port = cfg->i2c_port,
            .sda_io_num = cfg->sda_gpio,
            .scl_io_num = cfg->scl_gpio,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = cfg->internal_pullup,
        };
        ESP_TRY(i2c_new_master_bus(&bc, &l->bus));
        l->own_bus = true;
    }
    esp_err_t err = i2c_master_probe(l->bus, RV8803_ADDR, XFER_TIMEOUT_MS);
    if (err == ESP_OK) {
        i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = RV8803_ADDR,
            .scl_speed_hz = cfg->clk_hz,
        };
        err = i2c_master_bus_add_device(l->bus, &dc, &l->dev);
    }
    if (err != ESP_OK) {
        link_close(l);
    }
    return err;  // ESP_ERR_NOT_FOUND from the probe when nothing answers
}

static esp_err_t get(link_t *l, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(l->dev, &reg, 1, buf, len, XFER_TIMEOUT_MS);
}

static esp_err_t put(link_t *l, uint8_t reg, const uint8_t *buf, size_t len)
{
    uint8_t b[8];
    if (len + 1 > sizeof b) {
        return ESP_ERR_INVALID_SIZE;
    }
    b[0] = reg;
    memcpy(b + 1, buf, len);
    return i2c_master_transmit(l->dev, b, len + 1, XFER_TIMEOUT_MS);
}

// The time registers, read until two reads agree from the seconds up, so a
// second that ended between the bytes of one read cannot mix two times.
static esp_err_t read_regs(link_t *l, uint8_t regs[8])
{
    uint8_t again[8];
    ESP_TRY(get(l, RV8803_REG_100TH, regs, 8));
    for (int i = 0; i < 2; i++) {
        ESP_TRY(get(l, RV8803_REG_100TH, again, 8));
        bool same = memcmp(again + 1, regs + 1, 7) == 0;
        memcpy(regs, again, 8);
        if (same) {
            break;
        }
    }
    return ESP_OK;
}

esp_err_t rv8803_read(const rv8803_config_t *cfg, rv8803_time_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    link_t l;
    ESP_TRY(link_open(cfg, &l));
    uint8_t regs[8], flag = 0, ctrl = 0;
    esp_err_t err = get(&l, RV8803_REG_FLAG, &flag, 1);
    if (err == ESP_OK) {
        err = get(&l, RV8803_REG_CTRL, &ctrl, 1);
    }
    if (err == ESP_OK) {
        err = read_regs(&l, regs);
    }
    link_close(&l);
    if (err != ESP_OK) {
        return err;
    }
    *out = (rv8803_time_t){.low = flag & RV8803_FLAG_V1F};
    out->lost = rv8803_stale(flag, ctrl) || !rv8803_regs_to_ms(regs, &out->epoch_ms);
    return ESP_OK;
}

esp_err_t rv8803_write(const rv8803_config_t *cfg, int64_t epoch_s)
{
    uint8_t regs[7];
    if (!rv8803_regs_from_s(epoch_s, regs)) {
        return ESP_ERR_INVALID_ARG;
    }
    link_t l;
    ESP_TRY(link_open(cfg, &l));
    uint8_t ctrl = 0;
    esp_err_t err = get(&l, RV8803_REG_CTRL, &ctrl, 1);
    if (err == ESP_OK) {
        uint8_t held = ctrl | RV8803_CTRL_RESET;
        err = put(&l, RV8803_REG_CTRL, &held, 1);
    }
    if (err == ESP_OK) {
        err = put(&l, RV8803_REG_SEC, regs, sizeof regs);
    }
    // RESET released whatever happened above: left set, it freezes the time.
    uint8_t run = ctrl & (uint8_t)~RV8803_CTRL_RESET;
    esp_err_t rel = put(&l, RV8803_REG_CTRL, &run, 1);
    if (err == ESP_OK) {
        err = rel;
    }
    if (err == ESP_OK) {
        uint8_t none = 0;
        err = put(&l, RV8803_REG_FLAG, &none, 1);  // V1F, V2F and the event flags cleared
    }
    link_close(&l);
    return err;
}

esp_err_t rv8803_get_trim(const rv8803_config_t *cfg, int *steps)
{
    if (steps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    link_t l;
    ESP_TRY(link_open(cfg, &l));
    uint8_t reg = 0;
    esp_err_t err = get(&l, RV8803_REG_OFFSET, &reg, 1);
    link_close(&l);
    if (err == ESP_OK) {
        *steps = rv8803_offset_steps(reg);
    }
    return err;
}

esp_err_t rv8803_set_trim(const rv8803_config_t *cfg, int steps)
{
    link_t l;
    ESP_TRY(link_open(cfg, &l));
    uint8_t reg = rv8803_offset_reg(steps);
    esp_err_t err = put(&l, RV8803_REG_OFFSET, &reg, 1);
    link_close(&l);
    return err;
}
