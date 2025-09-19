/**
 * @file adafruit_neopixel.h
 * @brief Adafruit_NeoPixel-like library for ESP32 using RMT
 *
 * Simple, reliable NeoPixel control based on Adafruit's API
 * Optimized for ESP32-S3 with maximum safety controls
 */

#ifndef ADAFRUIT_NEOPIXEL_H
#define ADAFRUIT_NEOPIXEL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum safe brightness (PROVEN WORKING LEVEL from HowdyTTS)
#define NEOPIXEL_MAX_BRIGHTNESS    5

// Color constants
#define NEOPIXEL_COLOR_RED      0xFF0000
#define NEOPIXEL_COLOR_GREEN    0x00FF00
#define NEOPIXEL_COLOR_BLUE     0x0000FF
#define NEOPIXEL_COLOR_YELLOW   0xFFFF00
#define NEOPIXEL_COLOR_MAGENTA  0xFF00FF
#define NEOPIXEL_COLOR_CYAN     0x00FFFF
#define NEOPIXEL_COLOR_WHITE    0xFFFFFF
#define NEOPIXEL_COLOR_OFF      0x000000

/**
 * @brief NeoPixel strip configuration
 */
typedef struct {
    uint16_t num_leds;          ///< Number of LEDs in strip
    gpio_num_t pin;             ///< GPIO pin number
    uint8_t brightness;         ///< Global brightness (0-255, capped at NEOPIXEL_MAX_BRIGHTNESS)
} neopixel_config_t;

/**
 * @brief NeoPixel handle type
 */
typedef struct neopixel_s* neopixel_handle_t;

/**
 * @brief Initialize NeoPixel strip
 *
 * @param config Strip configuration
 * @param handle Output handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_init(const neopixel_config_t* config, neopixel_handle_t* handle);

/**
 * @brief Begin NeoPixel operation (like Adafruit's begin())
 *
 * @param handle NeoPixel handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_begin(neopixel_handle_t handle);

/**
 * @brief Set pixel color by index
 *
 * @param handle NeoPixel handle
 * @param pixel_index Pixel index (0-based)
 * @param color 24-bit RGB color (0xRRGGBB)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_set_pixel_color(neopixel_handle_t handle, uint16_t pixel_index, uint32_t color);

/**
 * @brief Set pixel color using separate R,G,B values
 *
 * @param handle NeoPixel handle
 * @param pixel_index Pixel index (0-based)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_set_pixel_rgb(neopixel_handle_t handle, uint16_t pixel_index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Update strip with current pixel data (like Adafruit's show())
 *
 * @param handle NeoPixel handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_show(neopixel_handle_t handle);

/**
 * @brief Clear all pixels (set to black)
 *
 * @param handle NeoPixel handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_clear(neopixel_handle_t handle);

/**
 * @brief Fill all pixels with same color
 *
 * @param handle NeoPixel handle
 * @param color 24-bit RGB color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_fill(neopixel_handle_t handle, uint32_t color);

/**
 * @brief Set global brightness
 *
 * @param handle NeoPixel handle
 * @param brightness Brightness level (0-255, capped at NEOPIXEL_MAX_BRIGHTNESS)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_set_brightness(neopixel_handle_t handle, uint8_t brightness);

/**
 * @brief Get current brightness setting
 *
 * @param handle NeoPixel handle
 * @return uint8_t Current brightness (0-255)
 */
uint8_t neopixel_get_brightness(neopixel_handle_t handle);

/**
 * @brief Get number of pixels in strip
 *
 * @param handle NeoPixel handle
 * @return uint16_t Number of pixels
 */
uint16_t neopixel_num_pixels(neopixel_handle_t handle);

/**
 * @brief Convert separate R,G,B values to 24-bit color
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return uint32_t 24-bit color value
 */
static inline uint32_t neopixel_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/**
 * @brief Get red component from 24-bit color
 *
 * @param color 24-bit color value
 * @return uint8_t Red component (0-255)
 */
static inline uint8_t neopixel_color_red(uint32_t color) {
    return (color >> 16) & 0xFF;
}

/**
 * @brief Get green component from 24-bit color
 *
 * @param color 24-bit color value
 * @return uint8_t Green component (0-255)
 */
static inline uint8_t neopixel_color_green(uint32_t color) {
    return (color >> 8) & 0xFF;
}

/**
 * @brief Get blue component from 24-bit color
 *
 * @param color 24-bit color value
 * @return uint8_t Blue component (0-255)
 */
static inline uint8_t neopixel_color_blue(uint32_t color) {
    return color & 0xFF;
}

/**
 * @brief Deinitialize NeoPixel strip and free resources
 *
 * @param handle NeoPixel handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t neopixel_deinit(neopixel_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // ADAFRUIT_NEOPIXEL_H