idf_component_register(
    SRCS "display_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES led_matrix sensors freertos esp_timer log esp_common
)