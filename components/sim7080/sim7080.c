#include "sim7080.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_modem_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define AT_TIMEOUT_MS 1000
#define SIM_UP_TRIES 15    // tries for the lock and PSM request while the SIM comes up
#define POLL_MS 2000       // between +CEREG asks during an attach

static struct {
    esp_modem_dce_t *dce;
    int pwrkey;
} s;

static char out[CONFIG_ESP_MODEM_C_API_STR_MAX];

static uint32_t ms_since(int64_t t0_us)
{
    return (uint32_t)((esp_timer_get_time() - t0_us) / 1000);
}

esp_err_t sim7080_start(const sim7080_config_t *cfg, const char *apn, esp_netif_t *ppp_netif)
{
    if (cfg == NULL || ppp_netif == NULL || !sim7080_apn_valid(apn)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dce != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t mask = 1ULL << cfg->pwrkey_gpio;
    if (cfg->dtr_gpio >= 0) {
        mask |= 1ULL << cfg->dtr_gpio;
    }
    gpio_config_t io = {.pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT};
    gpio_hold_dis(cfg->pwrkey_gpio);
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(cfg->pwrkey_gpio, 0);
    if (cfg->dtr_gpio >= 0) {
        gpio_set_level(cfg->dtr_gpio, 0);  // DTR low keeps the modem's UART awake
    }
    s.pwrkey = cfg->pwrkey_gpio;

    esp_modem_dte_config_t dte = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte.uart_config.port_num = cfg->uart_port;
    dte.uart_config.tx_io_num = cfg->tx_gpio;
    dte.uart_config.rx_io_num = cfg->rx_gpio;
    dte.uart_config.rts_io_num = -1;
    dte.uart_config.cts_io_num = -1;
    dte.uart_config.baud_rate = cfg->baud;
    esp_modem_dce_config_t dc = ESP_MODEM_DCE_DEFAULT_CONFIG(apn);
    s.dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7070, &dte, &dc, ppp_netif);
    return s.dce != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sim7080_at(const char *cmd, const char *pass, uint32_t timeout_ms, const char **answer)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char line[SIM7080_CMD_MAX + 2];
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

static bool answers(uint32_t window_ms, int *tries)
{
    int64_t t0 = esp_timer_get_time();
    do {
        ++*tries;
        if (esp_modem_at_raw(s.dce, "AT\r", out, "OK", "ERROR", 100) == ESP_OK) {
            return true;
        }
    } while (ms_since(t0) < window_ms);
    return false;
}

// PWRKEY as LilyGO drives it: low, high for 1 s, low (the modem toggles on,
// or wakes from PSM).
static void pwrkey_pulse(void)
{
    gpio_hold_dis(s.pwrkey);
    gpio_set_level(s.pwrkey, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(s.pwrkey, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(s.pwrkey, 0);
}

esp_err_t sim7080_wake(uint32_t ask_ms, uint32_t answer_ms, sim7080_wake_t *info)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t t0 = esp_timer_get_time();
    sim7080_wake_t w = {0};
    bool ok = answers(ask_ms, &w.tries);
    if (!ok) {
        pwrkey_pulse();
        w.pulsed = true;
        ok = answers(answer_ms, &w.tries);
    }
    w.ms = ms_since(t0);
    if (info != NULL) {
        *info = w;
    }
    return ok ? ESP_OK : ESP_ERR_TIMEOUT;
}

// A command the modem refuses until its SIM is up after CFUN=1, asked again
// each second. A try that ran to its timeout means the modem stopped
// answering, so it is not repeated.
static esp_err_t until_sim_up(const char *cmd, uint32_t timeout_ms)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < SIM_UP_TRIES; i++) {
        err = sim7080_at(cmd, NULL, timeout_ms, NULL);
        if (err != ESP_FAIL) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return err;
}

static esp_err_t cereg(sim7080_cereg_t *c)
{
    const char *a;
    esp_err_t err = sim7080_at("AT+CEREG?", NULL, AT_TIMEOUT_MS, &a);
    if (err == ESP_OK && sim7080_parse_cereg(a, c) != 0) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t sim7080_attach(const sim7080_attach_cfg_t *cfg, uint32_t limit_ms, sim7080_cereg_t *grant,
                         uint32_t *ms)
{
    char cmds[SIM7080_ATTACH_CMDS][SIM7080_CMD_MAX], cops[SIM7080_CMD_MAX], cpsms[SIM7080_CMD_MAX];
    int n = sim7080_attach_script(cfg, cmds, SIM7080_ATTACH_CMDS);
    if (n < 0 || sim7080_cops_cmd(cfg->plmn, cops, sizeof cops) < 0 ||
        sim7080_cpsms_cmd(cfg, cpsms, sizeof cpsms) < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t t0 = 0;
    for (int i = 0; i < n; i++) {
        bool radio = strncmp(cmds[i], "AT+CFUN=", 8) == 0;
        if (strcmp(cmds[i], "AT+CFUN=1") == 0) {
            t0 = esp_timer_get_time();
        }
        // CFUN=0 fails on a modem whose radio is already off; nothing else is ignored.
        esp_err_t err = sim7080_at(cmds[i], NULL, radio ? 10000 : AT_TIMEOUT_MS, NULL);
        if (err != ESP_OK && i > 0) {
            return err;
        }
    }
    // The lock (AT+COPS=1) can take a network search: never longer than the
    // registration window, so a refused network fails within limit_ms.
    esp_err_t err = until_sim_up(cops, limit_ms);
    if (err == ESP_OK) {
        err = until_sim_up(cpsms, AT_TIMEOUT_MS);
    }
    if (err != ESP_OK) {
        return err;
    }
    sim7080_cereg_t c = {0};
    while (ms_since(t0) < limit_ms) {
        if (cereg(&c) == ESP_OK && sim7080_registered(&c)) {
            if (grant != NULL) {
                *grant = c;
            }
            if (ms != NULL) {
                *ms = ms_since(t0);
            }
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t sim7080_resume(uint32_t window_ms, uint32_t *ms)
{
    int64_t t0 = esp_timer_get_time();
    sim7080_cereg_t c;
    do {
        if (cereg(&c) == ESP_OK && sim7080_registered(&c)) {
            if (ms != NULL) {
                *ms = ms_since(t0);
            }
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    } while (ms_since(t0) < window_ms);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sim7080_csq(int *rssi)
{
    const char *a;
    esp_err_t err = sim7080_at("AT+CSQ", NULL, AT_TIMEOUT_MS, &a);
    return err != ESP_OK ? err : sim7080_parse_csq(a, rssi) == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7080_network(char *mcc_mnc, size_t size)
{
    const char *a;
    esp_err_t err = sim7080_at("AT+COPS=3,2", NULL, AT_TIMEOUT_MS, NULL);
    if (err == ESP_OK) {
        err = sim7080_at("AT+COPS?", NULL, AT_TIMEOUT_MS, &a);
    }
    return err != ESP_OK ? err
           : sim7080_parse_cops(a, mcc_mnc, size) >= 0 ? ESP_OK
                                                       : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7080_firmware(char *rev, size_t size)
{
    const char *a;
    esp_err_t err = sim7080_at("AT+CGMR", NULL, AT_TIMEOUT_MS, &a);
    return err != ESP_OK ? err
           : sim7080_parse_cgmr(a, rev, size) >= 0 ? ESP_OK
                                                   : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7080_network_time(int64_t *unix_s)
{
    const char *a;
    esp_err_t err = sim7080_at("AT+CCLK?", NULL, AT_TIMEOUT_MS, &a);
    return err != ESP_OK ? err : sim7080_parse_cclk(a, unix_s) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t sim7080_gnss_on(void)
{
    esp_err_t err = sim7080_at("AT+CFUN=0", NULL, 10000, NULL);
    if (err == ESP_OK) {
        err = sim7080_at("AT+CGNSPWR=1", NULL, AT_TIMEOUT_MS, NULL);
    }
    return err;
}

esp_err_t sim7080_gnss_read(sim7080_gnss_t *fix)
{
    const char *a;
    esp_err_t err = sim7080_at("AT+CGNSINF", NULL, 2000, &a);
    return err != ESP_OK ? err : sim7080_parse_cgnsinf(a, fix) == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t sim7080_gnss_off(void)
{
    return sim7080_at("AT+CGNSPWR=0", NULL, AT_TIMEOUT_MS, NULL);
}

esp_err_t sim7080_ppp_up(void)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_modem_set_mode(s.dce, ESP_MODEM_MODE_DATA);
}

esp_err_t sim7080_ppp_down(void)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_modem_set_mode(s.dce, ESP_MODEM_MODE_COMMAND);
}

esp_err_t sim7080_power_off(void)
{
    return sim7080_at("AT+CPOWD=1", "POWER DOWN", 5000, NULL);
}

void sim7080_sleep_hold(void)
{
    gpio_set_level(s.pwrkey, 0);
    gpio_hold_en(s.pwrkey);
}

esp_err_t sim7080_stop(void)
{
    if (s.dce == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_modem_destroy(s.dce);
    s.dce = NULL;
    return ESP_OK;
}
