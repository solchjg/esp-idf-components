idf_component_register(SRCS "rv8803.c" "rv8803_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c log esp_try)
