#include "sim7670.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_modem_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sim7080_core.h"

#define AT_TIMEOUT_MS 1000
#define SIM_UP_TRIES 15   // tries for the lock while the SIM comes up
#define POLL_MS 2000      // between +CEREG asks during an attach
#define CPOF_MS 3550      // AT+CPOF until the modem stops answering (SLOT2)
#define GNSS_READY_MS 30000

static struct {
    esp_modem_dce_t *dce;
    int supply;
    bool radio_off;
} s = {.supply = -1};

static char out[CONFIG_ESP_MODEM_C_API_STR_MAX];

static uint32_t ms_since(int64_t t0_us)
{
    return (uint32_t)((esp_timer_get_time() - t0_us) / 1000);
}

static void supply(bool on)
{
    gpio_hold_dis(s.supply);
    gpio_set_level(s.supply, on);
    gpio_hold_en(s.supply);  // kept through deep sleep
}

esp_err_t sim7670_start(const sim7670_config_t *cfg, const char *apn, esp_netif_t *ppp_netif)
{
    if (cfg == NULL || ppp_netif == NULL || !sim7080_apn_valid(apn) || cfg->supply_gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dce != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t mask = 1ULL << cfg->supply_gpio;
    if (cfg->dtr_gpio >= 0) {
        mask |= 1ULL << cfg->dtr_gpio;
    }
    gpio_hold_dis(cfg->supply_gpio);
    gpio_config_t io = {.pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    s.supply = cfg->supply_gpio;
    supply(false);
    if (cfg->dtr_gpio >= 0) {
        gpio_set_level(cfg->dtr_gpio, 0);  // DTR low keeps the modem's UART awake
    }

    esp_modem_dte_config_t dte = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte.uart_config.port_num = cfg->uart_port;
    dte.uart_config.tx_io_num = cfg->tx_gpio;
    dte.uart_config.rx_io_num = cfg->rx_gpio;
    dte.uart_config.rts_io_num = -1;
    dte.uart_config.cts_io_num = -1;
    dte.uart_config.baud_rate = cfg->baud;
    esp_modem_dce_config_t dc = ESP_MODEM_DCE_DEFAULT_CONFIG(apn);
    s.dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte, &dc, ppp_netif);
    return s.dce != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sim7670_at(const char *cmd, const char *pass, uint32_t timeout_ms, const char **answer)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char line[SIM7670_CMD_MAX + 2];
    if (cmd == NULL || snprintf(line, sizeof line, "%s\r", cmd) >= (int)sizeof line) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    esp_err_t err = esp_modem_at_raw(s.dce, line, out, pass ? pass : "OK", "ERROR", (int)timeout_ms);
    if (answer != NULL) {
        *answer = out;
    }
    return err;
}

esp_err_t sim7670_power_on(uint32_t answer_ms, uint32_t *ms)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    supply(true);
    int64_t t0 = esp_timer_get_time();
    do {
        if (esp_modem_at_raw(s.dce, "AT\r", out, "OK", "ERROR", 100) == ESP_OK) {
            if (ms != NULL) {
                *ms = ms_since(t0);
            }
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (ms_since(t0) < answer_ms);
    return ESP_ERR_TIMEOUT;
}

// A command the modem refuses until its SIM is up ("SIM busy" for about a
// second after the first answer on SLOT2), asked again each second. A try
// that ran to its timeout means the modem stopped answering, so it is not
// repeated.
static esp_err_t until_sim_up(const char *cmd, uint32_t timeout_ms)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < SIM_UP_TRIES; i++) {
        err = sim7670_at(cmd, NULL, timeout_ms, NULL);
        if (err != ESP_FAIL) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return err;
}

esp_err_t sim7670_attach(const sim7670_attach_cfg_t *cfg, uint32_t limit_ms, uint32_t *ms)
{
    char cmds[SIM7670_ATTACH_CMDS][SIM7670_CMD_MAX], cops[SIM7670_CMD_MAX];
    int n = sim7670_attach_script(cfg, cmds, SIM7670_ATTACH_CMDS);
    if (n < 0 || sim7080_cops_cmd(cfg->plmn, cops, sizeof cops) < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < n; i++) {
        esp_err_t err = until_sim_up(cmds[i], AT_TIMEOUT_MS);
        if (err != ESP_OK) {
            return err;
        }
    }
    // The lock (AT+COPS=1) can take a network search: never longer than the
    // registration window, so a refused network fails within limit_ms.
    esp_err_t err = until_sim_up(cops, limit_ms);
    if (err != ESP_OK) {
        return err;
    }
    while (ms_since(t0) < limit_ms) {
        const char *a;
        sim7080_cereg_t c;
        if (sim7670_at("AT+CEREG?", NULL, AT_TIMEOUT_MS, &a) == ESP_OK && sim7080_parse_cereg(a, &c) == 0 &&
            sim7080_registered(&c)) {
            if (ms != NULL) {
                *ms = ms_since(t0);
            }
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t sim7670_csq(int *rssi)
{
    const char *a;
    esp_err_t err = sim7670_at("AT+CSQ", NULL, AT_TIMEOUT_MS, &a);
    return err != ESP_OK ? err : sim7080_parse_csq(a, rssi) == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7670_network(char *mcc_mnc, size_t size)
{
    const char *a;
    esp_err_t err = sim7670_at("AT+COPS=3,2", NULL, AT_TIMEOUT_MS, NULL);
    if (err == ESP_OK) {
        err = sim7670_at("AT+COPS?", NULL, AT_TIMEOUT_MS, &a);
    }
    return err != ESP_OK ? err
           : sim7080_parse_cops(a, mcc_mnc, size) >= 0 ? ESP_OK
                                                       : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7670_firmware(char *rev, size_t size)
{
    const char *a;
    esp_err_t err = sim7670_at("AT+CGMR", NULL, AT_TIMEOUT_MS, &a);
    return err != ESP_OK ? err
           : sim7670_parse_cgmr(a, rev, size) >= 0 ? ESP_OK
                                                   : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7670_network_time(int64_t *unix_s)
{
    const char *a;
    esp_err_t err = sim7670_at("AT+CCLK?", NULL, AT_TIMEOUT_MS, &a);
    return err != ESP_OK ? err : sim7670_parse_cclk(a, unix_s) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t sim7670_gnss_on(bool radio_off)
{
    esp_err_t err = ESP_OK;
    if (radio_off) {
        err = sim7670_at("AT+CFUN=4", NULL, 10000, NULL);
        s.radio_off = err == ESP_OK;
    }
    if (err == ESP_OK) {
        err = sim7670_at("AT+CGNSSPWR=1", "READY", GNSS_READY_MS, NULL);
    }
    return err;
}

esp_err_t sim7670_gnss_read(sim7670_gnss_t *fix)
{
    const char *a;
    esp_err_t err = sim7670_at("AT+CGNSSINFO", NULL, 2000, &a);
    return err != ESP_OK ? err
           : sim7670_parse_cgnssinfo(a, fix) == 0 ? ESP_OK
                                                  : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7670_gnss_off(void)
{
    esp_err_t err = sim7670_at("AT+CGNSSPWR=0", NULL, AT_TIMEOUT_MS, NULL);
    if (s.radio_off) {
        esp_err_t e2 = sim7670_at("AT+CFUN=1", NULL, 10000, NULL);
        s.radio_off = e2 != ESP_OK;
        if (err == ESP_OK) {
            err = e2;
        }
    }
    return err;
}

esp_err_t sim7670_ppp_up(void)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_modem_set_mode(s.dce, ESP_MODEM_MODE_DATA);
}

esp_err_t sim7670_ppp_down(void)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_modem_set_mode(s.dce, ESP_MODEM_MODE_COMMAND);
}

esp_err_t sim7670_power_off(void)
{
    if (s.supply < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s.dce != NULL ? sim7670_at("AT+CPOF", NULL, 10000, NULL) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(CPOF_MS));
    }
    supply(false);
    s.radio_off = false;
    return err;
}

esp_err_t sim7670_stop(void)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_modem_destroy(s.dce);
    s.dce = NULL;
    return ESP_OK;
}
