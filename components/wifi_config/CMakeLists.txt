idf_component_register(
    SRCS "wifi_config.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_http_server nvs_flash log freertos esp_common json sensors display_controller alerts demo_mode app_update
)
