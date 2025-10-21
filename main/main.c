/**
 * @file main.c
 * @brief MatrixFluid - Vehicle Fluid Level Indicator Main Application
 *
 * ESP32-S3 based vehicle fluid level indicator with 8x8 RGB LED matrix display.
 * Features timed status cycles, multi-level fluid sensing, Wi-Fi portal refresh, and safety-limited brightness.
 *
 * Hardware:
 * - Waveshare ESP32-S3-Matrix board
 * - 8x8 WS2812B LED matrix (GPIO14)
 * - Fluid sensors (GPIO4=full, GPIO2=above-half, GPIO5=below-half, GPIO3=reserve)
 * - Onboard QMI8658 accelerometer (unused; demo mode uses USB host detection)
 *
 * Safety:
 * - LED brightness capped at 5/255 (~2%) to prevent overheating
 * - Timed activation cycle with 7-second auto-shutoff
 * - Optional manual refresh via Wi-Fi portal
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
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// Component includes
#include "led_matrix.h"
#include "fluid_sensors.h"
#include "display_controller.h"
#include "wifi_config.h"
#include "demo_mode.h"
#include "alerts.h"

static const char *TAG = "matrixfluid";

// System configuration
#define DISPLAY_TIMEOUT_MS          7000    ///< Auto-off timeout (7 seconds)
#define SELF_TEST_DURATION_MS       2000    ///< Self-test pattern duration
#define STARTUP_DELAY_MS            1000    ///< Startup delay after boot

// System state
typedef enum {
    SYSTEM_STATE_INITIALIZING = 0,
    SYSTEM_STATE_SELF_TEST,
    SYSTEM_STATE_WIFI_SETUP,
    SYSTEM_STATE_RUNNING,
    SYSTEM_STATE_DEMO_MODE,
    SYSTEM_STATE_ERROR
} system_state_t;

// Global system state
static struct {
    system_state_t state;
    fluid_level_t current_fluid_level;
    fluid_level_t previous_fluid_level;
    uint32_t startup_time_ms;
    bool wifi_enabled;
    bool demo_mode_enabled;
    uint32_t last_demo_check;
    uint32_t total_web_triggers;
    bool auto_demo_requested;
} g_system = {0};

// Forward declarations
static void fluid_level_changed_callback(fluid_level_t new_level, fluid_level_t old_level, void *user_ctx);
static void display_event_callback(trigger_source_t source, fluid_level_t fluid_level, void *user_ctx);

/**
 * @brief Display event callback from display controller
 */
static void display_event_callback(trigger_source_t source, fluid_level_t fluid_level, void *user_ctx) {
    ESP_LOGI(TAG, "Display activated: source=%s, level=%s",
             display_controller_trigger_to_string(source),
             fluid_level_to_string(fluid_level));

    // Update statistics
    if (source == TRIGGER_SOURCE_MANUAL) {
        g_system.total_web_triggers++;
    }
}

/**
 * @brief Fluid level change callback
 */
static void fluid_level_changed_callback(fluid_level_t new_level, fluid_level_t old_level, void *user_ctx) {
    uint8_t new_percent = fluid_level_to_percent(new_level);
    uint8_t old_percent = fluid_level_to_percent(old_level);
    ESP_LOGI(TAG, "Fluid level changed: %s (%u%%) -> %s (%u%%)",
             fluid_level_to_string(old_level), (unsigned)old_percent,
             fluid_level_to_string(new_level), (unsigned)new_percent);

    g_system.previous_fluid_level = old_level;
    g_system.current_fluid_level = new_level;

    // Notify display controller
    display_controller_update_fluid_level(new_level, old_level);

    // Log critical fluid level
    if (new_level == FLUID_LEVEL_NEAR_EMPTY) {
        ESP_LOGW(TAG, "WARNING: Fluid level approaching reserve threshold!");
    } else if (new_level == FLUID_LEVEL_EMPTY) {
        ESP_LOGW(TAG, "CRITICAL: Fluid level below reserve threshold!");
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

    // Test fluid sensors
    fluid_level_t test_level = fluid_sensors_get_level();
    ESP_LOGI(TAG, "Fluid sensor test: %s", fluid_level_to_string(test_level));

    // Clear display and proceed to WiFi setup
    ret = led_matrix_clear();
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to clear display after self-test");

    g_system.state = SYSTEM_STATE_WIFI_SETUP;
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
    printf("Features: Timed updates, 8x8 LED display, dual fluid sensors, Wi-Fi portal\n");
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
    printf("  Fluid level: %s\n", fluid_level_to_string(g_system.current_fluid_level));
    printf("  Web triggers: %lu\n", g_system.total_web_triggers);
    printf("\n");

    // Print control options
    printf("Control Options:\n");
    printf("  WiFi Network: %s (Password: %s)\n", WIFI_AP_SSID, WIFI_AP_PASSWORD);
    printf("  Web Interface: http://192.168.4.1/\n");
    printf("  - Manual display trigger\n");
    printf("  - Configurable automatic timing\n");
    printf("  - Brightness control\n");
    printf("\n");

    // Print pin assignments
    printf("Pin Assignments:\n");
    printf("  GPIO%d: Fluid sensor (full)\n", FLUID_FULL_SENSOR_GPIO);
    printf("  GPIO%d: Fluid sensor (above half)\n", FLUID_HALF_SENSOR_GPIO);
    printf("  GPIO%d: Fluid sensor (below half)\n", FLUID_LOW_SENSOR_GPIO);
    printf("  GPIO%d: Fluid sensor (reserve/near-empty)\n", FLUID_EMPTY_SENSOR_GPIO);
    printf("  GPIO14: LED matrix data\n");
    printf("  5V/GND: Power (5V buck converter recommended)\n");
    printf("\n");
}

static void publish_portal_status_snapshot(void) {
    if (!g_system.wifi_enabled) {
        return;
    }

    display_stats_t display_stats;
    display_controller_get_stats(&display_stats);

    fluid_reading_t raw_reading = {0};
    bool raw_valid = (fluid_sensors_read_raw(&raw_reading) == ESP_OK);

    wifi_portal_status_t status = {0};
    status.fluid_level = g_system.current_fluid_level;
    status.displayed_fluid_level = display_controller_get_current_level();
    status.fluid_percentage = fluid_level_to_percent(status.fluid_level);
    if (raw_valid) {
        status.full_sensor_submerged = raw_reading.full_sensor_submerged;
        status.half_sensor_submerged = raw_reading.half_sensor_submerged;
        status.low_sensor_submerged = raw_reading.low_sensor_submerged;
        status.empty_sensor_submerged = raw_reading.empty_sensor_submerged;
        status.full_sensor_signal_high = raw_reading.full_sensor_signal_high;
        status.half_sensor_signal_high = raw_reading.half_sensor_signal_high;
        status.low_sensor_signal_high = raw_reading.low_sensor_signal_high;
        status.empty_sensor_signal_high = raw_reading.empty_sensor_signal_high;
    }
    status.display_active = display_controller_is_active();
    status.uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    status.total_display_count = display_stats.total_displays;
    status.manual_trigger_count = display_stats.manual_triggers;
    status.periodic_trigger_count = display_stats.periodic_triggers;
    status.demo_mode_active = g_system.demo_mode_enabled;
    status.demo_mode_requested = wifi_config_demo_mode_requested();
    status.auto_demo_requested = g_system.auto_demo_requested;
    status.power_source = demo_mode_get_power_source();

    alert_snapshot_t alert_snapshot = {
        .fluid_level = status.fluid_level,
        .fluid_percentage = status.fluid_percentage,
        .full_submerged = status.full_sensor_submerged,
        .half_submerged = status.half_sensor_submerged,
        .low_submerged = status.low_sensor_submerged,
        .empty_submerged = status.empty_sensor_submerged,
        .full_signal_high = status.full_sensor_signal_high,
        .half_signal_high = status.half_sensor_signal_high,
        .low_signal_high = status.low_sensor_signal_high,
        .empty_signal_high = status.empty_sensor_signal_high,
        .uptime_seconds = status.uptime_seconds,
    };
    const char *power_label = demo_mode_power_source_to_string(status.power_source);
    if (power_label) {
        strncpy(alert_snapshot.power_source, power_label, sizeof(alert_snapshot.power_source) - 1);
        alert_snapshot.power_source[sizeof(alert_snapshot.power_source) - 1] = '\0';
    }
    alerts_update_snapshot(&alert_snapshot);

    alerts_get_portal_status(&status.alerts);

    display_config_t portal_config;
    if (wifi_config_get_display_config(&portal_config) == ESP_OK) {
        status.display_config = portal_config;

        if (portal_config.periodic_display_enabled && portal_config.display_interval_seconds > 0) {
            const uint64_t now_ms = esp_timer_get_time() / 1000ULL;
            const uint64_t interval_ms = (uint64_t)portal_config.display_interval_seconds * 1000ULL;
            uint32_t next_wake_seconds = portal_config.display_interval_seconds;

            if (display_stats.last_display_time > 0 && interval_ms > 0) {
                uint64_t last_ms = display_stats.last_display_time;
                if (now_ms >= last_ms) {
                    uint64_t elapsed_ms = now_ms - last_ms;
                    if (elapsed_ms < interval_ms) {
                        next_wake_seconds = (uint32_t)((interval_ms - elapsed_ms + 999ULL) / 1000ULL);
                    } else {
                        next_wake_seconds = 0;
                    }
                }
            }

            status.next_wake_seconds = next_wake_seconds;
        }
    }

    wifi_config_update_status(&status);
}

/**
 * @brief System monitoring task
 */
static void system_monitor_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(10000);  // 10 second intervals

    while (1) {
        // Print periodic status
        const char* state_names[] = {
            "INIT", "SELF_TEST", "WIFI_SETUP", "RUNNING", "DEMO", "ERROR"
        };
        const char* state_name = (g_system.state < 6) ? state_names[g_system.state] : "UNKNOWN";

        display_stats_t display_stats;
        display_controller_get_stats(&display_stats);

        ESP_LOGI(TAG, "Status: State=%s, Fluid=%s (%u%%), Demo=%s, WiFi=%s, Displays=%lu, WebTriggers=%lu, Errors=%lu",
                 state_name,
                 fluid_level_to_string(g_system.current_fluid_level),
                 (unsigned)fluid_level_to_percent(g_system.current_fluid_level),
                 g_system.demo_mode_enabled ? "YES" : "NO",
                 g_system.wifi_enabled ? "YES" : "NO",
                 display_stats.total_displays,
                 g_system.total_web_triggers,
                 fluid_sensors_get_error_count());

        uint8_t ap_clients = 0;
        char ap_ip[16] = "0.0.0.0";
        wifi_config_get_status(&ap_clients, ap_ip);

        wifi_sta_status_t sta_status = {0};
        if (wifi_config_get_sta_status(&sta_status) == ESP_OK) {
            const char *sta_state = "Disabled";
            if (sta_status.connected) {
                sta_state = "Connected";
            } else if (sta_status.connecting) {
                sta_state = "Connecting";
            } else if (sta_status.enabled) {
                sta_state = sta_status.has_credentials ? "Enabled" : "Enabled (no creds)";
            }

            ESP_LOGI(TAG,
                     "WiFi AP IP=%s Clients=%u | STA %s SSID=%s IP=%s%s%s",
                     ap_ip,
                     ap_clients,
                     sta_state,
                     sta_status.ssid[0] ? sta_status.ssid : "--",
                     sta_status.ip[0] ? sta_status.ip : "--",
                     sta_status.last_error[0] ? " Error=" : "",
                     sta_status.last_error[0] ? sta_status.last_error : "");
        }

        publish_portal_status_snapshot();

        // Print demo mode status if active
        if (g_system.demo_mode_enabled || g_system.auto_demo_requested || wifi_config_demo_mode_requested()) {
            ESP_LOGI(TAG, "Demo Mode: Active=%s, Auto=%s, Portal=%s, Power=%s",
                     g_system.demo_mode_enabled ? "YES" : "NO",
                     g_system.auto_demo_requested ? "YES" : "NO",
                     wifi_config_demo_mode_requested() ? "YES" : "NO",
                     demo_mode_power_source_to_string(demo_mode_get_power_source()));
        }

        // Check for critical conditions
        if (g_system.current_fluid_level == FLUID_LEVEL_SENSOR_ERROR) {
            ESP_LOGW(TAG, "Sensor error condition persisting");
        } else if (g_system.current_fluid_level == FLUID_LEVEL_EMPTY) {
            ESP_LOGW(TAG, "Fluid level remains below reserve threshold");
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
    g_system.wifi_enabled = false;
    g_system.demo_mode_enabled = false;
    g_system.total_web_triggers = 0;
    g_system.auto_demo_requested = false;

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

    // Default Wi-Fi / scheduler configuration
    wifi_config_init_t wifi_init_config = {
        .enable_ap = true,
        .enable_web_server = true,
        .default_display = {
            .periodic_display_enabled = true,
            .display_interval_seconds = 900,    // 15 minutes
            .display_duration_seconds = 7,      // 7 seconds
            .display_brightness = 3,
            .manual_trigger_enabled = true,
            .auto_brightness = false
        },
        .log_stream = {
            .enable_udp_sink = false,
            .udp_host = "",
            .udp_port = 514
        }
    };

    display_controller_config_t display_config = {
        .mode = wifi_init_config.default_display.periodic_display_enabled
            ? DISPLAY_MODE_PERIODIC
            : DISPLAY_MODE_MANUAL_ONLY,
        .periodic_interval_ms = wifi_init_config.default_display.display_interval_seconds * 1000,
        .display_duration_ms = wifi_init_config.default_display.display_duration_seconds * 1000,
        .brightness = wifi_init_config.default_display.display_brightness,
        .fade_in_out = false,
        .show_startup_sequence = true
    };

    ret = display_controller_init(&display_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display controller init failed: %s", esp_err_to_name(ret));
        // Continue - critical error
    } else {
        ESP_LOGI(TAG, "Display controller initialized");
    }

    // Initialize WiFi configuration
    ret = wifi_config_init(&wifi_init_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi config init failed: %s", esp_err_to_name(ret));
        // Continue - not critical for basic operation
    } else {
        ESP_LOGI(TAG, "WiFi configuration initialized");
        g_system.wifi_enabled = true;
        wifi_config_set_display_config(&wifi_init_config.default_display);
    }

    // Initialize demo mode detection
    demo_config_t demo_config = {
        .enable_detection = true,
        .check_interval_ms = 5000,  // Check every 5 seconds
        .verbose_logging = true
    };
    ret = demo_mode_init(&demo_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Demo mode init failed: %s", esp_err_to_name(ret));
        // Continue - not critical for operation
    } else {
        ESP_LOGI(TAG, "Demo mode detection initialized");
    }

    alerts_init();

    // Register callbacks
    if (fluid_sensors_register_callback(fluid_level_changed_callback, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Fluid sensor callback registered");
    }

    if (display_controller_register_callback(display_event_callback, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Display controller callback registered");
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

    // Start WiFi Access Point if enabled
    if (g_system.wifi_enabled && g_system.state == SYSTEM_STATE_WIFI_SETUP) {
        ESP_LOGI(TAG, "Starting WiFi Access Point...");
        ret = wifi_config_start_ap();
        if (ret == ESP_OK) {
            ret = wifi_config_start_server();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "WiFi Access Point and web server started");
                wifi_config_print_info();
            } else {
                ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "Failed to start WiFi AP: %s", esp_err_to_name(ret));
        }
    }

    // Start display controller
    if (g_system.state != SYSTEM_STATE_ERROR) {
        ret = display_controller_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Display controller started");
            g_system.state = SYSTEM_STATE_RUNNING;
        } else {
            ESP_LOGE(TAG, "Failed to start display controller: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "System ready! Web interface: http://192.168.4.1/");
    ESP_LOGI(TAG, "Current fluid level: %s", fluid_level_to_string(g_system.current_fluid_level));

    publish_portal_status_snapshot();

    // Create system monitoring task
    xTaskCreate(
        system_monitor_task,
        "system_monitor",
        4096,
        NULL,
        2,  // Low priority
        NULL
    );

    // Main loop - system is now timer and web driven
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Check for manual web triggers
        if (wifi_config_should_trigger_display()) {
            ESP_LOGI(TAG, "Manual web trigger detected");
            display_controller_trigger_manual();
            wifi_config_clear_trigger_flag();
        }

        // Check for demo mode activation
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - g_system.last_demo_check >= 2000) {  // Check every 2 seconds
            g_system.last_demo_check = now;

            bool auto_demo_active = demo_mode_is_active();
            g_system.auto_demo_requested = auto_demo_active;
            bool portal_demo_requested = wifi_config_demo_mode_requested();
            bool should_be_demo = auto_demo_active || portal_demo_requested;
            bool currently_demo = (g_system.state == SYSTEM_STATE_DEMO_MODE);

            // Transition to demo mode if requested
            if (should_be_demo && !currently_demo && g_system.state == SYSTEM_STATE_RUNNING && !display_controller_is_active()) {
                ESP_LOGI(TAG, "Demo mode detected via USB host connection");
                g_system.state = SYSTEM_STATE_DEMO_MODE;
                g_system.demo_mode_enabled = true;

                // Stop regular display controller and start demo
                display_controller_stop();
                demo_mode_print_status();

                esp_err_t demo_ret = demo_mode_start();
                if (demo_ret == ESP_OK) {
                    ESP_LOGI(TAG, "Demo mode started successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to start demo mode: %s", esp_err_to_name(demo_ret));
                }
            }
            // Transition out of demo mode if no longer applicable
            else if (!should_be_demo && currently_demo) {
                ESP_LOGI(TAG, "Exiting demo mode - returning to scheduled operation");
                g_system.state = SYSTEM_STATE_RUNNING;
                g_system.demo_mode_enabled = false;

                esp_err_t stop_ret = demo_mode_stop();
                if (stop_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Demo mode stop reported: %s", esp_err_to_name(stop_ret));
                }

                esp_err_t restart_ret = display_controller_start();
                if (restart_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to restart display controller: %s", esp_err_to_name(restart_ret));
                }
            }
        }

        // Handle error recovery
        if (g_system.state == SYSTEM_STATE_ERROR) {
            ESP_LOGE(TAG, "System in error state - attempting recovery...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }
    }
}
