idf_component_register(SRCS "ssd1683.c" "ssd1683_core.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_timer esp_try)
