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

// Colour constants (8-bit per channel)
const led_color_t LED_COLOR_GREEN  = {0, 255, 0};
const led_color_t LED_COLOR_YELLOW = {255, 255, 0};
const led_color_t LED_COLOR_RED    = {255, 0, 0};
const led_color_t LED_COLOR_OFF    = {0, 0, 0};

// Pattern definitions (8x8 bit patterns)
static const uint64_t PATTERN_GREEN_CHECK_BITS = 0x0018243C7E7E3C18ULL;
static const uint64_t PATTERN_YELLOW_WARN_BITS = 0x3C7EFFFFFFFF7E3CULL;
static const uint64_t PATTERN_RED_STOP_BITS     = 0xFFE7C3C3C3E7FFULL;
static const uint64_t PATTERN_ERROR_X_BITS      = 0xC3663C183C66C3ULL;

#define FONT5X7_WIDTH   5
#define FONT5X7_HEIGHT  7
#define FONT5X7_SPACING 1

// 5x7 ASCII font (subset 32..126) derived from the public-domain glcdfont
static const uint8_t FONT5X7[95][FONT5X7_WIDTH] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5f,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7f,0x14,0x7f,0x14}, // '#'
    {0x24,0x2a,0x7f,0x2a,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '''
    {0x00,0x1c,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1c,0x00}, // ')'
    {0x14,0x08,0x3e,0x08,0x14}, // '*'
    {0x08,0x08,0x3e,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3e,0x51,0x49,0x45,0x3e}, // '0'
    {0x00,0x42,0x7f,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4b,0x31}, // '3'
    {0x18,0x14,0x12,0x7f,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3c,0x4a,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1e}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3e}, // '@'
    {0x7e,0x11,0x11,0x11,0x7e}, // 'A'
    {0x7f,0x49,0x49,0x49,0x36}, // 'B'
    {0x3e,0x41,0x41,0x41,0x22}, // 'C'
    {0x7f,0x41,0x41,0x22,0x1c}, // 'D'
    {0x7f,0x49,0x49,0x49,0x41}, // 'E'
    {0x7f,0x09,0x09,0x09,0x01}, // 'F'
    {0x3e,0x41,0x49,0x49,0x7a}, // 'G'
    {0x7f,0x08,0x08,0x08,0x7f}, // 'H'
    {0x00,0x41,0x7f,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3f,0x01}, // 'J'
    {0x7f,0x08,0x14,0x22,0x41}, // 'K'
    {0x7f,0x40,0x40,0x40,0x40}, // 'L'
    {0x7f,0x02,0x0c,0x02,0x7f}, // 'M'
    {0x7f,0x04,0x08,0x10,0x7f}, // 'N'
    {0x3e,0x41,0x41,0x41,0x3e}, // 'O'
    {0x7f,0x09,0x09,0x09,0x06}, // 'P'
    {0x3e,0x41,0x51,0x21,0x5e}, // 'Q'
    {0x7f,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7f,0x01,0x01}, // 'T'
    {0x3f,0x40,0x40,0x40,0x3f}, // 'U'
    {0x1f,0x20,0x40,0x20,0x1f}, // 'V'
    {0x3f,0x40,0x38,0x40,0x3f}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7f,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\\'
    {0x00,0x41,0x41,0x7f,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x80,0x80,0x80,0x80,0x80}, // '_'
    {0x00,0x03,0x05,0x00,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7f,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7f}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7e,0x09,0x01,0x02}, // 'f'
    {0x0c,0x52,0x52,0x52,0x3e}, // 'g'
    {0x7f,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7d,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3d,0x00}, // 'j'
    {0x7f,0x10,0x28,0x44,0x00}, // 'k'
    {0x00,0x41,0x7f,0x40,0x00}, // 'l'
    {0x7c,0x04,0x18,0x04,0x78}, // 'm'
    {0x7c,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7c,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7c}, // 'q'
    {0x7c,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3f,0x44,0x40,0x20}, // 't'
    {0x3c,0x40,0x40,0x20,0x7c}, // 'u'
    {0x1c,0x20,0x40,0x20,0x1c}, // 'v'
    {0x3c,0x40,0x30,0x40,0x3c}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0c,0x50,0x50,0x50,0x3c}, // 'y'
    {0x44,0x64,0x54,0x4c,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7f,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x04,0x08,0x10,0x08}  // '~'
};

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

static inline const uint8_t *glyph_for_char(char c) {
    if (c < 32 || c > 126) {
        return FONT5X7[0];
    }
    return FONT5X7[c - 32];
}

static inline uint32_t color_to_neopixel(led_color_t color) {
    return neopixel_color(color.r, color.g, color.b);
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

int led_matrix_measure_text(const char *text) {
    if (!text) {
        return 0;
    }

    int width = 0;
    for (const char *p = text; *p; ++p) {
        width += FONT5X7_WIDTH;
        if (*(p + 1)) {
            width += FONT5X7_SPACING;
        }
    }

    return width;
}

esp_err_t led_matrix_draw_text_frame(const char *text, int16_t offset_x,
                                     led_color_t color, uint8_t brightness) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(text != NULL, ESP_ERR_INVALID_ARG, TAG, "text is NULL");

    if (brightness > LED_MAX_BRIGHTNESS) {
        ESP_LOGW(TAG, "Text brightness limited from %d to %d", brightness, LED_MAX_BRIGHTNESS);
        brightness = LED_MAX_BRIGHTNESS;
    }

    ESP_RETURN_ON_ERROR(neopixel_set_brightness(led_state.neopixel, brightness),
                        TAG, "Failed to set brightness");
    ESP_RETURN_ON_ERROR(neopixel_fill(led_state.neopixel, NEOPIXEL_COLOR_OFF),
                        TAG, "Failed to clear pixel buffer");

    uint32_t neo_color = color_to_neopixel(color);
    int16_t cursor = offset_x;

    for (const char *p = text; *p; ++p) {
        const uint8_t *glyph = glyph_for_char(*p);

        for (int col = 0; col < FONT5X7_WIDTH; ++col) {
            int16_t x = cursor + col;
            if (x < 0 || x >= LED_MATRIX_WIDTH) {
                continue;
            }

            uint8_t column_bits = glyph[col];
            for (int row = 0; row < FONT5X7_HEIGHT; ++row) {
                if (column_bits & (1 << row)) {
                    int16_t y = row;
                    if (y >= 0 && y < LED_MATRIX_HEIGHT) {
                        uint8_t index = xy_to_index((uint8_t)x, (uint8_t)y);
                        neopixel_set_pixel_color(led_state.neopixel, index, neo_color);
                    }
                }
            }
        }

        cursor += FONT5X7_WIDTH;
        if (*(p + 1)) {
            cursor += FONT5X7_SPACING;
        }
    }

    return neopixel_show(led_state.neopixel);
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
