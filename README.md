# esp-idf-components

Reusable ESP-IDF v6 components, first written for the LilyGO T-Display-S3
webserver (`solchjg/tdisplay-s3-webserver`) and now shared with the T-Display-S3
AMOLED weather station (`solchjg/tdisplay-s3-amoled`). Each directory under `components/`
is a plain ESP-IDF component with its own `idf_component.yml`; a project takes
only the ones it needs and wires them together in its own `app_main`.

## Components

| Component | Use it for | Kconfig options (default) | Also needs |
|-----------|------------|---------------------------|------------|
| `wifi_net` | station + open SoftAP, credentials in NVS | `WIFI_NET_SOFTAP_CHANNEL` (1), `WIFI_NET_SOFTAP_MAX_CLIENTS` (4), `WIFI_NET_NVS_NAMESPACE` ("net") | `esp_try` |
| `ble_setup` | BLE GATT service carrying the app's settings as a form body, an optional paged history of lines (`read_history`, filtered by `range=`), optional field rules the page checks and draws the form from, sections, choices and units included, and a field required only while another holds a value (`required_if`) (`read_form`), LE Secure Connections passkey pairing, a static random address replaced whenever no bond is held (so a phone that kept an erased pairing meets a new device), NimBLE peripheral started and stopped on demand; a Web Bluetooth page in `web/` | `BLE_SETUP_BODY_MAX` (2048), `BLE_SETUP_FORM_MAX` (2048), `BLE_SETUP_ADV_INTERVAL_MS` (160) | `CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED` in the app's sdkconfig |
| `portal` | captive WiFi setup form on the SoftAP (192.168.4.1), optionally with app fields (`portal_start_extra`); the page checks fields in the browser (`required`, `pattern`, … and `data-group`/`data-need` for all-or-nothing groups) and keeps what was typed when the device refuses a submission; an optional WiFi on/off switch that makes the credentials optional when off (`pc_parse_wifi`) | `PORTAL_PAGE_TITLE` ("TDS3 setup"), `PORTAL_BODY_MAX` (1024), `PORTAL_WIFI_SWITCH` (n) | — |
| `identity` | SoftAP SSID and hostname from the base MAC | `IDENTITY_SOFTAP_SSID_PREFIX` ("TDS3-"), `IDENTITY_HOSTNAME_PREFIX` ("tds3-") | — |
| `status_http` | `GET /` and `GET /api/status`, a server other routes can join | — | — |
| `ota_rx` | `POST /ota` download into the other slot; an app hook can refuse a request (409) | — | `status_http` or any `httpd_handle_t` |
| `udp_log` | mirror one log tag's lines over UDP | `UDP_LOG_PORT` (5555), `UDP_LOG_TAG` ("web") | — |
| `mdns_name` | answer `<hostname>.local` | — | `espressif/mdns` (fetched by its manifest) |
| `button` | long-press (5 s) detector on an active-low GPIO | `BUTTON_GPIO` (0) | — |
| `deep_sleep` | deep sleep for a period, app prepare callbacks before it, the wake cause after | `DEEP_SLEEP_MIN_S` (1), `DEEP_SLEEP_MAX_S` (86400), `DEEP_SLEEP_WAKE_GPIO` (-1: timer only), `DEEP_SLEEP_WAKE_PULLUP` (y) | `esp_try` |
| `time_sync` | time zone and SNTP while a network is up; synced state survives deep sleep; the clock can also be set from another source (`time_sync_set`) | `TIME_SYNC_SERVER` ("pool.ntp.org"), `TIME_SYNC_TZ` ("UTC0") | — |
| `mail_tx` | one plain-text email over SMTP: TLS on 465, STARTTLS otherwise, CA-bundle verified, AUTH PLAIN or LOGIN | — (needs `MBEDTLS_CERTIFICATE_BUNDLE`, on by default) | — |
| `ntfy_tx` | one ntfy push notification (self-hosted or ntfy.sh) by HTTP POST: title, priority, tags, click URL, optional Bearer token; HTTPS CA-bundle verified | — (HTTPS needs `MBEDTLS_CERTIFICATE_BUNDLE`, on by default) | — |
| `notecard` | Blues Notecard on I2C (0x17): Blues' note-c, vendored unchanged (v2.6.2, MIT), over the I2C master driver, joining a bus another driver opened or opening its own; a pure core builds the JSON request lines (`hub.set`, `card.transport`, `note.add`, `card.location.mode`, ...) and reads answer fields (`nc_str`, `nc_num`, `nc_err`, dotted paths) | — | `esp_try` |
| `axp2101` | AXP2101 PMU on I2C (0x34): DC3, ALDO3 and BLDO1/2 rails, battery voltage and charger state; joins a bus another driver opened on the port or opens its own and releases it at `axp2101_stop`, so a bus can share a controller in turn; a pure core holds the rail codes and the register sequences | — | `esp_try` |
| `max17048` | MAX17048 single-cell fuel gauge on I2C (0x36): cell voltage in mV and state of charge in %; joins a bus another driver opened on the port or opens its own; a pure core holds the scales and the read sequence | — | `esp_try` |
| `sim7670` | SIMCom SIM7670G LTE Cat-1 modem over `esp_modem`, on a board that switches its supply from a GPIO and holds PWRKEY low: supply on and first answer, attach (APN, network lock `AT+COPS`), registration, network time in UTC, GNSS (`AT+CGNSSPWR`, `+CGNSSINFO`), PPP data mode, `AT+CPOF` with the supply cut and held through deep sleep; a pure core builds the commands and parses `+CGMR`, `+CCLK` and `+CGNSSINFO` | — | `sim7080` (its 3GPP parsers), `espressif/esp_modem` (fetched by its manifest); the app's PPP netif |
| `sim7080` | SIMCom SIM7080G LTE-M modem over `esp_modem`: PWRKEY wake, the full attach (network lock `AT+COPS`, PSM request `AT+CPSMS`), PSM resume, the granted PSM timers, network time, GNSS, PPP data mode; a pure core builds the AT commands and parses `+CEREG`, `+CSQ`, `+CCLK`, `+COPS`, `+CGNSINF` and the firmware revision | — | `espressif/esp_modem` (fetched by its manifest); the app's PPP netif |
| `flash_ring` | append-only log of fixed-size records in a data partition: reuses its oldest sector when full, skips torn records, resumes a cut-short clear | — | a data partition; NVS for the clear mark |
| `bme280` | BME280 temperature, humidity and pressure on I2C, one forced reading per call, configured by `bme280_config_t` | — | `esp_try` |
| `ads1115` | ADS1115 16-bit ADC on I2C, single-shot conversions and their mean in µV; joins a bus another driver opened on the port, or opens its own | — | `esp_try` |
| `loop420` | 4-20 mA loop, pure C: current from a shunt voltage, NAMUR NE43 fault limits (3.6 / 21.0 mA), linear scale to a value | — | — |
| `st7789` | ST7789 panel on an 8-bit i80 bus or 4-wire SPI (joining an SPI bus another device already brought up), configured by `st7789_config_t`; the header names the settings of the T-Display-S3 and the Waveshare 1.9" 170 × 320 module; a pure core holds the colour helpers and the rectangle and band rules | — | `esp_try` |
| `rm67162` | RM67162 AMOLED panel on a QSPI bus, configured by `rm67162_config_t`; same API shape as `st7789` | — | `esp_try` |
| `ssd1322` | SSD1322 256 × 64 grey OLED on 4-wire SPI, configured by `ssd1322_config_t`, joining an SPI bus another device already brought up; a pure core holds the init sequence, the column window and the 4-bit packing | — | `esp_try` |
| `text` | 16 × 32, 12 × 24 and 6 × 10 bitmap font renderer, pure C | — | — |
| `esp_try` | `ESP_TRY`: return an `esp_err_t` instead of aborting | — | — |
| `tdisplay_s3` | T-Display-S3 pin and panel constants, header only | — | — |
| `tdisplay_s3_amoled` | T-Display-S3 AMOLED 1.91" pin and panel constants, header only | — | — |
| `tsim7080g_s3` | T-SIM7080G-S3 pin and rail constants, header only | — | — |

Set Kconfig options with `idf.py menuconfig` or in the project's
`sdkconfig.defaults`. The defaults are the T-Display-S3 webserver's values.

## Rules every component keeps

- No component includes an app's components: it reports through callbacks the
  app registers, and takes app-specific values from its own Kconfig options.
- Pure cores (`*_core.c`, `identity.c`, `text.c`, `loop420.c`, `dns_core.c`) include no
  ESP-IDF headers, so `tests/host/` compiles them with the host `cc`.
- Every call returns `esp_err_t`; no component retries or aborts
  (no `ESP_ERROR_CHECK`). Policy belongs to the app.
- `components/text/font16x32.c` and `font12x24.c` are generated by
  `tools/bdf_to_c.py` (`bdf_to_c.py ter-u24b.bdf 12x24` for the small one); regenerate
  them, never hand-edit them.

Comments that cite "FSD §n" or a requirement ID refer to the FSD of the
project the component was built for, `tdisplay-s3-webserver` or
`tdisplay-s3-amoled`; the AMOLED FSD keeps the webserver's numbers and adds
its own sections, so an ID means the same in both.

## Using them

**IDF Component Manager** (recommended for a new project). In the project's
`main/idf_component.yml`, list each component you need, pinned to a tag:

```yaml
dependencies:
  wifi_net:
    git: https://github.com/solchjg/esp-idf-components.git
    path: components/wifi_net
    version: v0.1.0
  esp_try:
    git: https://github.com/solchjg/esp-idf-components.git
    path: components/esp_try
    version: v0.1.0
```

Remember the "Also needs" column: list those components too. Tag `v0.1.0`
holds only twelve of them (`button`, `esp_try`, `identity`, `mdns_name`,
`ota_rx`, `portal`, `st7789`, `status_http`, `tdisplay_s3`, `text`, `udp_log`,
`wifi_net`); until the next tag, pin a commit of `main` (`version: <commit sha>`)
to get the others.

**Git submodule** (when the project's host tests compile these sources, as the
T-Display-S3 webserver and the AMOLED weather station do):

```bash
git submodule add https://github.com/solchjg/esp-idf-components.git shared
```

and in the project's top-level `CMakeLists.txt`, before `project.cmake`:

```cmake
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/shared/components)
```

## Tests and CI

```bash
ruff check .
pytest tests/host -q            # pure cores, host compiler
cd examples/build_all && idf.py set-target esp32s3 && idf.py build
```

`ci.yml` runs both on every push and pull request. Bump `version` in every
changed component's `idf_component.yml` and tag the release `vX.Y.Z`.
