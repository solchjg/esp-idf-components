idf_component_register(SRCS "max17048.c" "max17048_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c log esp_try)
