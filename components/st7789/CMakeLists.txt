idf_component_register(SRCS "st7789.c" "st7789_core.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_try)
