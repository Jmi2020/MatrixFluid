idf_component_register(
    SRCS "fluid_sensors.c" "qmi8658_accel.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)