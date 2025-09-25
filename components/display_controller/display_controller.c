/**
 * @file display_controller.c
 * @brief Timer-based Display Controller Implementation
 */

#include <string.h>
#include <stdio.h>
#include "display_controller.h"
#include "led_matrix.h"
#include "fluid_sensors.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_ctrl";

#define ICON_HOLD_MS      1500
#define SCROLL_STEP_MS     120
#define DISPLAY_ANIMATION_TASK_STACK_WORDS 1024

// Controller state
static struct {
    bool initialized;
    bool started;
    display_controller_config_t config;
    display_stats_t stats;
    display_event_callback_t callback;
    void *callback_ctx;

    // Timers
    esp_timer_handle_t periodic_timer;
    esp_timer_handle_t display_off_timer;

    // Current state
    bool display_active;
    fluid_level_t current_fluid_level;
    fluid_level_t previous_fluid_level;
    uint32_t display_start_time;
    TaskHandle_t animation_task;
    led_color_t caption_color;
    uint8_t caption_brightness;
    char caption_text[32];
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    StaticTask_t animation_tcb;
    StackType_t animation_stack[DISPLAY_ANIMATION_TASK_STACK_WORDS];
#endif
} g_display_ctrl = {0};

// Forward declarations
static void periodic_timer_callback(void* arg);
static void display_off_timer_callback(void* arg);
static esp_err_t activate_display(trigger_source_t source);
static esp_err_t deactivate_display(void);
static led_pattern_t fluid_level_to_pattern(fluid_level_t level);
static void display_animation_task(void *param);
static void build_caption(fluid_level_t level, char *buffer, size_t len,
                          led_color_t *color_out);
static void draw_caption_snapshot(void);

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

static void build_caption(fluid_level_t level, char *buffer, size_t len,
                          led_color_t *color_out) {
    const char *text = "STATUS";
    led_color_t color = LED_COLOR_GREEN;

    switch (level) {
        case FLUID_LEVEL_ABOVE_HALF:
            text = "LEVEL OK";
            color = LED_COLOR_GREEN;
            break;
        case FLUID_LEVEL_BELOW_HALF:
            text = "BELOW HALF";
            color = LED_COLOR_YELLOW;
            break;
        case FLUID_LEVEL_NEAR_EMPTY:
            text = "TANK EMPTY";
            color = LED_COLOR_RED;
            break;
        case FLUID_LEVEL_SENSOR_ERROR:
        default:
            text = "SENSOR ERR";
            color = LED_COLOR_RED;
            break;
    }

    if (buffer && len > 0) {
        snprintf(buffer, len, "%s", text);
        for (char *p = buffer; *p; ++p) {
            if (*p >= 'a' && *p <= 'z') {
                *p = (char)(*p - 32);
            }
        }
    }

    if (color_out) {
        *color_out = color;
    }
}

/**
 * @brief Periodic timer callback
 */
static void periodic_timer_callback(void* arg) {
    if (g_display_ctrl.config.mode == DISPLAY_MODE_PERIODIC) {
        ESP_LOGI(TAG, "Periodic display trigger");
        activate_display(TRIGGER_SOURCE_PERIODIC);
    }
}

/**
 * @brief Display off timer callback
 */
static void display_off_timer_callback(void* arg) {
    ESP_LOGI(TAG, "Display timeout - turning off");
    deactivate_display();
}

/**
 * @brief Activate display with fade-in effect
 */
static esp_err_t activate_display(trigger_source_t source) {
    if (!g_display_ctrl.initialized || !g_display_ctrl.started) {
        return ESP_ERR_INVALID_STATE;
    }

    g_display_ctrl.display_active = true;
    g_display_ctrl.display_start_time = esp_timer_get_time() / 1000; // Convert to ms

    // Update statistics
    g_display_ctrl.stats.total_displays++;
    g_display_ctrl.stats.last_display_time = g_display_ctrl.display_start_time;
    g_display_ctrl.stats.last_trigger = source;

    switch (source) {
        case TRIGGER_SOURCE_MANUAL:
            g_display_ctrl.stats.manual_triggers++;
            break;
        case TRIGGER_SOURCE_PERIODIC:
            g_display_ctrl.stats.periodic_triggers++;
            break;
        case TRIGGER_SOURCE_FLUID_CHANGE:
            g_display_ctrl.stats.fluid_change_triggers++;
            break;
        default:
            break;
    }

    // Get current fluid level
    g_display_ctrl.current_fluid_level = fluid_sensors_get_level();
    led_pattern_t pattern = fluid_level_to_pattern(g_display_ctrl.current_fluid_level);

    ESP_LOGI(TAG, "Activating display: source=%s, fluid=%s, pattern=%d",
             display_controller_trigger_to_string(source),
             fluid_level_to_string(g_display_ctrl.current_fluid_level),
             pattern);
    ESP_LOGI(TAG, "Caption seed before build: brightness=%d", g_display_ctrl.config.brightness);

    // Show pattern with configured brightness
    esp_err_t ret = led_matrix_show_pattern(pattern, g_display_ctrl.config.brightness);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to show pattern: %s", esp_err_to_name(ret));
        g_display_ctrl.display_active = false;
        return ret;
    }

    build_caption(g_display_ctrl.current_fluid_level,
                  g_display_ctrl.caption_text,
                  sizeof(g_display_ctrl.caption_text),
                  &g_display_ctrl.caption_color);
    g_display_ctrl.caption_brightness = g_display_ctrl.config.brightness;
    ESP_LOGI(TAG, "Caption prepared: '%s', color=(%u,%u,%u)",
             g_display_ctrl.caption_text,
             g_display_ctrl.caption_color.r,
             g_display_ctrl.caption_color.g,
             g_display_ctrl.caption_color.b);

    if (g_display_ctrl.animation_task) {
        vTaskDelete(g_display_ctrl.animation_task);
        g_display_ctrl.animation_task = NULL;
    }

    TaskHandle_t animation_handle = NULL;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
    animation_handle = xTaskCreateStatic(display_animation_task,
                                         "disp_scroll",
                                         DISPLAY_ANIMATION_TASK_STACK_WORDS,
                                         NULL,
                                         4,
                                         g_display_ctrl.animation_stack,
                                         &g_display_ctrl.animation_tcb);
#endif

    if (animation_handle == NULL) {
        BaseType_t created = xTaskCreate(display_animation_task,
                                         "disp_scroll",
                                         2048,
                                         NULL,
                                         4,
                                         &animation_handle);
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create display animation task");
            animation_handle = NULL;
        }
    }

    if (animation_handle) {
        g_display_ctrl.animation_task = animation_handle;
    } else {
        draw_caption_snapshot();
    }

    // Start display off timer
    if (g_display_ctrl.config.display_duration_ms > 0) {
        esp_timer_start_once(g_display_ctrl.display_off_timer,
                            g_display_ctrl.config.display_duration_ms * 1000); // Convert to microseconds
    }

    // Call callback if registered
    if (g_display_ctrl.callback) {
        g_display_ctrl.callback(source, g_display_ctrl.current_fluid_level, g_display_ctrl.callback_ctx);
    }

    return ESP_OK;
}

/**
 * @brief Deactivate display with fade-out effect
 */
static esp_err_t deactivate_display(void) {
    if (!g_display_ctrl.display_active) {
        return ESP_OK;
    }

    g_display_ctrl.display_active = false;

    // Stop display off timer
    esp_timer_stop(g_display_ctrl.display_off_timer);

    // Clear display
    esp_err_t ret = led_matrix_clear();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear display: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Display deactivated");
    return ret;
}

static void display_animation_task(void *param) {
    (void)param;

    const TickType_t hold_ticks = pdMS_TO_TICKS(ICON_HOLD_MS);
    if (hold_ticks > 0) {
        vTaskDelay(hold_ticks);
    }

    if (!g_display_ctrl.display_active) {
        g_display_ctrl.animation_task = NULL;
        vTaskDelete(NULL);
    }

    int text_width = led_matrix_measure_text(g_display_ctrl.caption_text);
    if (text_width <= 0) {
        g_display_ctrl.animation_task = NULL;
        vTaskDelete(NULL);
    }

    bool scrolling = text_width > LED_MATRIX_WIDTH;
    int16_t offset = scrolling
        ? LED_MATRIX_WIDTH
        : (int16_t)((LED_MATRIX_WIDTH - text_width) / 2);

    const TickType_t step_ticks = pdMS_TO_TICKS(SCROLL_STEP_MS);

    ESP_LOGI(TAG,
             "Caption scroll start: text='%s', width=%d, scrolling=%s, brightness=%d",
             g_display_ctrl.caption_text,
             text_width,
             scrolling ? "yes" : "no",
             g_display_ctrl.caption_brightness);

    while (g_display_ctrl.display_active) {
        esp_err_t err = led_matrix_draw_text_frame(g_display_ctrl.caption_text,
                                                   offset,
                                                   g_display_ctrl.caption_color,
                                                   g_display_ctrl.caption_brightness);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to draw text frame: %s", esp_err_to_name(err));
            ESP_LOGE(TAG, "Caption text '%s', offset=%d", g_display_ctrl.caption_text, offset);
            break;
        }

        if (scrolling) {
            offset--;
            if (offset < -text_width) {
                offset = LED_MATRIX_WIDTH;
            }
        }

        if (step_ticks > 0) {
            vTaskDelay(step_ticks);
        } else {
            taskYIELD();
        }
    }

    g_display_ctrl.animation_task = NULL;
    vTaskDelete(NULL);
}

static void draw_caption_snapshot(void) {
    int text_width = led_matrix_measure_text(g_display_ctrl.caption_text);
    if (text_width <= 0) {
        return;
    }

    int16_t offset = (text_width > LED_MATRIX_WIDTH)
        ? 0
        : (int16_t)((LED_MATRIX_WIDTH - text_width) / 2);

    esp_err_t err = led_matrix_draw_text_frame(g_display_ctrl.caption_text,
                                               offset,
                                               g_display_ctrl.caption_color,
                                               g_display_ctrl.caption_brightness);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallback caption draw failed: %s", esp_err_to_name(err));
    }
}

esp_err_t display_controller_init(const display_controller_config_t *config) {
    if (g_display_ctrl.initialized) {
        ESP_LOGW(TAG, "Display controller already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    g_display_ctrl.config = *config;

    // Validate configuration
    if (g_display_ctrl.config.brightness > 5) {
        ESP_LOGW(TAG, "Brightness clamped from %d to 5", g_display_ctrl.config.brightness);
        g_display_ctrl.config.brightness = 5;
    }

    if (g_display_ctrl.config.periodic_interval_ms < 1000) {
        ESP_LOGW(TAG, "Periodic interval too short, setting to 5000ms");
        g_display_ctrl.config.periodic_interval_ms = 5000;
    }

    // Initialize timers
    esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_timer_callback,
        .arg = NULL,
        .name = "display_periodic"
    };

    esp_timer_create_args_t display_off_timer_args = {
        .callback = &display_off_timer_callback,
        .arg = NULL,
        .name = "display_off"
    };

    esp_err_t ret = esp_timer_create(&periodic_timer_args, &g_display_ctrl.periodic_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create periodic timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_create(&display_off_timer_args, &g_display_ctrl.display_off_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create display off timer: %s", esp_err_to_name(ret));
        esp_timer_delete(g_display_ctrl.periodic_timer);
        return ret;
    }

    // Initialize state
    g_display_ctrl.initialized = true;
    g_display_ctrl.started = false;
    g_display_ctrl.display_active = false;
    g_display_ctrl.current_fluid_level = FLUID_LEVEL_SENSOR_ERROR;
    g_display_ctrl.previous_fluid_level = FLUID_LEVEL_SENSOR_ERROR;

    memset(&g_display_ctrl.stats, 0, sizeof(g_display_ctrl.stats));

    ESP_LOGI(TAG, "Display controller initialized: mode=%s, interval=%lums, duration=%lums, brightness=%d",
             display_controller_mode_to_string(g_display_ctrl.config.mode),
             g_display_ctrl.config.periodic_interval_ms,
             g_display_ctrl.config.display_duration_ms,
             g_display_ctrl.config.brightness);

    return ESP_OK;
}

esp_err_t display_controller_start(void) {
    if (!g_display_ctrl.initialized) {
        ESP_LOGE(TAG, "Display controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_display_ctrl.started) {
        ESP_LOGW(TAG, "Display controller already started");
        return ESP_OK;
    }

    g_display_ctrl.started = true;

    // Start periodic timer if configured
    if (g_display_ctrl.config.mode == DISPLAY_MODE_PERIODIC) {
        esp_err_t ret = esp_timer_start_periodic(g_display_ctrl.periodic_timer,
                                                 g_display_ctrl.config.periodic_interval_ms * 1000); // Convert to microseconds
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start periodic timer: %s", esp_err_to_name(ret));
            g_display_ctrl.started = false;
            return ret;
        }
        ESP_LOGI(TAG, "Periodic display started: interval=%lums", g_display_ctrl.config.periodic_interval_ms);
    }

    // Show startup sequence if configured
    if (g_display_ctrl.config.show_startup_sequence) {
        display_controller_show_startup_sequence();
    }

    // Always-on mode
    if (g_display_ctrl.config.mode == DISPLAY_MODE_ALWAYS_ON) {
        activate_display(TRIGGER_SOURCE_STARTUP);
    }

    ESP_LOGI(TAG, "Display controller started");
    return ESP_OK;
}

esp_err_t display_controller_stop(void) {
    if (!g_display_ctrl.started) {
        return ESP_OK;
    }

    g_display_ctrl.started = false;

    // Stop timers
    esp_timer_stop(g_display_ctrl.periodic_timer);
    esp_timer_stop(g_display_ctrl.display_off_timer);

    // Clear display
    deactivate_display();

    while (g_display_ctrl.animation_task) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Display controller stopped");
    return ESP_OK;
}

esp_err_t display_controller_trigger_manual(void) {
    if (!g_display_ctrl.initialized || !g_display_ctrl.started) {
        ESP_LOGE(TAG, "Display controller not ready");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_display_ctrl.config.mode == DISPLAY_MODE_OFF) {
        ESP_LOGW(TAG, "Display controller is in OFF mode");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Manual display trigger");
    return activate_display(TRIGGER_SOURCE_MANUAL);
}

esp_err_t display_controller_set_config(const display_controller_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    bool restart_needed = false;

    // Check if periodic timer settings changed
    if (g_display_ctrl.config.mode != config->mode ||
        g_display_ctrl.config.periodic_interval_ms != config->periodic_interval_ms) {
        restart_needed = true;
    }

    // Update configuration
    g_display_ctrl.config = *config;

    // Validate brightness
    if (g_display_ctrl.config.brightness > 5) {
        g_display_ctrl.config.brightness = 5;
    }

    // Restart if needed
    if (restart_needed && g_display_ctrl.started) {
        ESP_LOGI(TAG, "Restarting display controller due to configuration change");
        display_controller_stop();
        display_controller_start();
    }

    ESP_LOGI(TAG, "Configuration updated");
    return ESP_OK;
}

esp_err_t display_controller_get_config(display_controller_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    *config = g_display_ctrl.config;
    return ESP_OK;
}

esp_err_t display_controller_update_fluid_level(fluid_level_t new_level, fluid_level_t old_level) {
    g_display_ctrl.previous_fluid_level = old_level;
    g_display_ctrl.current_fluid_level = new_level;

    // Trigger display if mode is ON_CHANGE and level changed
    if (g_display_ctrl.config.mode == DISPLAY_MODE_ON_CHANGE &&
        new_level != old_level && g_display_ctrl.started) {
        ESP_LOGI(TAG, "Fluid level changed: %s -> %s",
                 fluid_level_to_string(old_level), fluid_level_to_string(new_level));
        return activate_display(TRIGGER_SOURCE_FLUID_CHANGE);
    }

    // Update always-on display
    if (g_display_ctrl.config.mode == DISPLAY_MODE_ALWAYS_ON && g_display_ctrl.display_active) {
        led_pattern_t pattern = fluid_level_to_pattern(new_level);
        return led_matrix_show_pattern(pattern, g_display_ctrl.config.brightness);
    }

    return ESP_OK;
}

esp_err_t display_controller_register_callback(display_event_callback_t callback, void *user_ctx) {
    g_display_ctrl.callback = callback;
    g_display_ctrl.callback_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t display_controller_get_stats(display_stats_t *stats) {
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    *stats = g_display_ctrl.stats;
    return ESP_OK;
}

bool display_controller_is_active(void) {
    return g_display_ctrl.display_active;
}

fluid_level_t display_controller_get_current_level(void) {
    return g_display_ctrl.current_fluid_level;
}

esp_err_t display_controller_force_off(void) {
    return deactivate_display();
}

esp_err_t display_controller_set_brightness(uint8_t brightness) {
    if (brightness > 5) {
        brightness = 5;
    }

    g_display_ctrl.config.brightness = brightness;

    // Update display if active
    if (g_display_ctrl.display_active) {
        led_pattern_t pattern = fluid_level_to_pattern(g_display_ctrl.current_fluid_level);
        return led_matrix_show_pattern(pattern, brightness);
    }

    return ESP_OK;
}

esp_err_t display_controller_show_startup_sequence(void) {
    ESP_LOGI(TAG, "Showing startup sequence");

    // Show self-test pattern briefly
    led_matrix_show_pattern(PATTERN_SELF_TEST, g_display_ctrl.config.brightness);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Show current fluid level
    g_display_ctrl.current_fluid_level = fluid_sensors_get_level();
    led_pattern_t pattern = fluid_level_to_pattern(g_display_ctrl.current_fluid_level);
    led_matrix_show_pattern(pattern, g_display_ctrl.config.brightness);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Clear display
    led_matrix_clear();

    return ESP_OK;
}

const char* display_controller_mode_to_string(display_mode_t mode) {
    switch (mode) {
        case DISPLAY_MODE_OFF:         return "OFF";
        case DISPLAY_MODE_MANUAL_ONLY: return "MANUAL_ONLY";
        case DISPLAY_MODE_PERIODIC:    return "PERIODIC";
        case DISPLAY_MODE_ON_CHANGE:   return "ON_CHANGE";
        case DISPLAY_MODE_ALWAYS_ON:   return "ALWAYS_ON";
        default:                       return "UNKNOWN";
    }
}

const char* display_controller_trigger_to_string(trigger_source_t source) {
    switch (source) {
        case TRIGGER_SOURCE_MANUAL:       return "MANUAL";
        case TRIGGER_SOURCE_PERIODIC:     return "PERIODIC";
        case TRIGGER_SOURCE_FLUID_CHANGE: return "FLUID_CHANGE";
        case TRIGGER_SOURCE_STARTUP:      return "STARTUP";
        case TRIGGER_SOURCE_DEMO:         return "DEMO";
        default:                          return "UNKNOWN";
    }
}

esp_err_t display_controller_deinit(void) {
    if (!g_display_ctrl.initialized) {
        return ESP_OK;
    }

    // Stop if running
    display_controller_stop();

    // Delete timers
    if (g_display_ctrl.periodic_timer) {
        esp_timer_delete(g_display_ctrl.periodic_timer);
        g_display_ctrl.periodic_timer = NULL;
    }

    if (g_display_ctrl.display_off_timer) {
        esp_timer_delete(g_display_ctrl.display_off_timer);
        g_display_ctrl.display_off_timer = NULL;
    }

    g_display_ctrl.initialized = false;
    ESP_LOGI(TAG, "Display controller deinitialized");

    return ESP_OK;
}
