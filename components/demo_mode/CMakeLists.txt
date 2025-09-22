idf_component_register(
    SRCS "demo_mode.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_common log led_matrix
)
