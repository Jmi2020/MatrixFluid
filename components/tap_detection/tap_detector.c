/**
 * @file tap_detector.c
 * @brief Triple-Tap Detection Implementation
 */

#include "tap_detector.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "tap_detector";

// Module state
static struct {
    tap_config_t config;
    tap_state_t current_state;
    tap_event_t tap_buffer[3];      ///< Buffer for triple-tap sequence
    uint8_t tap_count;              ///< Current number of taps in sequence
    uint32_t sequence_start_ms;     ///< Start time of current sequence
    uint32_t last_debounce_ms;      ///< Last debounce period start time

    // Statistics
    tap_stats_t stats;

    // Callback management
    triple_tap_callback_t callback;
    void *callback_ctx;

    // Task management
    TaskHandle_t auto_task_handle;
    SemaphoreHandle_t mutex;
    bool initialized;
    bool auto_detection_active;

    // Vibration filtering
    float vibration_level;          ///< Current sustained vibration level
    uint32_t vibration_start_ms;    ///< Start time of current vibration
} tap_state = {0};

/**
 * @brief Calculate vibration level using moving average
 */
static void update_vibration_level(float magnitude) {
    const float alpha = 0.1f;  // Low-pass filter coefficient
    tap_state.vibration_level = alpha * magnitude + (1.0f - alpha) * tap_state.vibration_level;
}

/**
 * @brief Check if current conditions indicate vehicle vibration
 */
static bool is_vehicle_vibration(float magnitude) {
    if (!tap_state.config.filter_vibration) {
        return false;
    }

    // Sustained vibration detection
    if (tap_state.vibration_level > tap_state.config.vibration_threshold) {
        uint32_t current_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (tap_state.vibration_start_ms == 0) {
            tap_state.vibration_start_ms = current_ms;
        }

        // Consider it vibration if sustained for >200ms
        if ((current_ms - tap_state.vibration_start_ms) > 200) {
            return true;
        }
    } else {
        tap_state.vibration_start_ms = 0;
    }

    return false;
}

/**
 * @brief Validate tap timing against previous taps
 */
static bool is_valid_tap_timing(uint32_t timestamp_ms) {
    if (tap_state.tap_count == 0) {
        return true;  // First tap is always valid timing-wise
    }

    uint32_t interval = timestamp_ms - tap_state.tap_buffer[tap_state.tap_count - 1].timestamp_ms;

    return (interval >= tap_state.config.min_interval_ms &&
            interval <= tap_state.config.max_interval_ms);
}

/**
 * @brief Check if triple-tap sequence is complete and valid
 */
static bool is_sequence_complete(uint32_t timestamp_ms) {
    if (tap_state.tap_count < 3) {
        return false;
    }

    uint32_t total_duration = timestamp_ms - tap_state.sequence_start_ms;
    return (total_duration <= tap_state.config.window_ms);
}

/**
 * @brief Reset detection state to idle
 */
static void reset_detection_state(void) {
    tap_state.current_state = TAP_STATE_IDLE;
    tap_state.tap_count = 0;
    tap_state.sequence_start_ms = 0;
    memset(tap_state.tap_buffer, 0, sizeof(tap_state.tap_buffer));
}

/**
 * @brief Process a potential tap event
 */
static void process_tap_event(const accel_data_t *accel_data, float magnitude) {
    uint32_t current_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Check debounce period
    if (tap_state.current_state == TAP_STATE_DEBOUNCE) {
        uint32_t debounce_elapsed = current_ms - tap_state.last_debounce_ms;
        if (debounce_elapsed < tap_state.config.debounce_ms) {
            return;  // Still in debounce period
        } else {
            reset_detection_state();
        }
    }

    // Check if timing is valid for this tap
    if (!is_valid_tap_timing(current_ms)) {
        ESP_LOGD(TAG, "Invalid tap timing, resetting sequence");
        reset_detection_state();
        tap_state.stats.false_positives++;
        return;
    }

    // Create tap event
    tap_event_t tap_event = {
        .timestamp_ms = current_ms,
        .magnitude_g = magnitude,
        .axis_mask = 0,  // Will be set based on which axes contributed
        .is_valid = true
    };

    // Determine which axes contributed to the tap
    if (fabsf(accel_data->x) > tap_state.config.threshold_g / 2) {
        tap_event.axis_mask |= 0x01;  // X axis
    }
    if (fabsf(accel_data->y) > tap_state.config.threshold_g / 2) {
        tap_event.axis_mask |= 0x02;  // Y axis
    }
    if (fabsf(accel_data->z) > tap_state.config.threshold_g / 2) {
        tap_event.axis_mask |= 0x04;  // Z axis
    }

    // Add to buffer
    tap_state.tap_buffer[tap_state.tap_count] = tap_event;
    tap_state.tap_count++;

    // Update sequence timing
    if (tap_state.tap_count == 1) {
        tap_state.sequence_start_ms = current_ms;
        tap_state.current_state = TAP_STATE_FIRST;
    } else if (tap_state.tap_count == 2) {
        tap_state.current_state = TAP_STATE_SECOND;
    }

    // Update statistics
    tap_state.stats.total_taps++;
    tap_state.stats.avg_magnitude_g = (tap_state.stats.avg_magnitude_g * (tap_state.stats.total_taps - 1) + magnitude) / tap_state.stats.total_taps;

    ESP_LOGD(TAG, "Tap %d detected: %.2fg at %lu ms", tap_state.tap_count, magnitude, current_ms);

    // Check for sequence completion
    if (tap_state.tap_count == 3) {
        if (is_sequence_complete(current_ms)) {
            // Triple-tap detected!
            tap_state.current_state = TAP_STATE_DEBOUNCE;
            tap_state.last_debounce_ms = current_ms;
            tap_state.stats.triple_tap_count++;
            tap_state.stats.last_detection_ms = current_ms;

            ESP_LOGI(TAG, "Triple-tap detected! Total duration: %lu ms",
                     current_ms - tap_state.sequence_start_ms);

            // Call registered callback
            if (tap_state.callback) {
                tap_state.callback(tap_state.tap_buffer, tap_state.callback_ctx);
            }
        } else {
            ESP_LOGD(TAG, "Triple-tap sequence too slow, resetting");
            reset_detection_state();
            tap_state.stats.false_positives++;
        }
    }

    // Check for sequence timeout
    if (tap_state.tap_count > 0 && tap_state.tap_count < 3) {
        uint32_t elapsed = current_ms - tap_state.sequence_start_ms;
        if (elapsed > tap_state.config.window_ms) {
            ESP_LOGD(TAG, "Tap sequence timeout, resetting");
            reset_detection_state();
            tap_state.stats.false_positives++;
        }
    }
}

/**
 * @brief Auto-detection task with graceful error handling
 */
static void tap_detector_auto_task(void *pvParameters) {
    ESP_LOGI(TAG, "Auto tap detection task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(20);  // 50Hz sampling

    uint32_t consecutive_errors = 0;
    const uint32_t max_errors = 10;
    bool accel_disabled_logged = false;

    while (tap_state.auto_detection_active) {
        accel_data_t accel_data;
        esp_err_t ret = qmi8658_read_accel(&accel_data);

        if (ret == ESP_OK) {
            // Reset error count on successful read
            if (consecutive_errors > 0) {
                ESP_LOGI(TAG, "Accelerometer recovered, tap detection resumed");
                consecutive_errors = 0;
                accel_disabled_logged = false;
            }

            esp_err_t process_ret = tap_detector_process_data(&accel_data);
            if (process_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to process accelerometer data: %s", esp_err_to_name(process_ret));
            }
        } else {
            consecutive_errors++;

            // Only log first few errors, then go quiet to prevent spam
            if (consecutive_errors <= 3) {
                ESP_LOGW(TAG, "Failed to read accelerometer: %s (error %lu/%lu)",
                         esp_err_to_name(ret), consecutive_errors, max_errors);
            } else if (consecutive_errors >= max_errors && !accel_disabled_logged) {
                ESP_LOGW(TAG, "Accelerometer failure persistent after %lu errors - tap detection disabled",
                         consecutive_errors);
                accel_disabled_logged = true;
            }

            // Don't process data on error, but keep the task running for recovery
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }

    ESP_LOGI(TAG, "Auto tap detection task stopped");
    vTaskDelete(NULL);
}

esp_err_t tap_detector_init(const tap_config_t *config) {
    esp_err_t ret = ESP_OK;

    if (tap_state.initialized) {
        ESP_LOGW(TAG, "Tap detector already initialized");
        return ESP_OK;
    }

    // Setup default configuration
    if (config) {
        tap_state.config = *config;
    } else {
        tap_state.config.threshold_g = TAP_THRESHOLD_G_DEFAULT;
        tap_state.config.window_ms = TAP_WINDOW_MS_DEFAULT;
        tap_state.config.min_interval_ms = TAP_MIN_INTERVAL_MS;
        tap_state.config.max_interval_ms = TAP_MAX_INTERVAL_MS;
        tap_state.config.debounce_ms = TAP_DEBOUNCE_MS;
        tap_state.config.filter_vibration = true;
        tap_state.config.vibration_threshold = 0.5f;
    }

    // Create mutex
    tap_state.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(tap_state.mutex, ESP_ERR_NO_MEM, TAG, "Failed to create mutex");

    // Initialize state
    reset_detection_state();
    memset(&tap_state.stats, 0, sizeof(tap_state.stats));
    tap_state.callback = NULL;
    tap_state.callback_ctx = NULL;
    tap_state.auto_task_handle = NULL;
    tap_state.auto_detection_active = false;
    tap_state.vibration_level = 0.0f;
    tap_state.vibration_start_ms = 0;
    tap_state.last_debounce_ms = 0;

    tap_state.initialized = true;

    ESP_LOGI(TAG, "Tap detector initialized: threshold=%.1fg, window=%dms",
             tap_state.config.threshold_g, tap_state.config.window_ms);

    return ESP_OK;
}

esp_err_t tap_detector_register_callback(triple_tap_callback_t callback, void *user_ctx) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        tap_state.callback = callback;
        tap_state.callback_ctx = user_ctx;
        xSemaphoreGive(tap_state.mutex);
        ESP_LOGI(TAG, "Triple-tap callback registered");
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t tap_detector_process_data(const accel_data_t *accel_data) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(accel_data, ESP_ERR_INVALID_ARG, TAG, "accel_data is NULL");

    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    float magnitude = sqrtf(accel_data->x * accel_data->x +
                           accel_data->y * accel_data->y +
                           accel_data->z * accel_data->z);

    // Update vibration level for filtering
    update_vibration_level(magnitude);

    // Check for vehicle vibration
    if (is_vehicle_vibration(magnitude)) {
        tap_state.stats.vibration_events++;
        xSemaphoreGive(tap_state.mutex);
        return ESP_OK;  // Ignore this reading
    }

    // Check if magnitude exceeds tap threshold
    if (magnitude >= tap_state.config.threshold_g) {
        process_tap_event(accel_data, magnitude);
    }

    xSemaphoreGive(tap_state.mutex);
    return ESP_OK;
}

esp_err_t tap_detector_start_auto(void) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (tap_state.auto_detection_active) {
        ESP_LOGW(TAG, "Auto detection already active");
        return ESP_OK;
    }

    tap_state.auto_detection_active = true;

    BaseType_t task_created = xTaskCreate(
        tap_detector_auto_task,
        "tap_detector",
        3072,  // Larger stack for float operations
        NULL,
        6,     // High priority for real-time detection
        &tap_state.auto_task_handle
    );

    if (task_created != pdTRUE) {
        tap_state.auto_detection_active = false;
        ESP_LOGE(TAG, "Failed to create auto detection task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Auto tap detection started");
    return ESP_OK;
}

esp_err_t tap_detector_stop_auto(void) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (!tap_state.auto_detection_active) {
        return ESP_OK;
    }

    tap_state.auto_detection_active = false;

    // Wait for task to finish
    if (tap_state.auto_task_handle) {
        while (eTaskGetState(tap_state.auto_task_handle) != eDeleted) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        tap_state.auto_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Auto tap detection stopped");
    return ESP_OK;
}

tap_state_t tap_detector_get_state(void) {
    return tap_state.initialized ? tap_state.current_state : TAP_STATE_IDLE;
}

esp_err_t tap_detector_get_stats(tap_stats_t *stats) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(stats, ESP_ERR_INVALID_ARG, TAG, "stats is NULL");

    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *stats = tap_state.stats;
        xSemaphoreGive(tap_state.mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t tap_detector_reset_stats(void) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&tap_state.stats, 0, sizeof(tap_state.stats));
        xSemaphoreGive(tap_state.mutex);
        ESP_LOGI(TAG, "Statistics reset");
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t tap_detector_update_config(const tap_config_t *config) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is NULL");

    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        tap_state.config = *config;
        reset_detection_state();  // Reset state when config changes
        xSemaphoreGive(tap_state.mutex);

        ESP_LOGI(TAG, "Configuration updated: threshold=%.1fg, window=%dms",
                 config->threshold_g, config->window_ms);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t tap_detector_reset_state(void) {
    ESP_RETURN_ON_FALSE(tap_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        reset_detection_state();
        xSemaphoreGive(tap_state.mutex);
        ESP_LOGI(TAG, "Detection state reset");
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

const char* tap_state_to_string(tap_state_t state) {
    switch (state) {
        case TAP_STATE_IDLE:      return "IDLE";
        case TAP_STATE_FIRST:     return "FIRST_TAP";
        case TAP_STATE_SECOND:    return "SECOND_TAP";
        case TAP_STATE_DEBOUNCE:  return "DEBOUNCE";
        default:                  return "UNKNOWN";
    }
}

bool tap_detector_is_debouncing(void) {
    if (!tap_state.initialized) {
        return false;
    }

    bool debouncing = false;
    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (tap_state.current_state == TAP_STATE_DEBOUNCE) {
            uint32_t current_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t elapsed = current_ms - tap_state.last_debounce_ms;
            debouncing = (elapsed < tap_state.config.debounce_ms);
        }
        xSemaphoreGive(tap_state.mutex);
    }

    return debouncing;
}

uint32_t tap_detector_debounce_remaining_ms(void) {
    if (!tap_state.initialized || tap_state.current_state != TAP_STATE_DEBOUNCE) {
        return 0;
    }

    uint32_t remaining = 0;
    if (xSemaphoreTake(tap_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint32_t current_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed = current_ms - tap_state.last_debounce_ms;

        if (elapsed < tap_state.config.debounce_ms) {
            remaining = tap_state.config.debounce_ms - elapsed;
        }
        xSemaphoreGive(tap_state.mutex);
    }

    return remaining;
}

esp_err_t tap_detector_deinit(void) {
    if (!tap_state.initialized) {
        return ESP_OK;
    }

    // Stop auto detection
    tap_detector_stop_auto();

    // Cleanup mutex
    if (tap_state.mutex) {
        vSemaphoreDelete(tap_state.mutex);
        tap_state.mutex = NULL;
    }

    tap_state.initialized = false;
    ESP_LOGI(TAG, "Tap detector deinitialized");

    return ESP_OK;
}