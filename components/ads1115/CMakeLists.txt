idf_component_register(SRCS "ads1115.c" "ads1115_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c freertos esp_try)
