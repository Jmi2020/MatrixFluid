idf_component_register(
    SRCS "alerts.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_client freertos lwip json esp_timer esp_netif sensors
)
