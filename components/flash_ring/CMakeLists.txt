idf_component_register(SRCS "flash_ring.c" "flash_ring_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common esp_partition freertos
                       PRIV_REQUIRES nvs_flash)
