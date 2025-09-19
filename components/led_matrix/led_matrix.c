/**
 * @file led_matrix.c
 * @brief Simple, Safe WS2812B LED Matrix Implementation
 *
 * SAFETY-FIRST approach using direct RMT calls. No dynamic allocation.
 * Based on working ESP-IDF examples. Maximum brightness 15 for safety.
 */

#include "led_matrix.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "led_matrix";

// WS2812B timing parameters (microseconds)
#define WS2812_T0H    0.4f    // 0 bit high time
#define WS2812_T0L    0.85f   // 0 bit low time
#define WS2812_T1H    0.8f    // 1 bit high time
#define WS2812_T1L    0.45f   // 1 bit low time
#define WS2812_RES    50.0f   // Reset time

// RMT timing (10MHz = 100ns per tick)
#define RMT_RESOLUTION_HZ 10000000
#define RMT_TICK_NS       100

// Convert microseconds to RMT ticks
#define US_TO_TICKS(us) ((uint32_t)((us) * RMT_RESOLUTION_HZ / 1000000))

// Predefined colors
const led_color_t LED_COLOR_GREEN  = {0, 255, 0};
const led_color_t LED_COLOR_YELLOW = {255, 255, 0};
const led_color_t LED_COLOR_RED    = {255, 0, 0};
const led_color_t LED_COLOR_OFF    = {0, 0, 0};

// Pattern definitions (8x8 bit patterns)
static const uint64_t PATTERN_GREEN_CHECK_BITS = 0x0018243C7E7E3C18ULL;
static const uint64_t PATTERN_YELLOW_WARN_BITS = 0x3C7EFFFFFFFF7E3CULL;
static const uint64_t PATTERN_RED_STOP_BITS     = 0xFFE7C3C3C3E7FFULL;
static const uint64_t PATTERN_ERROR_X_BITS      = 0xC3663C183C66C3ULL;

// Simple module state - no dynamic allocation
static struct {
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    led_color_t framebuffer[LED_MATRIX_SIZE];
    uint8_t current_brightness;
    bool initialized;
} led_state = {0};

// Static reset symbol - no malloc
static rmt_symbol_word_t reset_symbol = {
    .level0 = 0, .duration0 = US_TO_TICKS(WS2812_RES),
    .level1 = 0, .duration1 = US_TO_TICKS(WS2812_RES)
};

/**
 * @brief Apply brightness scaling to color
 */
static led_color_t apply_brightness(led_color_t color, uint8_t brightness) {
    led_color_t result;
    result.r = (color.r * brightness) / 255;
    result.g = (color.g * brightness) / 255;
    result.b = (color.b * brightness) / 255;
    return result;
}

/**
 * @brief Convert X,Y coordinates to linear buffer index
 */
static inline uint8_t xy_to_index(uint8_t x, uint8_t y) {
    return y * LED_MATRIX_WIDTH + x;
}

/**
 * @brief Set pattern from 64-bit bitmap
 */
static void set_pattern_from_bitmap(uint64_t bitmap, led_color_t color) {
    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            uint8_t bit_pos = y * 8 + x;
            bool pixel_on = (bitmap >> (63 - bit_pos)) & 1;
            uint8_t index = xy_to_index(x, y);

            if (pixel_on) {
                led_state.framebuffer[index] = apply_brightness(color, led_state.current_brightness);
            } else {
                led_state.framebuffer[index] = LED_COLOR_OFF;
            }
        }
    }
}

/**
 * @brief Simple RMT transmission - send RGB data + reset
 */
static esp_err_t transmit_rgb_data(void) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    // Convert RGB to GRB format (WS2812B byte order)
    static uint8_t led_data[LED_MATRIX_SIZE * 3];
    for (int i = 0; i < LED_MATRIX_SIZE; i++) {
        led_data[i * 3 + 0] = led_state.framebuffer[i].g;  // Green first
        led_data[i * 3 + 1] = led_state.framebuffer[i].r;  // Red second
        led_data[i * 3 + 2] = led_state.framebuffer[i].b;  // Blue third
    }

    rmt_transmit_config_t tx_config = { .loop_count = 0 };

    // Send RGB data
    esp_err_t ret = rmt_transmit(led_state.rmt_chan, led_state.bytes_encoder,
                                led_data, sizeof(led_data), &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB data transmission failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for completion
    esp_err_t wait_ret = rmt_tx_wait_all_done(led_state.rmt_chan, 1000);
    if (wait_ret != ESP_OK) {
        ESP_LOGW(TAG, "RGB transmission wait timeout");
    }

    // Send reset pulse
    ret = rmt_transmit(led_state.rmt_chan, led_state.copy_encoder,
                      &reset_symbol, sizeof(reset_symbol), &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reset pulse transmission failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
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

    // Clear framebuffer
    memset(led_state.framebuffer, 0, sizeof(led_state.framebuffer));

    // Configure RMT channel
    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_MATRIX_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,  // Simple queue
        .flags.invert_out = false,
        .flags.with_dma = false,
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_config, &led_state.rmt_chan);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create RMT channel");

    // Create bytes encoder for RGB data
    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .level0 = 1, .duration0 = US_TO_TICKS(WS2812_T0H),
            .level1 = 0, .duration1 = US_TO_TICKS(WS2812_T0L),
        },
        .bit1 = {
            .level0 = 1, .duration0 = US_TO_TICKS(WS2812_T1H),
            .level1 = 0, .duration1 = US_TO_TICKS(WS2812_T1L),
        },
        .flags.msb_first = 1,
    };

    ret = rmt_new_bytes_encoder(&bytes_config, &led_state.bytes_encoder);
    if (ret != ESP_OK) {
        rmt_del_channel(led_state.rmt_chan);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create bytes encoder");
    }

    // Create copy encoder for reset symbol
    rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &led_state.copy_encoder);
    if (ret != ESP_OK) {
        rmt_del_encoder(led_state.bytes_encoder);
        rmt_del_channel(led_state.rmt_chan);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create copy encoder");
    }

    // Enable RMT channel
    ret = rmt_enable(led_state.rmt_chan);
    if (ret != ESP_OK) {
        rmt_del_encoder(led_state.copy_encoder);
        rmt_del_encoder(led_state.bytes_encoder);
        rmt_del_channel(led_state.rmt_chan);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to enable RMT channel");
    }

    led_state.initialized = true;

    // CRITICAL: Send immediate "all LEDs off" to hardware
    ESP_LOGI(TAG, "Sending emergency LED clear to hardware...");
    memset(led_state.framebuffer, 0, sizeof(led_state.framebuffer));
    ret = transmit_rgb_data();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Emergency LED clear successful");
    } else {
        ESP_LOGE(TAG, "CRITICAL: Emergency LED clear failed: %s", esp_err_to_name(ret));
        // Don't fail init - GPIO is already set low
    }

    ESP_LOGI(TAG, "LED matrix initialized: brightness=%d, GPIO=%d",
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

    // Temporarily update brightness for this pattern
    uint8_t old_brightness = led_state.current_brightness;
    led_state.current_brightness = brightness;

    switch (pattern) {
        case PATTERN_OFF:
            memset(led_state.framebuffer, 0, sizeof(led_state.framebuffer));
            break;

        case PATTERN_GREEN_CHECK:
            set_pattern_from_bitmap(PATTERN_GREEN_CHECK_BITS, LED_COLOR_GREEN);
            break;

        case PATTERN_YELLOW_WARN:
            set_pattern_from_bitmap(PATTERN_YELLOW_WARN_BITS, LED_COLOR_YELLOW);
            break;

        case PATTERN_RED_STOP:
            set_pattern_from_bitmap(PATTERN_RED_STOP_BITS, LED_COLOR_RED);
            break;

        case PATTERN_ERROR_BLINK:
            set_pattern_from_bitmap(PATTERN_ERROR_X_BITS, LED_COLOR_RED);
            break;

        case PATTERN_SELF_TEST:
            // Simple color blocks for self-test
            for (int i = 0; i < LED_MATRIX_SIZE; i++) {
                if (i < LED_MATRIX_SIZE / 3) {
                    led_state.framebuffer[i] = apply_brightness(LED_COLOR_RED, brightness);
                } else if (i < 2 * LED_MATRIX_SIZE / 3) {
                    led_state.framebuffer[i] = apply_brightness(LED_COLOR_GREEN, brightness);
                } else {
                    led_state.framebuffer[i] = apply_brightness(LED_COLOR_YELLOW, brightness);
                }
            }
            break;

        default:
            ESP_LOGE(TAG, "Unknown pattern: %d", pattern);
            led_state.current_brightness = old_brightness;
            return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = transmit_rgb_data();
    led_state.current_brightness = old_brightness;

    return ret;
}

esp_err_t led_matrix_clear(void) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    memset(led_state.framebuffer, 0, sizeof(led_state.framebuffer));
    return transmit_rgb_data();
}

esp_err_t led_matrix_set_pixel(uint8_t x, uint8_t y, led_color_t color) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(x < LED_MATRIX_WIDTH && y < LED_MATRIX_HEIGHT,
                        ESP_ERR_INVALID_ARG, TAG, "coordinates out of bounds");

    uint8_t index = xy_to_index(x, y);
    led_state.framebuffer[index] = apply_brightness(color, led_state.current_brightness);

    return ESP_OK;
}

esp_err_t led_matrix_update(void) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    return transmit_rgb_data();
}

esp_err_t led_matrix_set_brightness(uint8_t brightness) {
    ESP_RETURN_ON_FALSE(led_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (brightness > LED_MAX_BRIGHTNESS) {
        ESP_LOGW(TAG, "Brightness limited from %d to %d for safety", brightness, LED_MAX_BRIGHTNESS);
        brightness = LED_MAX_BRIGHTNESS;
    }

    led_state.current_brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d", brightness);

    return ESP_OK;
}

uint8_t led_matrix_get_brightness(void) {
    return led_state.initialized ? led_state.current_brightness : 0;
}

esp_err_t led_matrix_deinit(void) {
    if (!led_state.initialized) {
        return ESP_OK;
    }

    // Clear display first
    memset(led_state.framebuffer, 0, sizeof(led_state.framebuffer));
    transmit_rgb_data();

    // Cleanup resources
    if (led_state.copy_encoder) {
        rmt_del_encoder(led_state.copy_encoder);
        led_state.copy_encoder = NULL;
    }

    if (led_state.bytes_encoder) {
        rmt_del_encoder(led_state.bytes_encoder);
        led_state.bytes_encoder = NULL;
    }

    if (led_state.rmt_chan) {
        rmt_disable(led_state.rmt_chan);
        rmt_del_channel(led_state.rmt_chan);
        led_state.rmt_chan = NULL;
    }

    led_state.initialized = false;
    ESP_LOGI(TAG, "LED matrix deinitialized");

    return ESP_OK;
}