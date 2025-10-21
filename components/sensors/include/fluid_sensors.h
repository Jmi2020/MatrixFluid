/**
 * @file fluid_sensors.h
 * @brief Fluid Level Sensors Interface
 *
 * Manages four digital fluid level sensors (see FLUID_*_SENSOR_GPIO for pin assignments):
 * - Full sensor (highest threshold)
 * - Above-half sensor
 * - Below-half sensor
 * - Reserve / near-empty sensor
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

// GPIO pin assignments (override at compile time if different pins are required)
#ifndef FLUID_FULL_SENSOR_GPIO
#define FLUID_FULL_SENSOR_GPIO    4    ///< Top sensor (tank full threshold)
#endif
#ifndef FLUID_HALF_SENSOR_GPIO
#define FLUID_HALF_SENSOR_GPIO    2    ///< Upper-mid sensor (~above half)
#endif
#ifndef FLUID_LOW_SENSOR_GPIO
#define FLUID_LOW_SENSOR_GPIO     5    ///< Lower-mid sensor (~below half)
#endif
#ifndef FLUID_EMPTY_SENSOR_GPIO
#define FLUID_EMPTY_SENSOR_GPIO   3    ///< Reserve/near-empty sensor
#endif

#define FLUID_SENSOR_COUNT        4    ///< Total supported sensor inputs

// Timing constants
#define FLUID_DEBOUNCE_MS         100  ///< Debounce time for sensor readings
#define FLUID_READ_INTERVAL_MS    50   ///< Reading interval for monitoring task

/**
 * @brief Fluid level states based on sensor combination
 */
typedef enum {
    FLUID_LEVEL_FULL = 0,         ///< Top sensor HIGH (tank at maximum threshold)
    FLUID_LEVEL_ABOVE_HALF = 1,   ///< Upper-mid sensor HIGH (tank > ~50%)
    FLUID_LEVEL_BELOW_HALF = 2,   ///< Lower-mid sensor HIGH (tank between reserve & ~50%)
    FLUID_LEVEL_NEAR_EMPTY = 3,   ///< Reserve sensor HIGH (tank just above empty)
    FLUID_LEVEL_EMPTY = 4,        ///< No sensors HIGH (tank below reserve threshold)
    FLUID_LEVEL_SENSOR_ERROR = 5  ///< Invalid sensor combination detected
} fluid_level_t;

/**
 * @brief Raw sensor reading structure
 */
typedef struct {
    bool full_sensor_submerged;       ///< Full-level sensor (true = submerged / signal HIGH)
    bool half_sensor_submerged;      ///< Half-full sensor (true = float submerged / signal HIGH)
    bool low_sensor_submerged;       ///< Lower-mid sensor (true = submerged / signal HIGH)
    bool empty_sensor_submerged;     ///< Near-empty sensor (true = float submerged / signal HIGH)
    bool full_sensor_signal_high;    ///< Raw GPIO reading for full sensor is HIGH (>=3V)
    bool half_sensor_signal_high;    ///< Raw GPIO reading is HIGH (>=3V)
    bool low_sensor_signal_high;     ///< Raw GPIO reading for lower-mid sensor is HIGH (>=3V)
    bool empty_sensor_signal_high;   ///< Raw GPIO reading is HIGH (>=3V)
    uint32_t timestamp_ms;           ///< Reading timestamp in milliseconds
    bool is_valid;                   ///< Reading validity flag
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
