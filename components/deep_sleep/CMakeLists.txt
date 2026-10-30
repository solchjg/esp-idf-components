idf_component_register(SRCS "deep_sleep.c" "deep_sleep_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_hw_support esp_driver_gpio esp_try)
