/**
 * @file led_matrix_neopixel.c
 * @brief LED Matrix Implementation using Adafruit_NeoPixel
 *
 * Drop-in replacement for led_matrix.c using the reliable Adafruit_NeoPixel library
 * Maintains the same API interface while providing maximum stability
 */

#include "led_matrix.h"
#include "adafruit_neopixel.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "led_matrix_neo";

// Pattern definitions (8x8 bit patterns)
static const uint64_t PATTERN_GREEN_CHECK_BITS = 0x0018243C7E7E3C18ULL;
static const uint64_t PATTERN_YELLOW_WARN_BITS = 0x3C7EFFFFFFFF7E3CULL;
static const uint64_t PATTERN_RED_STOP_BITS     = 0xFFE7C3C3C3E7FFULL;
static const uint64_t PATTERN_ERROR_X_BITS      = 0xC3663C183C66C3ULL;

// Module state - simple structure with NeoPixel handle
static struct {
    neopixel_handle_t neopixel;
    uint8_t current_brightness;
    bool initialized;
} led_state = {0};

/**
 * @brief Convert X,Y coordinates to linear buffer index
 */
static inline uint8_t xy_to_index(uint8_t x, uint8_t y) {
    return y * LED_MATRIX_WIDTH + x;
}

/**
 * @brief Set pattern from 64-bit bitmap
 */
static void set_pattern_from_bitmap(uint64_t bitmap, uint32_t color) {
    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            uint8_t bit_pos = y * 8 + x;
            bool pixel_on = (bitmap >> (63 - bit_pos)) & 1;
            uint8_t index = xy_to_index(x, y);

            if (pixel_on) {
                neopixel_set_pixel_color(led_state.neopixel, index, color);
            } else {
                neopixel_set_pixel_color(led_state.neopixel, index, NEOPIXEL_COLOR_OFF);
            }
        }
    }
}

/**
 * @brief Emergency LED clear - immediate hardware safety
 */
static void emergency_led_clear(void) {
    ESP_LOGI(TAG, "EMERGENCY: Clearing LEDs immediately");

    // Set GPIO low immediately
    gpio_config_t emergency_config = {
        .pin_bit_mask = (1ULL << LED_MATRIX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&emergency_config);
    gpio_set_level(LED_MATRIX_GPIO, 0);

    // Wait for WS2812B reset (>50us)
    vTaskDelay(pdMS_TO_TICKS(1));
}

esp_err_t led_matrix_init(const led_config_t *config) {
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");

    if (led_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // EMERGENCY SAFETY: Clear LEDs immediately
    emergency_led_clear();

    // Safety: Limit brightness to working example levels
    led_state.current_brightness = config->brightness;
    if (led_state.current_brightness > LED_MAX_BRIGHTNESS) {
        ESP_LOGW(TAG, "Brightness limited from %d to %d for safety",
                 led_state.current_brightness, LED_MAX_BRIGHTNESS);
        led_state.current_brightness = LED_MAX_BRIGHTNESS;
    }

    // Initialize NeoPixel configuration
    neopixel_config_t neo_config = {
        .num_leds = LED_MATRIX_SIZE,
        .pin = LED_MATRIX_GPIO,
        .brightness = led_state.current_brightness
    };

    esp_err_t ret = neopixel_init(&neo_config, &led_state.neopixel);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to initialize NeoPixel");

    ret = neopixel_begin(led_state.neopixel);
    if (ret != ESP_OK) {
        neopixel_deinit(led_state.neopixel);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to begin NeoPixel");
    }

    led_state.initialized = true;

    // CRITICAL: Send immediate "all LEDs off" to hardware
    ESP_LOGI(TAG, "Sending emergency LED clear to hardware...");
    ret = neopixel_clear(led_state.neopixel);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Emergency LED clear successful");
    } else {
        ESP_LOGE(TAG, "CRITICAL: Emergency LED clear failed: %s", esp_err_to_name(ret));
        // Don't fail init - GPIO is already set low
    }

    ESP_LOGI(TAG, "LED matrix initialized with NeoPixel: brightness=%d, GPIO=%d",
             led_state.current_brightness, LED_MATRIX_GPIO);

    return ESP_OK;
}

esp_err_t led_matrix_show_pattern(led_pattern_t pattern, uint8_t brightness) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    // Safety check brightness
    if (brightness > LED_MAX_BRIGHTNESS) {
        ESP_LOGW(TAG, "Pattern brightness limited from %d to %d", brightness, LED_MAX_BRIGHTNESS);
        brightness = LED_MAX_BRIGHTNESS;
    }

    // Update NeoPixel brightness for this pattern
    esp_err_t ret = neopixel_set_brightness(led_state.neopixel, brightness);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to set brightness");

    switch (pattern) {
        case PATTERN_OFF:
            ret = neopixel_clear(led_state.neopixel);
            break;

        case PATTERN_GREEN_CHECK:
            set_pattern_from_bitmap(PATTERN_GREEN_CHECK_BITS, NEOPIXEL_COLOR_GREEN);
            ret = neopixel_show(led_state.neopixel);
            break;

        case PATTERN_YELLOW_WARN:
            set_pattern_from_bitmap(PATTERN_YELLOW_WARN_BITS, NEOPIXEL_COLOR_YELLOW);
            ret = neopixel_show(led_state.neopixel);
            break;

        case PATTERN_RED_STOP:
            set_pattern_from_bitmap(PATTERN_RED_STOP_BITS, NEOPIXEL_COLOR_RED);
            ret = neopixel_show(led_state.neopixel);
            break;

        case PATTERN_ERROR_BLINK:
            set_pattern_from_bitmap(PATTERN_ERROR_X_BITS, NEOPIXEL_COLOR_RED);
            ret = neopixel_show(led_state.neopixel);
            break;

        case PATTERN_SELF_TEST:
            // Simple color blocks for self-test
            for (int i = 0; i < LED_MATRIX_SIZE; i++) {
                uint32_t color;
                if (i < LED_MATRIX_SIZE / 3) {
                    color = NEOPIXEL_COLOR_RED;
                } else if (i < 2 * LED_MATRIX_SIZE / 3) {
                    color = NEOPIXEL_COLOR_GREEN;
                } else {
                    color = NEOPIXEL_COLOR_BLUE;  // Use blue instead of yellow for clarity
                }
                neopixel_set_pixel_color(led_state.neopixel, i, color);
            }
            ret = neopixel_show(led_state.neopixel);
            break;

        default:
            ESP_LOGE(TAG, "Unknown pattern: %d", pattern);
            return ESP_ERR_INVALID_ARG;
    }

    return ret;
}

esp_err_t led_matrix_clear(void) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    return neopixel_clear(led_state.neopixel);
}

esp_err_t led_matrix_set_pixel(uint8_t x, uint8_t y, led_color_t color) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(x < LED_MATRIX_WIDTH && y < LED_MATRIX_HEIGHT,
                        ESP_ERR_INVALID_ARG, TAG, "coordinates out of bounds");

    uint8_t index = xy_to_index(x, y);
    uint32_t neo_color = neopixel_color(color.r, color.g, color.b);

    return neopixel_set_pixel_color(led_state.neopixel, index, neo_color);
}

esp_err_t led_matrix_update(void) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    return neopixel_show(led_state.neopixel);
}

esp_err_t led_matrix_set_brightness(uint8_t brightness) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (brightness > LED_MAX_BRIGHTNESS) {
        ESP_LOGW(TAG, "Brightness limited from %d to %d for safety", brightness, LED_MAX_BRIGHTNESS);
        brightness = LED_MAX_BRIGHTNESS;
    }

    led_state.current_brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d", brightness);

    return neopixel_set_brightness(led_state.neopixel, brightness);
}

uint8_t led_matrix_get_brightness(void) {
    return led_state.initialized ? neopixel_get_brightness(led_state.neopixel) : 0;
}

esp_err_t led_matrix_deinit(void) {
    if (!led_state.initialized) {
        return ESP_OK;
    }

    // Clear display first
    neopixel_clear(led_state.neopixel);

    // Deinitialize NeoPixel
    esp_err_t ret = neopixel_deinit(led_state.neopixel);
    led_state.neopixel = NULL;

    led_state.initialized = false;
    ESP_LOGI(TAG, "LED matrix deinitialized");

    return ret;
}