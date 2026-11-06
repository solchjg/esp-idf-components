idf_component_register(SRCS "sim7080.c" "sim7080_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_netif
                       PRIV_REQUIRES esp_driver_gpio esp_timer freertos)
