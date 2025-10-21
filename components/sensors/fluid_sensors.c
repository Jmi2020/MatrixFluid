/**
 * @file fluid_sensors.c
 * @brief Fluid Level Sensors Implementation
 */

#include "fluid_sensors.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char *TAG = "fluid_sensors";

// Module state
static struct {
    fluid_config_t config;
    fluid_level_t current_level;
    fluid_level_t stable_level;
    uint32_t level_change_time_ms;
    uint32_t error_count;

    // Callback management
    fluid_level_callback_t callback;
    void *callback_ctx;

    // Task management
    TaskHandle_t monitor_task_handle;
    SemaphoreHandle_t mutex;
    bool initialized;
    bool monitoring_active;
} fluid_state = {0};

static const gpio_num_t SENSOR_GPIO_MAP[FLUID_SENSOR_COUNT] = {
    FLUID_FULL_SENSOR_GPIO,
    FLUID_HALF_SENSOR_GPIO,
    FLUID_LOW_SENSOR_GPIO,
    FLUID_EMPTY_SENSOR_GPIO
};

/**
 * @brief Determine fluid level from sensor readings
 */
static fluid_level_t determine_level(const fluid_reading_t *reading) {
    if (!reading) {
        return FLUID_LEVEL_SENSOR_ERROR;
    }

    bool sensors[FLUID_SENSOR_COUNT] = {
        reading->full_sensor_submerged,
        reading->half_sensor_submerged,
        reading->low_sensor_submerged,
        reading->empty_sensor_submerged
    };

    bool seen_dry = false;
    for (int i = 0; i < FLUID_SENSOR_COUNT; ++i) {
        if (!sensors[i]) {
            seen_dry = true;
        } else if (seen_dry) {
            // Higher sensor reports dry but lower sensor still wet -> inconsistent
            return FLUID_LEVEL_SENSOR_ERROR;
        }
    }

    if (sensors[0]) {
        return FLUID_LEVEL_FULL;
    }
    if (sensors[1]) {
        return FLUID_LEVEL_ABOVE_HALF;
    }
    if (sensors[2]) {
        return FLUID_LEVEL_BELOW_HALF;
    }
    if (sensors[3]) {
        return FLUID_LEVEL_NEAR_EMPTY;
    }
    return FLUID_LEVEL_EMPTY;
}

/**
 * @brief Background monitoring task
 */
static void fluid_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Fluid monitoring task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval_ticks = pdMS_TO_TICKS(fluid_state.config.read_interval_ms);

    while (fluid_state.monitoring_active) {
        fluid_reading_t reading;
        esp_err_t ret = fluid_sensors_read_raw(&reading);

        if (ret == ESP_OK && reading.is_valid) {
            fluid_level_t new_level = determine_level(&reading);

            if (xSemaphoreTake(fluid_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (new_level != fluid_state.current_level) {
                    // Level change detected - start/reset debounce timer
                    fluid_state.current_level = new_level;
                    fluid_state.level_change_time_ms = reading.timestamp_ms;
                } else {
                    // Level is stable - check if debounce period has passed
                    uint32_t stable_time = reading.timestamp_ms - fluid_state.level_change_time_ms;

                    if (stable_time >= fluid_state.config.debounce_ms &&
                        new_level != fluid_state.stable_level) {

                        fluid_level_t old_level = fluid_state.stable_level;
                        fluid_state.stable_level = new_level;

                        if (new_level == FLUID_LEVEL_SENSOR_ERROR) {
                            fluid_state.error_count++;
                            ESP_LOGW(TAG, "Sensor error detected (count: %lu)", fluid_state.error_count);
                        }

                        // Call registered callback
                        if (fluid_state.callback) {
                            fluid_state.callback(new_level, old_level, fluid_state.callback_ctx);
                        }

                        ESP_LOGI(TAG, "Fluid level changed: %s -> %s",
                                fluid_level_to_string(old_level),
                                fluid_level_to_string(new_level));
                    }
                }
                xSemaphoreGive(fluid_state.mutex);
            }
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }

    ESP_LOGI(TAG, "Fluid monitoring task stopped");
    vTaskDelete(NULL);
}

esp_err_t fluid_sensors_init(const fluid_config_t *config) {
    esp_err_t ret = ESP_OK;

    if (fluid_state.initialized) {
        ESP_LOGW(TAG, "Fluid sensors already initialized");
        return ESP_OK;
    }

    // Setup default configuration
    if (config) {
        fluid_state.config = *config;
    } else {
        fluid_state.config.debounce_ms = FLUID_DEBOUNCE_MS;
        fluid_state.config.read_interval_ms = FLUID_READ_INTERVAL_MS;
        fluid_state.config.enable_monitoring = true;
    }

    // Create mutex
    fluid_state.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(fluid_state.mutex, ESP_ERR_NO_MEM, TAG, "Failed to create mutex");

    // Configure GPIO pins
    uint64_t pin_mask = 0;
    for (int i = 0; i < FLUID_SENSOR_COUNT; ++i) {
        pin_mask |= (1ULL << SENSOR_GPIO_MAP[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Bias low until sensor drives line high
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), cleanup, TAG, "GPIO config failed");

    // Initialize state
    fluid_state.current_level = FLUID_LEVEL_SENSOR_ERROR;
    fluid_state.stable_level = FLUID_LEVEL_SENSOR_ERROR;
    fluid_state.level_change_time_ms = 0;
    fluid_state.error_count = 0;
    fluid_state.callback = NULL;
    fluid_state.callback_ctx = NULL;
    fluid_state.monitor_task_handle = NULL;
    fluid_state.monitoring_active = false;

    // Mark initialized before taking initial reading so helper can run safely
    fluid_state.initialized = true;

    // Read initial state
    fluid_reading_t initial_reading;
    ret = fluid_sensors_read_raw(&initial_reading);
    if (ret == ESP_OK && initial_reading.is_valid) {
        fluid_state.stable_level = determine_level(&initial_reading);
        fluid_state.current_level = fluid_state.stable_level;
        fluid_state.level_change_time_ms = initial_reading.timestamp_ms;
    }

    // Start monitoring if enabled
    if (fluid_state.config.enable_monitoring) {
        ESP_GOTO_ON_ERROR(fluid_sensors_start_monitoring(), cleanup, TAG, "Failed to start monitoring");
    }

    ESP_LOGI(TAG, "Fluid sensors initialized, initial level: %s",
             fluid_level_to_string(fluid_state.stable_level));

    return ESP_OK;

cleanup:
    if (fluid_state.mutex) {
        vSemaphoreDelete(fluid_state.mutex);
        fluid_state.mutex = NULL;
    }
    return ret;
}

fluid_level_t fluid_sensors_get_level(void) {
    if (!fluid_state.initialized) {
        return FLUID_LEVEL_SENSOR_ERROR;
    }

    fluid_level_t level = FLUID_LEVEL_SENSOR_ERROR;
    if (xSemaphoreTake(fluid_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        level = fluid_state.stable_level;
        xSemaphoreGive(fluid_state.mutex);
    }

    return level;
}

esp_err_t fluid_sensors_read_raw(fluid_reading_t *reading) {
    ESP_RETURN_ON_FALSE(fluid_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(reading, ESP_ERR_INVALID_ARG, TAG, "reading is NULL");

    memset(reading, 0, sizeof(*reading));

    bool signals[FLUID_SENSOR_COUNT] = {0};
    for (int i = 0; i < FLUID_SENSOR_COUNT; ++i) {
        signals[i] = gpio_get_level(SENSOR_GPIO_MAP[i]) != 0;
    }

    reading->full_sensor_signal_high = signals[0];
    reading->full_sensor_submerged = signals[0];
    reading->half_sensor_signal_high = signals[1];
    reading->half_sensor_submerged = signals[1];
    reading->low_sensor_signal_high = signals[2];
    reading->low_sensor_submerged = signals[2];
    reading->empty_sensor_signal_high = signals[3];
    reading->empty_sensor_submerged = signals[3];
    reading->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    reading->is_valid = true;

    return ESP_OK;
}

esp_err_t fluid_sensors_register_callback(fluid_level_callback_t callback, void *user_ctx) {
    ESP_RETURN_ON_FALSE(fluid_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (xSemaphoreTake(fluid_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        fluid_state.callback = callback;
        fluid_state.callback_ctx = user_ctx;
        xSemaphoreGive(fluid_state.mutex);
        ESP_LOGI(TAG, "Fluid level callback registered");
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t fluid_sensors_start_monitoring(void) {
    ESP_RETURN_ON_FALSE(fluid_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (fluid_state.monitoring_active) {
        ESP_LOGW(TAG, "Monitoring already active");
        return ESP_OK;
    }

    fluid_state.monitoring_active = true;

    BaseType_t task_created = xTaskCreate(
        fluid_monitor_task,
        "fluid_monitor",
        2048,
        NULL,
        5,  // High priority for sensor monitoring
        &fluid_state.monitor_task_handle
    );

    if (task_created != pdTRUE) {
        fluid_state.monitoring_active = false;
        ESP_LOGE(TAG, "Failed to create monitoring task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Fluid monitoring started");
    return ESP_OK;
}

esp_err_t fluid_sensors_stop_monitoring(void) {
    ESP_RETURN_ON_FALSE(fluid_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (!fluid_state.monitoring_active) {
        return ESP_OK;
    }

    fluid_state.monitoring_active = false;

    // Wait for task to finish
    if (fluid_state.monitor_task_handle) {
        // Task will delete itself when monitoring_active becomes false
        while (eTaskGetState(fluid_state.monitor_task_handle) != eDeleted) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        fluid_state.monitor_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Fluid monitoring stopped");
    return ESP_OK;
}

const char* fluid_level_to_string(fluid_level_t level) {
    switch (level) {
        case FLUID_LEVEL_FULL:       return "FULL";
        case FLUID_LEVEL_ABOVE_HALF:  return "ABOVE_HALF";
        case FLUID_LEVEL_BELOW_HALF:  return "BELOW_HALF";
        case FLUID_LEVEL_NEAR_EMPTY:  return "NEAR_EMPTY";
        case FLUID_LEVEL_EMPTY:       return "EMPTY";
        case FLUID_LEVEL_SENSOR_ERROR: return "SENSOR_ERROR";
        default:                      return "UNKNOWN";
    }
}

uint32_t fluid_sensors_get_error_count(void) {
    if (!fluid_state.initialized) {
        return 0;
    }

    uint32_t count = 0;
    if (xSemaphoreTake(fluid_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        count = fluid_state.error_count;
        xSemaphoreGive(fluid_state.mutex);
    }

    return count;
}

esp_err_t fluid_sensors_deinit(void) {
    if (!fluid_state.initialized) {
        return ESP_OK;
    }

    // Stop monitoring
    fluid_sensors_stop_monitoring();

    // Reset GPIO pins
    for (int i = 0; i < FLUID_SENSOR_COUNT; ++i) {
        gpio_reset_pin(SENSOR_GPIO_MAP[i]);
    }

    // Cleanup mutex
    if (fluid_state.mutex) {
        vSemaphoreDelete(fluid_state.mutex);
        fluid_state.mutex = NULL;
    }

    fluid_state.initialized = false;
    ESP_LOGI(TAG, "Fluid sensors deinitialized");

    return ESP_OK;
}

uint8_t fluid_level_to_percent(fluid_level_t level) {
    switch (level) {
        case FLUID_LEVEL_FULL:
            return 80;
        case FLUID_LEVEL_ABOVE_HALF:
            return 60;
        case FLUID_LEVEL_BELOW_HALF:
            return 40;
        case FLUID_LEVEL_NEAR_EMPTY:
            return 20;
        case FLUID_LEVEL_EMPTY:
            return 0;
        case FLUID_LEVEL_SENSOR_ERROR:
        default:
            return 0;
    }
}
