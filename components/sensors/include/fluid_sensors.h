/**
 * @file fluid_sensors.h
 * @brief Fluid Level Sensors Interface
 *
 * Manages two digital fluid level sensors:
 * - Half-full sensor (GPIO2)
 * - Near-empty sensor (GPIO3)
 *
 * Provides debounced readings and fluid level state determination.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO pin assignments
#define FLUID_HALF_SENSOR_GPIO    2    ///< Half-full sensor GPIO
#define FLUID_EMPTY_SENSOR_GPIO   3    ///< Near-empty sensor GPIO

// Timing constants
#define FLUID_DEBOUNCE_MS         100  ///< Debounce time for sensor readings
#define FLUID_READ_INTERVAL_MS    50   ///< Reading interval for monitoring task

/**
 * @brief Fluid level states based on sensor combination
 */
typedef enum {
    FLUID_LEVEL_ABOVE_HALF = 0,   ///< Both sensors HIGH (tank >50%)
    FLUID_LEVEL_BELOW_HALF = 1,   ///< Half sensor LOW, Empty sensor HIGH (20-50%)
    FLUID_LEVEL_NEAR_EMPTY = 2,   ///< Both sensors LOW (tank <20%)
    FLUID_LEVEL_SENSOR_ERROR = 3  ///< Invalid sensor combination
} fluid_level_t;

/**
 * @brief Raw sensor reading structure
 */
typedef struct {
    bool half_sensor;       ///< Half-full sensor state (true = float submerged)
    bool empty_sensor;      ///< Near-empty sensor state (true = float submerged)
    uint32_t timestamp_ms;  ///< Reading timestamp
    bool is_valid;          ///< Reading validity flag
} fluid_reading_t;

/**
 * @brief Fluid sensor configuration
 */
typedef struct {
    uint32_t debounce_ms;       ///< Debounce time (default: FLUID_DEBOUNCE_MS)
    uint32_t read_interval_ms;  ///< Reading interval (default: FLUID_READ_INTERVAL_MS)
    bool enable_monitoring;     ///< Enable background monitoring task
} fluid_config_t;

/**
 * @brief Fluid level change callback function type
 *
 * @param new_level New fluid level
 * @param old_level Previous fluid level
 * @param user_ctx User context data
 */
typedef void (*fluid_level_callback_t)(fluid_level_t new_level, fluid_level_t old_level, void *user_ctx);

/**
 * @brief Initialize fluid sensors
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fluid_sensors_init(const fluid_config_t *config);

/**
 * @brief Get current fluid level (debounced)
 *
 * @return Current fluid level state
 */
fluid_level_t fluid_sensors_get_level(void);

/**
 * @brief Get raw sensor reading (immediate, not debounced)
 *
 * @param reading Pointer to store reading
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fluid_sensors_read_raw(fluid_reading_t *reading);

/**
 * @brief Register callback for fluid level changes
 *
 * @param callback Callback function
 * @param user_ctx User context data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fluid_sensors_register_callback(fluid_level_callback_t callback, void *user_ctx);

/**
 * @brief Start background monitoring task
 *
 * Task monitors sensors and calls registered callbacks on level changes.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fluid_sensors_start_monitoring(void);

/**
 * @brief Stop background monitoring task
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fluid_sensors_stop_monitoring(void);

/**
 * @brief Convert fluid level to string
 *
 * @param level Fluid level enum
 * @return const char* String representation
 */
const char* fluid_level_to_string(fluid_level_t level);

/**
 * @brief Get sensor error count since initialization
 *
 * @return Number of invalid sensor readings detected
 */
uint32_t fluid_sensors_get_error_count(void);

/**
 * @brief Deinitialize fluid sensors
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t fluid_sensors_deinit(void);

#ifdef __cplusplus
}
#endif
