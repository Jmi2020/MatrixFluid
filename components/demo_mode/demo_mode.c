/**
 * @file demo_mode.c
 * @brief Demo Mode Detection and Management Implementation
 */

#include <stdio.h>
#include <string.h>
#include "demo_mode.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/usb_serial_jtag.h"

// Component includes
#include "led_matrix.h"

static const char *TAG = "demo_mode";

// Demo mode state
static struct {
    bool initialized;
    demo_config_t config;
    demo_mode_state_t state;
    power_source_t power_source;
    uint32_t last_check_time;
    uint32_t demo_start_time;
    TimerHandle_t demo_timer;
    bool demo_active;
    bool auto_requested;
} g_demo = {0};

// Forward declarations
static void demo_timer_callback(TimerHandle_t timer);
static esp_err_t detect_power_source(void);

static esp_err_t detect_power_source(void) {
    power_source_t previous = g_demo.power_source;

    if (!g_demo.config.enable_detection) {
        g_demo.power_source = POWER_SOURCE_UNKNOWN;
        g_demo.auto_requested = false;
        return ESP_OK;
    }

    bool usb_connected = usb_serial_jtag_is_connected();
    g_demo.power_source = usb_connected ? POWER_SOURCE_USB : POWER_SOURCE_5V_BUCK;
    g_demo.auto_requested = usb_connected;

    if (previous != g_demo.power_source) {
        ESP_LOGI(TAG, "Power source changed: %s",
                 demo_mode_power_source_to_string(g_demo.power_source));
    }

    return ESP_OK;
}

/**
 * @brief Demo timer callback for animations
 */
static void demo_timer_callback(TimerHandle_t timer) {
    if (!g_demo.demo_active) {
        return;
    }

    static uint8_t demo_step = 0;
    esp_err_t ret = ESP_OK;

    switch (demo_step) {
        case 0:
            // Show USB power indicator
            ret = led_matrix_show_pattern(PATTERN_DEMO_USB, LED_DEFAULT_BRIGHTNESS);
            ESP_LOGI(TAG, "Demo: Showing USB power indicator");
            break;

        case 1:
            // Show pin assignments
            ret = led_matrix_show_pattern(PATTERN_DEMO_PINS, LED_DEFAULT_BRIGHTNESS);
            ESP_LOGI(TAG, "Demo: Showing pin assignments");
            break;

        case 2:
            // Demo fluid OK state
            ret = led_matrix_show_pattern(PATTERN_DEMO_FLUID_OK, LED_DEFAULT_BRIGHTNESS);
            ESP_LOGI(TAG, "Demo: Fluid OK animation");
            break;

        case 3:
            // Demo fluid low state
            ret = led_matrix_show_pattern(PATTERN_DEMO_FLUID_LOW, LED_DEFAULT_BRIGHTNESS);
            ESP_LOGI(TAG, "Demo: Fluid low animation");
            break;

        case 4:
            // Demo fluid critical state
            ret = led_matrix_show_pattern(PATTERN_DEMO_FLUID_CRITICAL, LED_DEFAULT_BRIGHTNESS);
            ESP_LOGI(TAG, "Demo: Fluid critical animation");
            break;

        default:
            demo_step = 0;
            return;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Demo animation failed: %s", esp_err_to_name(ret));
    }

    demo_step++;
    if (demo_step > 4) {
        demo_step = 0;
    }
}

esp_err_t demo_mode_init(const demo_config_t *config) {
    if (g_demo.initialized) {
        ESP_LOGW(TAG, "Demo mode already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Demo mode config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    memcpy(&g_demo.config, config, sizeof(demo_config_t));

    // Initialize state
    g_demo.state = DEMO_MODE_DISABLED;
    g_demo.power_source = POWER_SOURCE_UNKNOWN;
    g_demo.last_check_time = 0;
    g_demo.demo_start_time = 0;
    g_demo.demo_active = false;
    g_demo.auto_requested = false;

    // Create demo timer
    g_demo.demo_timer = xTimerCreate(
        "demo_timer",
        pdMS_TO_TICKS(DEMO_ANIMATION_INTERVAL_MS),
        pdTRUE,  // Auto-reload
        NULL,
        demo_timer_callback
    );

    if (!g_demo.demo_timer) {
        ESP_LOGE(TAG, "Failed to create demo timer");
        return ESP_ERR_NO_MEM;
    }

    g_demo.initialized = true;
    ESP_LOGI(TAG, "Demo mode initialized");

    return ESP_OK;
}

bool demo_mode_is_active(void) {
    if (!g_demo.initialized) {
        return false;
    }

    const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - g_demo.last_check_time >= g_demo.config.check_interval_ms) {
        detect_power_source();
        g_demo.last_check_time = now;

        if (!g_demo.demo_active) {
            g_demo.state = g_demo.auto_requested ? DEMO_MODE_USB_POWER : DEMO_MODE_DISABLED;
        }
    }

    return g_demo.auto_requested;
}

power_source_t demo_mode_get_power_source(void) {
    return g_demo.power_source;
}

demo_mode_state_t demo_mode_get_state(void) {
    return g_demo.state;
}

const char *demo_mode_state_to_string(demo_mode_state_t state) {
    switch (state) {
        case DEMO_MODE_DISABLED:
            return "Disabled";
        case DEMO_MODE_USB_POWER:
            return "USB Host Detected";
        case DEMO_MODE_SHOW_PINS:
            return "Showing Pins";
        case DEMO_MODE_ANIMATE:
            return "Animating";
        default:
            return "Unknown";
    }
}

const char *demo_mode_power_source_to_string(power_source_t source) {
    switch (source) {
        case POWER_SOURCE_USB:
            return "USB";
        case POWER_SOURCE_5V_BUCK:
            return "5V Buck";
        default:
            return "Unknown";
    }
}

esp_err_t demo_mode_start(void) {
    if (!g_demo.initialized) {
        ESP_LOGE(TAG, "Demo mode not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_demo.demo_active) {
        ESP_LOGW(TAG, "Demo mode already active");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting demo mode");
    g_demo.demo_active = true;
    g_demo.demo_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_demo.state = DEMO_MODE_SHOW_PINS;

    // Start with pin assignment display
    esp_err_t ret = led_matrix_show_pattern(PATTERN_DEMO_PINS, LED_DEFAULT_BRIGHTNESS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to show pin assignments: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start demo timer for animations
    if (xTimerStart(g_demo.demo_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start demo timer");
        g_demo.demo_active = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t demo_mode_stop(void) {
    if (!g_demo.demo_active) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping demo mode");

    // Stop timer
    xTimerStop(g_demo.demo_timer, pdMS_TO_TICKS(100));

    // Clear display
    led_matrix_clear();

    g_demo.demo_active = false;
    g_demo.state = DEMO_MODE_DISABLED;

    return ESP_OK;
}

esp_err_t demo_mode_update(void) {
    if (!g_demo.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check if we should be in demo mode
    bool should_be_active = demo_mode_is_active();

    if (should_be_active && !g_demo.demo_active) {
        // Start demo mode
        return demo_mode_start();
    } else if (!should_be_active && g_demo.demo_active) {
        // Stop demo mode
        return demo_mode_stop();
    }

    return ESP_OK;
}

void demo_mode_print_status(void) {
    if (!g_demo.initialized) {
        printf("Demo mode: Not initialized\n");
        return;
    }

    printf("\n=== Demo Mode Status ===\n");
    printf("State: %s\n", demo_mode_state_to_string(g_demo.state));
    printf("Power Source: %s\n", demo_mode_power_source_to_string(g_demo.power_source));
    printf("Auto Detection: %s\n",
           g_demo.config.enable_detection
               ? (g_demo.auto_requested ? "USB host present" : "No USB host present")
               : "Disabled");
    printf("Demo Active: %s\n", g_demo.demo_active ? "Yes" : "No");

    if (g_demo.demo_active) {
        uint32_t runtime = (xTaskGetTickCount() * portTICK_PERIOD_MS) - g_demo.demo_start_time;
        printf("Demo Runtime: %lu ms\n", runtime);
    }
    printf("\n");
}
