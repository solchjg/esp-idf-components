idf_component_register(SRCS "bme280.c" "bme280_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c log freertos esp_try)
