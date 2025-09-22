/**
 * @file led_matrix.h
 * @brief WS2812B LED Matrix Controller for ESP32-S3
 *
 * Controls 8x8 RGB LED matrix using ESP32-S3 RMT peripheral.
 * Includes safety brightness limits and display patterns for fluid status.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Hardware configuration
#define LED_MATRIX_GPIO         14      ///< GPIO pin for LED data
#define LED_MATRIX_SIZE         64      ///< Total LEDs (8x8)
#define LED_MATRIX_WIDTH        8       ///< Matrix width
#define LED_MATRIX_HEIGHT       8       ///< Matrix height

// Safety limits - Following working examples
#define LED_MAX_BRIGHTNESS      5       ///< Maximum brightness (PROVEN WORKING LEVEL from HowdyTTS)
#define LED_DEFAULT_BRIGHTNESS  3       ///< Default brightness (proven safe level)

/**
 * @brief Display patterns for different fluid levels and demo modes
 */
typedef enum {
    PATTERN_OFF = 0,        ///< All LEDs off
    PATTERN_GREEN_CHECK,    ///< Green checkmark (fluid OK)
    PATTERN_YELLOW_WARN,    ///< Yellow warning triangle (low fluid)
    PATTERN_RED_STOP,       ///< Red stop sign (critical level)
    PATTERN_ERROR_BLINK,    ///< Blinking red X (sensor error)
    PATTERN_SELF_TEST,      ///< Self-test pattern (startup)
    // Demo mode patterns
    PATTERN_DEMO_USB,       ///< Demo mode indicator (USB power)
    PATTERN_DEMO_PINS,      ///< Pin assignment display
    PATTERN_DEMO_FLUID_OK,  ///< Demo: fluid OK animation
    PATTERN_DEMO_FLUID_LOW, ///< Demo: fluid low animation
    PATTERN_DEMO_FLUID_CRITICAL ///< Demo: fluid critical animation
} led_pattern_t;

/**
 * @brief LED RGB color structure
 */
typedef struct {
    uint8_t r;  ///< Red component (0-255)
    uint8_t g;  ///< Green component (0-255)
    uint8_t b;  ///< Blue component (0-255)
} led_color_t;

/**
 * @brief LED matrix configuration
 */
typedef struct {
    uint8_t brightness;     ///< Global brightness (0-5 max)
    uint32_t timeout_ms;    ///< Auto-off timeout
} led_config_t;

// Predefined colors
extern const led_color_t LED_COLOR_GREEN;
extern const led_color_t LED_COLOR_YELLOW;
extern const led_color_t LED_COLOR_RED;
extern const led_color_t LED_COLOR_OFF;

int led_matrix_measure_text(const char *text);

esp_err_t led_matrix_draw_text_frame(const char *text, int16_t offset_x,
                                     led_color_t color, uint8_t brightness);

/**
 * @brief Initialize LED matrix driver
 *
 * @param config Configuration parameters
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_init(const led_config_t *config);

/**
 * @brief Display a pattern on the LED matrix
 *
 * @param pattern Pattern to display
 * @param brightness Brightness level (0-5, capped at maximum)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_show_pattern(led_pattern_t pattern, uint8_t brightness);

/**
 * @brief Turn off all LEDs
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_clear(void);

/**
 * @brief Set individual pixel color
 *
 * @param x X coordinate (0-7)
 * @param y Y coordinate (0-7)
 * @param color RGB color
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG for out of bounds
 */
esp_err_t led_matrix_set_pixel(uint8_t x, uint8_t y, led_color_t color);

/**
 * @brief Update the physical LED matrix with current buffer
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_update(void);

/**
 * @brief Set global brightness with safety check
 *
 * @param brightness Desired brightness (automatically capped at LED_MAX_BRIGHTNESS)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_set_brightness(uint8_t brightness);

/**
 * @brief Get current brightness level
 *
 * @return Current brightness (0-5)
 */
uint8_t led_matrix_get_brightness(void);

/**
 * @brief Deinitialize LED matrix driver
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_deinit(void);

/**
 * @brief Start demo mode animation sequence
 *
 * Cycles through demo patterns showing pin assignments and fluid states
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_start_demo_mode(void);

/**
 * @brief Stop demo mode and clear display
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_matrix_stop_demo_mode(void);

#ifdef __cplusplus
}
#endif
