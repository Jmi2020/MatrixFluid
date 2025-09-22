/**
 * @file adafruit_neopixel.c
 * @brief Adafruit_NeoPixel-like library implementation for ESP32
 *
 * Simple, reliable NeoPixel control using ESP32 RMT driver
 * Safety-first approach with maximum brightness limits
 */

#include "adafruit_neopixel.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "neopixel";

// WS2812B timing parameters (nanoseconds)
#define WS2812_T0H_NS    400    // 0 bit high time
#define WS2812_T0L_NS    850    // 0 bit low time
#define WS2812_T1H_NS    800    // 1 bit high time
#define WS2812_T1L_NS    450    // 1 bit low time
#define WS2812_RESET_NS  50000  // Reset time

// RMT configuration
#define RMT_RESOLUTION_HZ    10000000  // 10MHz, 100ns resolution

/**
 * @brief NeoPixel instance structure
 */
struct neopixel_s {
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    uint32_t* pixel_data;
    uint16_t num_pixels;
    gpio_num_t pin;
    uint8_t brightness;
    bool initialized;
};

// Static reset symbol for WS2812B
static const rmt_symbol_word_t ws2812_reset_symbol = {
    .level0 = 0,
    .duration0 = WS2812_RESET_NS / 100,  // Convert to 100ns ticks
    .level1 = 0,
    .duration1 = WS2812_RESET_NS / 100
};

/**
 * @brief Apply brightness scaling to color
 */
static uint32_t apply_brightness(uint32_t color, uint8_t brightness) {
    if (brightness >= 255) return color;

    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8) & 0xFF) * brightness / 255;
    uint8_t b = (color & 0xFF) * brightness / 255;

    return neopixel_color(r, g, b);
}

esp_err_t neopixel_init(const neopixel_config_t* config, neopixel_handle_t* handle) {
    ESP_RETURN_ON_FALSE(config && handle, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(config->num_leds > 0 && config->num_leds <= 256, ESP_ERR_INVALID_ARG, TAG, "Invalid LED count");

    // Allocate handle
    struct neopixel_s* neo = calloc(1, sizeof(struct neopixel_s));
    ESP_RETURN_ON_FALSE(neo, ESP_ERR_NO_MEM, TAG, "Failed to allocate handle");

    // Initialize handle
    neo->num_pixels = config->num_leds;
    neo->pin = config->pin;
    neo->brightness = (config->brightness > NEOPIXEL_MAX_BRIGHTNESS) ? NEOPIXEL_MAX_BRIGHTNESS : config->brightness;
    neo->initialized = false;

    if (neo->brightness != config->brightness) {
        ESP_LOGW(TAG, "Brightness limited from %d to %d for safety", config->brightness, neo->brightness);
    }

    // Allocate pixel buffer
    neo->pixel_data = calloc(neo->num_pixels, sizeof(uint32_t));
    if (!neo->pixel_data) {
        free(neo);
        ESP_RETURN_ON_ERROR(ESP_ERR_NO_MEM, TAG, "Failed to allocate pixel buffer");
    }

    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = neo->pin,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
        .flags.invert_out = false,
        .flags.with_dma = false
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_config, &neo->rmt_channel);
    if (ret != ESP_OK) {
        free(neo->pixel_data);
        free(neo);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create RMT channel");
    }

    // Create bytes encoder for WS2812B
    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2812_T0H_NS / 100,
            .level1 = 0,
            .duration1 = WS2812_T0L_NS / 100
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = WS2812_T1H_NS / 100,
            .level1 = 0,
            .duration1 = WS2812_T1L_NS / 100
        },
        .flags.msb_first = 1
    };

    ret = rmt_new_bytes_encoder(&bytes_config, &neo->bytes_encoder);
    if (ret != ESP_OK) {
        rmt_del_channel(neo->rmt_channel);
        free(neo->pixel_data);
        free(neo);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create bytes encoder");
    }

    // Create copy encoder for reset symbol
    rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &neo->copy_encoder);
    if (ret != ESP_OK) {
        rmt_del_encoder(neo->bytes_encoder);
        rmt_del_channel(neo->rmt_channel);
        free(neo->pixel_data);
        free(neo);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create copy encoder");
    }

    *handle = neo;
    ESP_LOGI(TAG, "NeoPixel initialized: %d pixels on GPIO%d, brightness=%d",
             neo->num_pixels, neo->pin, neo->brightness);

    return ESP_OK;
}

esp_err_t neopixel_begin(neopixel_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Handle is NULL");

    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    esp_err_t ret = rmt_enable(handle->rmt_channel);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to enable RMT channel");

    handle->initialized = true;

    // Clear all pixels on begin (safety)
    ESP_LOGI(TAG, "NeoPixel begin: clearing all pixels for safety");
    return neopixel_clear(handle);
}

esp_err_t neopixel_set_pixel_color(neopixel_handle_t handle, uint16_t pixel_index, uint32_t color) {
    ESP_RETURN_ON_FALSE(handle && handle->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    ESP_RETURN_ON_FALSE(pixel_index < handle->num_pixels, ESP_ERR_INVALID_ARG, TAG, "Pixel index out of bounds");

    handle->pixel_data[pixel_index] = color;
    return ESP_OK;
}

esp_err_t neopixel_set_pixel_rgb(neopixel_handle_t handle, uint16_t pixel_index, uint8_t r, uint8_t g, uint8_t b) {
    return neopixel_set_pixel_color(handle, pixel_index, neopixel_color(r, g, b));
}

esp_err_t neopixel_show(neopixel_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle && handle->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    // Convert RGB pixel data to GRB format for WS2812B
    uint8_t* led_data = malloc(handle->num_pixels * 3);
    ESP_RETURN_ON_FALSE(led_data, ESP_ERR_NO_MEM, TAG, "Failed to allocate LED data buffer");

    for (int i = 0; i < handle->num_pixels; i++) {
        uint32_t color = apply_brightness(handle->pixel_data[i], handle->brightness);

        // Waveshare panel expects RGB order
        led_data[i * 3 + 0] = neopixel_color_red(color);
        led_data[i * 3 + 1] = neopixel_color_green(color);
        led_data[i * 3 + 2] = neopixel_color_blue(color);
    }

    // Transmit LED data
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    esp_err_t ret = rmt_transmit(handle->rmt_channel, handle->bytes_encoder,
                                led_data, handle->num_pixels * 3, &tx_config);

    if (ret == ESP_OK) {
        // Wait for transmission completion
        ret = rmt_tx_wait_all_done(handle->rmt_channel, 1000);
        if (ret == ESP_OK) {
            // Send reset pulse
            ret = rmt_transmit(handle->rmt_channel, handle->copy_encoder,
                              &ws2812_reset_symbol, sizeof(ws2812_reset_symbol), &tx_config);
        }
    }

    free(led_data);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit LED data: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t neopixel_clear(neopixel_handle_t handle) {
    ESP_RETURN_ON_FALSE(handle && handle->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    memset(handle->pixel_data, 0, handle->num_pixels * sizeof(uint32_t));
    return neopixel_show(handle);
}

esp_err_t neopixel_fill(neopixel_handle_t handle, uint32_t color) {
    ESP_RETURN_ON_FALSE(handle && handle->initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    for (int i = 0; i < handle->num_pixels; i++) {
        handle->pixel_data[i] = color;
    }

    return ESP_OK;  // Note: Call neopixel_show() to actually update the strip
}

esp_err_t neopixel_set_brightness(neopixel_handle_t handle, uint8_t brightness) {
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Handle is NULL");

    if (brightness > NEOPIXEL_MAX_BRIGHTNESS) {
        ESP_LOGW(TAG, "Brightness limited from %d to %d for safety", brightness, NEOPIXEL_MAX_BRIGHTNESS);
        brightness = NEOPIXEL_MAX_BRIGHTNESS;
    }

    handle->brightness = brightness;
    ESP_LOGI(TAG, "Brightness set to %d", brightness);

    return ESP_OK;
}

uint8_t neopixel_get_brightness(neopixel_handle_t handle) {
    return handle ? handle->brightness : 0;
}

uint16_t neopixel_num_pixels(neopixel_handle_t handle) {
    return handle ? handle->num_pixels : 0;
}

esp_err_t neopixel_deinit(neopixel_handle_t handle) {
    if (!handle) return ESP_OK;

    if (handle->initialized) {
        // Clear display first
        neopixel_clear(handle);
        rmt_disable(handle->rmt_channel);
    }

    // Clean up encoders
    if (handle->copy_encoder) {
        rmt_del_encoder(handle->copy_encoder);
    }
    if (handle->bytes_encoder) {
        rmt_del_encoder(handle->bytes_encoder);
    }

    // Clean up RMT channel
    if (handle->rmt_channel) {
        rmt_del_channel(handle->rmt_channel);
    }

    // Free buffers
    if (handle->pixel_data) {
        free(handle->pixel_data);
    }

    free(handle);

    ESP_LOGI(TAG, "NeoPixel deinitialized");
    return ESP_OK;
}
