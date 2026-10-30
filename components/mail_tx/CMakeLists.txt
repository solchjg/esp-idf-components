idf_component_register(SRCS "mail_tx.c" "mail_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES mbedtls lwip esp_timer)
