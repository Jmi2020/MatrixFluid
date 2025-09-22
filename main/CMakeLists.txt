idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES driver freertos esp_timer spi_flash led_matrix sensors display_controller wifi_config demo_mode adafruit_neopixel esp_wifi esp_netif esp_http_server
                       INCLUDE_DIRS "")
