idf_component_register(SRCS "axp2101.c" "axp2101_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c log esp_try)
