/**
 * @file main.c
 * @brief MatrixFluid - Vehicle Fluid Level Indicator Main Application
 *
 * ESP32-S3 based vehicle fluid level indicator with 8x8 RGB LED matrix display.
 * Features triple-tap gesture activation, two-level fluid sensing, and safety-limited brightness.
 *
 * Hardware:
 * - Waveshare ESP32-S3-Matrix board
 * - 8x8 WS2812B LED matrix (GPIO14)
 * - QMI8658 accelerometer (I2C: SDA=GPIO8, SCL=GPIO9)
 * - Fluid sensors (GPIO2=half-full, GPIO3=near-empty)
 *
 * Safety:
 * - LED brightness capped at 40/255 (~15%) to prevent overheating
 * - Triple-tap activation only (7-second auto-shutoff)
 * - Vehicle vibration filtering
 * - Default to caution on sensor errors
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// Component includes
#include "led_matrix.h"
#include "fluid_sensors.h"
#include "qmi8658_accel.h"
#include "tap_detector.h"

static const char *TAG = "matrixfluid";

// System configuration
#define DISPLAY_TIMEOUT_MS          7000    ///< Auto-off timeout (7 seconds)
#define SELF_TEST_DURATION_MS       2000    ///< Self-test pattern duration
#define STARTUP_DELAY_MS            1000    ///< Startup delay after boot

// System state
typedef enum {
    SYSTEM_STATE_INITIALIZING = 0,
    SYSTEM_STATE_SELF_TEST,
    SYSTEM_STATE_IDLE,
    SYSTEM_STATE_DISPLAYING,
    SYSTEM_STATE_ERROR
} system_state_t;

// Global system state
static struct {
    system_state_t state;
    fluid_level_t current_fluid_level;
    bool display_active;
    TimerHandle_t display_timer;
    uint32_t total_activations;
    uint32_t startup_time_ms;
} g_system = {0};

// Forward declarations
static void display_timeout_callback(TimerHandle_t timer);
static void triple_tap_detected_callback(const tap_event_t tap_events[3], void *user_ctx);
static void fluid_level_changed_callback(fluid_level_t new_level, fluid_level_t old_level, void *user_ctx);

/**
 * @brief Convert fluid level to LED pattern
 */
static led_pattern_t fluid_level_to_pattern(fluid_level_t level) {
    switch (level) {
        case FLUID_LEVEL_ABOVE_HALF:  return PATTERN_GREEN_CHECK;
        case FLUID_LEVEL_BELOW_HALF:  return PATTERN_YELLOW_WARN;
        case FLUID_LEVEL_NEAR_EMPTY:  return PATTERN_RED_STOP;
        case FLUID_LEVEL_SENSOR_ERROR:
        default:                      return PATTERN_ERROR_BLINK;
    }
}

/**
 * @brief Display current fluid level on LED matrix
 */
static esp_err_t display_fluid_level(void) {
    led_pattern_t pattern = fluid_level_to_pattern(g_system.current_fluid_level);

    ESP_LOGI(TAG, "Displaying fluid level: %s (pattern: %d)",
             fluid_level_to_string(g_system.current_fluid_level), pattern);

    esp_err_t ret = led_matrix_show_pattern(pattern, LED_DEFAULT_BRIGHTNESS);
    if (ret == ESP_OK) {
        g_system.display_active = true;
        g_system.state = SYSTEM_STATE_DISPLAYING;

        // Start display timeout timer
        if (g_system.display_timer) {
            xTimerStart(g_system.display_timer, pdMS_TO_TICKS(100));
        }

        g_system.total_activations++;
    }

    return ret;
}

/**
 * @brief Turn off display and return to idle state
 */
static esp_err_t turn_off_display(void) {
    ESP_LOGI(TAG, "Turning off display");

    esp_err_t ret = led_matrix_clear();
    if (ret == ESP_OK) {
        g_system.display_active = false;
        g_system.state = SYSTEM_STATE_IDLE;

        // Stop display timeout timer
        if (g_system.display_timer) {
            xTimerStop(g_system.display_timer, pdMS_TO_TICKS(100));
        }
    }

    return ret;
}

/**
 * @brief Display timeout callback - turns off display after timeout
 */
static void display_timeout_callback(TimerHandle_t timer) {
    ESP_LOGI(TAG, "Display timeout reached");
    turn_off_display();
}

/**
 * @brief Triple-tap detection callback
 */
static void triple_tap_detected_callback(const tap_event_t tap_events[3], void *user_ctx) {
    ESP_LOGI(TAG, "Triple-tap detected! Tap magnitudes: %.2fg, %.2fg, %.2fg",
             tap_events[0].magnitude_g, tap_events[1].magnitude_g, tap_events[2].magnitude_g);

    if (g_system.state == SYSTEM_STATE_IDLE) {
        // Display current fluid level
        esp_err_t ret = display_fluid_level();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to display fluid level: %s", esp_err_to_name(ret));
        }
    } else if (g_system.state == SYSTEM_STATE_DISPLAYING) {
        // Reset display timeout
        if (g_system.display_timer) {
            xTimerReset(g_system.display_timer, pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "Display timeout reset");
    }
}

/**
 * @brief Fluid level change callback
 */
static void fluid_level_changed_callback(fluid_level_t new_level, fluid_level_t old_level, void *user_ctx) {
    ESP_LOGI(TAG, "Fluid level changed: %s -> %s",
             fluid_level_to_string(old_level), fluid_level_to_string(new_level));

    g_system.current_fluid_level = new_level;

    // If display is currently active, update it immediately
    if (g_system.display_active) {
        esp_err_t ret = led_matrix_show_pattern(fluid_level_to_pattern(new_level), LED_DEFAULT_BRIGHTNESS);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update display: %s", esp_err_to_name(ret));
        }
    }

    // Log critical fluid level
    if (new_level == FLUID_LEVEL_NEAR_EMPTY) {
        ESP_LOGW(TAG, "WARNING: Fluid level critically low!");
    } else if (new_level == FLUID_LEVEL_SENSOR_ERROR) {
        ESP_LOGE(TAG, "ERROR: Fluid sensor malfunction detected!");
    }
}

/**
 * @brief Perform system self-test
 */
static esp_err_t perform_self_test(void) {
    ESP_LOGI(TAG, "Starting self-test...");
    g_system.state = SYSTEM_STATE_SELF_TEST;

    // Test LED matrix with self-test pattern
    esp_err_t ret = led_matrix_show_pattern(PATTERN_SELF_TEST, LED_DEFAULT_BRIGHTNESS);
    ESP_RETURN_ON_ERROR(ret, TAG, "LED matrix self-test failed");

    // Wait for self-test duration
    vTaskDelay(pdMS_TO_TICKS(SELF_TEST_DURATION_MS));

    // Test accelerometer
    ret = qmi8658_self_test();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Accelerometer self-test failed: %s", esp_err_to_name(ret));
        // Continue anyway - not critical
    }

    // Clear display and return to idle
    ret = led_matrix_clear();
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to clear display after self-test");

    g_system.state = SYSTEM_STATE_IDLE;
    ESP_LOGI(TAG, "Self-test completed");

    return ESP_OK;
}


/**
 * @brief Print system information
 */
static void print_system_info(void) {
    printf("\n");
    printf("=== MatrixFluid Vehicle Fluid Level Indicator ===\n");
    printf("Hardware: Waveshare ESP32-S3-Matrix\n");
    printf("Features: Triple-tap activation, 8x8 LED display, dual fluid sensors\n");
    printf("Safety: LED brightness limited to %d/255 (~2%%)\n", LED_MAX_BRIGHTNESS);
    printf("\n");

    // Print chip information
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    printf("Chip: %s with %d CPU core(s), revision v%d.%d\n",
           CONFIG_IDF_TARGET, chip_info.cores,
           chip_info.revision / 100, chip_info.revision % 100);

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("Flash: %lu MB %s\n", flash_size / (1024 * 1024),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    }

    printf("Free heap: %lu bytes\n", esp_get_free_heap_size());
    printf("Uptime: %lu ms\n", g_system.startup_time_ms);
    printf("\n");

    // Print sensor status
    printf("Sensors:\n");
    printf("  Accelerometer: %s\n", qmi8658_is_connected() ? "Connected" : "Disconnected");
    printf("  Fluid level: %s\n", fluid_level_to_string(g_system.current_fluid_level));
    printf("  Total activations: %lu\n", g_system.total_activations);
    printf("\n");
}

/**
 * @brief System monitoring task
 */
static void system_monitor_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(10000);  // 10 second intervals

    while (1) {
        // Print periodic status
        tap_stats_t tap_stats;
        if (tap_detector_get_stats(&tap_stats) == ESP_OK) {
            ESP_LOGI(TAG, "Status: State=%d, Fluid=%s, Taps=%lu, Detections=%lu, Errors=%lu",
                     g_system.state,
                     fluid_level_to_string(g_system.current_fluid_level),
                     tap_stats.total_taps,
                     tap_stats.triple_tap_count,
                     fluid_sensors_get_error_count());
        }

        // Check for critical conditions
        if (g_system.current_fluid_level == FLUID_LEVEL_SENSOR_ERROR) {
            ESP_LOGW(TAG, "Sensor error condition persisting");
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

/**
 * @brief Main application entry point
 */
void app_main(void) {
    esp_err_t ret;

    g_system.startup_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_system.state = SYSTEM_STATE_INITIALIZING;
    g_system.display_active = false;
    g_system.total_activations = 0;

    // EMERGENCY LED SAFETY - Clear LEDs IMMEDIATELY on startup
    ESP_LOGI(TAG, "EMERGENCY: Clearing all LEDs for safety...");
    led_config_t emergency_config = {
        .brightness = 0,  // Start with zero brightness
        .timeout_ms = DISPLAY_TIMEOUT_MS
    };

    // Initialize LED matrix with minimal, safe configuration
    ret = led_matrix_init(&emergency_config);
    if (ret == ESP_OK) {
        // Force clear all LEDs immediately
        led_matrix_clear();
        ESP_LOGI(TAG, "Emergency LED clear completed");
    } else {
        ESP_LOGE(TAG, "CRITICAL: Emergency LED clear failed: %s", esp_err_to_name(ret));
        // Try direct GPIO reset as last resort
        gpio_set_direction(LED_MATRIX_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(LED_MATRIX_GPIO, 0);
    }

    // Print system information
    print_system_info();

    // Initial startup delay
    ESP_LOGI(TAG, "Starting MatrixFluid system...");
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));

    // Initialize remaining components (LED matrix already initialized for safety)
    ESP_LOGI(TAG, "Initializing remaining system components...");

    // Initialize accelerometer
    qmi8658_config_t accel_config = {
        .accel_scale = 2,      // ±2g range
        .accel_odr = 250,      // 250Hz sampling
        .enable_fifo = false,
        .fifo_watermark = 0
    };
    ret = qmi8658_init(&accel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Accelerometer init failed: %s", esp_err_to_name(ret));
        // Continue without accelerometer - not critical for safety
    }

    // Initialize fluid sensors
    fluid_config_t fluid_config = {
        .debounce_ms = 100,
        .read_interval_ms = 50,
        .enable_monitoring = true
    };
    ret = fluid_sensors_init(&fluid_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fluid sensors init failed: %s", esp_err_to_name(ret));
        // Continue - will show error pattern
    }

    // Initialize tap detector
    tap_config_t tap_config = {
        .threshold_g = 1.5f,
        .window_ms = 500,
        .min_interval_ms = 150,
        .max_interval_ms = 500,
        .debounce_ms = 1000,
        .filter_vibration = true,
        .vibration_threshold = 0.5f
    };
    ret = tap_detector_init(&tap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tap detector init failed: %s", esp_err_to_name(ret));
        // Continue - device will be always-on for debugging
    }

    // Create display timeout timer
    g_system.display_timer = xTimerCreate(
        "display_timeout",
        pdMS_TO_TICKS(DISPLAY_TIMEOUT_MS),
        pdFALSE,  // One-shot timer
        NULL,
        display_timeout_callback
    );
    if (!g_system.display_timer) {
        ESP_LOGE(TAG, "Failed to create display timer");
    }

    // Register callbacks if components initialized successfully
    if (tap_detector_register_callback(triple_tap_detected_callback, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Tap callback registered");
    }

    if (fluid_sensors_register_callback(fluid_level_changed_callback, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Fluid callback registered");
    }

    // Start tap detection if available
    if (tap_detector_start_auto() == ESP_OK) {
        ESP_LOGI(TAG, "Tap detection started");
    }

    // Get initial fluid level
    g_system.current_fluid_level = fluid_sensors_get_level();

    ESP_LOGI(TAG, "System initialization completed");
    ESP_LOGI(TAG, "Initial fluid level: %s", fluid_level_to_string(g_system.current_fluid_level));

    // Perform self-test
    ret = perform_self_test();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test failed: %s", esp_err_to_name(ret));
        g_system.state = SYSTEM_STATE_ERROR;
        led_matrix_show_pattern(PATTERN_ERROR_BLINK, LED_DEFAULT_BRIGHTNESS);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    ESP_LOGI(TAG, "System ready! Triple-tap to activate display.");
    ESP_LOGI(TAG, "Current fluid level: %s", fluid_level_to_string(g_system.current_fluid_level));

    // Create system monitoring task
    xTaskCreate(
        system_monitor_task,
        "system_monitor",
        2048,
        NULL,
        2,  // Low priority
        NULL
    );

    // Main loop - system is now event-driven via callbacks
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Handle error recovery
        if (g_system.state == SYSTEM_STATE_ERROR) {
            ESP_LOGE(TAG, "System in error state - attempting recovery...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }
    }
}