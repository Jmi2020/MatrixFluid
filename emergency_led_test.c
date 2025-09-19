/**
 * @file emergency_led_test.c
 * @brief EMERGENCY LED SAFETY TEST
 *
 * Minimal test program to verify LEDs are immediately cleared on startup.
 * This should be the first thing tested before any complex functionality.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_matrix.h"

static const char *TAG = "led_emergency_test";

void app_main(void) {
    ESP_LOGI(TAG, "=== EMERGENCY LED SAFETY TEST ===");
    ESP_LOGI(TAG, "This test verifies LEDs are immediately cleared on startup");

    // Emergency LED clear test
    led_config_t config = {
        .brightness = 0,  // Zero brightness for safety
        .timeout_ms = 5000
    };

    ESP_LOGI(TAG, "Step 1: Initialize LED matrix with emergency clear...");
    esp_err_t ret = led_matrix_init(&config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CRITICAL FAILURE: LED matrix init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "System is UNSAFE - LEDs may be uncontrolled!");
        return;
    }

    ESP_LOGI(TAG, "Step 2: Force LED clear...");
    ret = led_matrix_clear();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CRITICAL: LED clear failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SUCCESS: All LEDs should now be OFF");
    }

    ESP_LOGI(TAG, "Step 3: Wait 3 seconds to verify LEDs stay off...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Step 4: Test minimal brightness (brightness=1)...");
    ret = led_matrix_show_pattern(PATTERN_SELF_TEST, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Minimal brightness test pattern shown");
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "Step 5: Clear LEDs again...");
        led_matrix_clear();
        ESP_LOGI(TAG, "All LEDs should be OFF again");
    }

    ESP_LOGI(TAG, "=== EMERGENCY LED SAFETY TEST COMPLETE ===");
    ESP_LOGI(TAG, "If you see this message and LEDs are OFF, the safety fix worked!");

    // Keep system alive for monitoring
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "System stable, LEDs controlled");
    }
}