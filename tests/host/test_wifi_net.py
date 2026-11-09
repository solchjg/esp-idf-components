"""wifi_net_init beside another netif (source check; the call needs ESP-IDF).

On the cellular build the modem's PPP link creates the default event loop
before wifi_net_init runs, so esp_event_loop_create_default returns
ESP_ERR_INVALID_STATE there; that must not fail the WiFi set-up.
"""
import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "components" / "wifi_net" / "wifi_net.c"


def test_event_loop_already_up_is_accepted():
    body = SRC.read_text()
    assert "ESP_TRY(esp_event_loop_create_default())" not in body
    m = re.search(r"err = esp_event_loop_create_default\(\);\s*if \(err != ESP_OK && "
                  r"err != ESP_ERR_INVALID_STATE\)", body)
    assert m, "an event loop created by another netif must be accepted"
