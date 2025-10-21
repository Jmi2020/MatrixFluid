/**
 * @file demo_mode.c
 * @brief Demo Mode Detection and Management Implementation
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "demo_mode.h"
#include "fluid_sensors.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    bool demo_active;
    bool auto_requested;
    TaskHandle_t demo_task;
} g_demo = {0};

// Forward declarations
static esp_err_t detect_power_source(void);
static void demo_task(void *arg);
static void show_demo_state(fluid_level_t level, const char *caption,
                            led_color_t color);

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
    g_demo.demo_task = NULL;

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

    if (g_demo.demo_task) {
        vTaskDelete(g_demo.demo_task);
        g_demo.demo_task = NULL;
    }

    BaseType_t created = xTaskCreate(demo_task,
                                     "demo_loop",
                                     3072,
                                     NULL,
                                     4,
                                     &g_demo.demo_task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create demo loop task");
        g_demo.demo_task = NULL;
        g_demo.demo_active = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t demo_mode_stop(void) {
    if (!g_demo.demo_active) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping demo mode");
    g_demo.demo_active = false;
    g_demo.state = DEMO_MODE_DISABLED;

    if (g_demo.demo_task) {
        vTaskDelete(g_demo.demo_task);
        g_demo.demo_task = NULL;
    }

    led_matrix_clear();

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

static void show_demo_state(fluid_level_t level, const char *caption,
                            led_color_t color) {
    if (!g_demo.demo_active) {
        return;
    }

    led_pattern_t pattern = PATTERN_RED_STOP;
    switch (level) {
        case FLUID_LEVEL_FULL:
        case FLUID_LEVEL_ABOVE_HALF:
            pattern = PATTERN_GREEN_CHECK;
            break;
        case FLUID_LEVEL_BELOW_HALF:
            pattern = PATTERN_YELLOW_WARN;
            break;
        case FLUID_LEVEL_NEAR_EMPTY:
        case FLUID_LEVEL_EMPTY:
        default:
            pattern = PATTERN_RED_STOP;
            break;
    }

    led_matrix_show_pattern(pattern, LED_DEFAULT_BRIGHTNESS);
    vTaskDelay(pdMS_TO_TICKS(1500));

    int width = led_matrix_measure_text(caption);
    bool scrolling = width > LED_MATRIX_WIDTH;
    int16_t offset = scrolling ? LED_MATRIX_WIDTH : (LED_MATRIX_WIDTH - width) / 2;
    const TickType_t step = pdMS_TO_TICKS(120);

    if (!scrolling) {
        led_matrix_draw_text_frame(caption, offset, color, LED_DEFAULT_BRIGHTNESS);
        vTaskDelay(pdMS_TO_TICKS(800));
        return;
    }

    while (g_demo.demo_active) {
        led_matrix_draw_text_frame(caption, offset, color, LED_DEFAULT_BRIGHTNESS);
        vTaskDelay(step);
        offset--;
        if (offset < -width) {
            break;
        }
    }
}

static void demo_task(void *arg) {
    (void)arg;

    const TickType_t pause_ticks = pdMS_TO_TICKS(60000);

    while (g_demo.demo_active) {
        g_demo.state = DEMO_MODE_ANIMATE;
        for (int cycle = 0; cycle < 2 && g_demo.demo_active; ++cycle) {
            show_demo_state(FLUID_LEVEL_FULL, "TANK FULL", LED_COLOR_GREEN);
            show_demo_state(FLUID_LEVEL_ABOVE_HALF, "LEVEL OK", LED_COLOR_GREEN);
            show_demo_state(FLUID_LEVEL_BELOW_HALF, "LOW LEVEL", LED_COLOR_YELLOW);
            show_demo_state(FLUID_LEVEL_NEAR_EMPTY, "RESERVE LOW", LED_COLOR_RED);
            show_demo_state(FLUID_LEVEL_EMPTY, "TANK EMPTY", LED_COLOR_RED);
        }

        if (!g_demo.demo_active) {
            break;
        }

        g_demo.state = DEMO_MODE_USB_POWER;
        led_matrix_clear();
        vTaskDelay(pause_ticks);
    }

    led_matrix_clear();
    g_demo.demo_task = NULL;
    g_demo.demo_active = false;
    g_demo.state = DEMO_MODE_DISABLED;
    vTaskDelete(NULL);
}
