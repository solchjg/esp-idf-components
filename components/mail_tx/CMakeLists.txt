idf_component_register(SRCS "mail_tx.c" "mail_tx_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp-tls mbedtls esp_timer lwip)
