idf_component_register(SRCS "ntfy_tx.c" "ntfy_tx_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_http_client esp-tls mbedtls)
