idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES wifi_net portal identity status_http ota_rx udp_log
                                     mdns_name button deep_sleep st7789 rm67162 text esp_try tdisplay_s3
                                     tdisplay_s3_amoled time_sync)
