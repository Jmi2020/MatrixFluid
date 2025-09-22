/**
 * @file fluid_sensors.c
 * @brief Fluid Level Sensors Implementation
 */

#include "fluid_sensors.h"
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

/**
 * @brief Determine fluid level from sensor readings
 */
static fluid_level_t determine_level(bool half_sensor, bool empty_sensor) {
    if (half_sensor && empty_sensor) {
        return FLUID_LEVEL_ABOVE_HALF;
    } else if (!half_sensor && empty_sensor) {
        return FLUID_LEVEL_BELOW_HALF;
    } else if (!half_sensor && !empty_sensor) {
        return FLUID_LEVEL_NEAR_EMPTY;
    } else {
        // half_sensor == true && empty_sensor == false
        // This is physically impossible - empty sensor should be HIGH if half sensor is HIGH
        return FLUID_LEVEL_SENSOR_ERROR;
    }
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
            fluid_level_t new_level = determine_level(reading.half_sensor, reading.empty_sensor);

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
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FLUID_HALF_SENSOR_GPIO) | (1ULL << FLUID_EMPTY_SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // Enable pull-up for open-drain sensors
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
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
        fluid_state.stable_level = determine_level(initial_reading.half_sensor, initial_reading.empty_sensor);
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

    int half_raw = gpio_get_level(FLUID_HALF_SENSOR_GPIO);
    int empty_raw = gpio_get_level(FLUID_EMPTY_SENSOR_GPIO);

    // Floats pull the line low when liquid is present; convert to bool accordingly.
    reading->half_sensor = (half_raw == 0);
    reading->empty_sensor = (empty_raw == 0);
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
        case FLUID_LEVEL_ABOVE_HALF:  return "ABOVE_HALF";
        case FLUID_LEVEL_BELOW_HALF:  return "BELOW_HALF";
        case FLUID_LEVEL_NEAR_EMPTY:  return "NEAR_EMPTY";
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
    gpio_reset_pin(FLUID_HALF_SENSOR_GPIO);
    gpio_reset_pin(FLUID_EMPTY_SENSOR_GPIO);

    // Cleanup mutex
    if (fluid_state.mutex) {
        vSemaphoreDelete(fluid_state.mutex);
        fluid_state.mutex = NULL;
    }

    fluid_state.initialized = false;
    ESP_LOGI(TAG, "Fluid sensors deinitialized");

    return ESP_OK;
}
