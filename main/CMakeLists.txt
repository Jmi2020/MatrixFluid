idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES driver freertos esp_timer spi_flash led_matrix sensors tap_detection adafruit_neopixel
                       INCLUDE_DIRS "")
