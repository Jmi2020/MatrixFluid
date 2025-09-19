/**
 * @file simple_test_main.c
 * @brief EMERGENCY LED SAFETY TEST - Replace main.c temporarily
 *
 * This is a minimal test that ONLY tests LED safety on startup.
 * Replace main/main.c with this file to test the emergency fix.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_matrix.h"
#include "driver/gpio.h"

static const char *TAG = "led_safety_test";

void app_main(void) {
    ESP_LOGI(TAG, "=== EMERGENCY LED SAFETY TEST ===");

    // Emergency GPIO clear first (hardware level)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_MATRIX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_MATRIX_GPIO, 0);

    ESP_LOGI(TAG, "STEP 1: Emergency GPIO clear completed");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Initialize with zero brightness
    led_config_t config = {
        .brightness = 0,  // CRITICAL: Zero brightness
        .timeout_ms = 5000
    };

    ESP_LOGI(TAG, "STEP 2: Initializing LED matrix with zero brightness...");
    esp_err_t ret = led_matrix_init(&config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CRITICAL: LED init failed: %s", esp_err_to_name(ret));
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGE(TAG, "UNSAFE: LED control failed!");
        }
    }

    ESP_LOGI(TAG, "STEP 3: LED matrix initialized - all LEDs should be OFF");
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "STEP 4: Explicit LED clear...");
    led_matrix_clear();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "STEP 5: Testing brightness=1 (minimal safe level)...");
    led_matrix_set_brightness(1);
    led_matrix_show_pattern(PATTERN_GREEN_CHECK, 1);

    ESP_LOGI(TAG, "You should see a very dim green checkmark");
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "STEP 6: Clearing LEDs again...");
    led_matrix_clear();

    ESP_LOGI(TAG, "=== SUCCESS: LED SAFETY TEST PASSED ===");
    ESP_LOGI(TAG, "LEDs are under control and safe!");

    // Keep monitoring
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "System stable - LEDs controlled");
    }
}