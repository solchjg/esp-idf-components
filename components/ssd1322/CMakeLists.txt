idf_component_register(SRCS "ssd1322.c" "ssd1322_core.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_try)
