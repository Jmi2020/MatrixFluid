idf_component_register(
    SRCS "tap_detector.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer sensors
)