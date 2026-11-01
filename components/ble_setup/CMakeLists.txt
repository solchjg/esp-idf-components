idf_component_register(SRCS "ble_setup.c" "ble_setup_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES bt esp_hw_support log)
