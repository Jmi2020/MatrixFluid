idf_component_register(
    SRCS "fluid_sensors.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)
