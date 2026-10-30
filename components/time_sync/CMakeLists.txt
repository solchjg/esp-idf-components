idf_component_register(SRCS "time_sync.c" "time_sync_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_netif esp_system)
