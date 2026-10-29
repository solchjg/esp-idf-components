idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES wifi_net portal identity status_http ota_rx udp_log
                                     mdns_name button st7789 text esp_try tdisplay_s3)
