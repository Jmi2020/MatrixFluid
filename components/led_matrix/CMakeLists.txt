idf_component_register(
    SRCS "led_matrix_neopixel.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos adafruit_neopixel
)