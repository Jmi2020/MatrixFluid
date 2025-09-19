/**
 * @file tap_detector.h
 * @brief Triple-Tap Detection Algorithm
 *
 * Detects triple-tap gestures from accelerometer data while filtering out
 * vehicle vibrations and other false triggers.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "qmi8658_accel.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default detection parameters
#define TAP_THRESHOLD_G_DEFAULT     1.5f    ///< Default tap threshold (g-force)
#define TAP_WINDOW_MS_DEFAULT       500     ///< Default time window for triple-tap (ms)
#define TAP_MIN_INTERVAL_MS         150     ///< Minimum time between taps (ms)
#define TAP_MAX_INTERVAL_MS         500     ///< Maximum time between taps (ms)
#define TAP_DEBOUNCE_MS            1000     ///< Debounce period after successful detection
#define TAP_BUFFER_SIZE              10     ///< Size of tap event buffer

/**
 * @brief Tap detection state
 */
typedef enum {
    TAP_STATE_IDLE = 0,     ///< Waiting for first tap
    TAP_STATE_FIRST,        ///< First tap detected, waiting for second
    TAP_STATE_SECOND,       ///< Second tap detected, waiting for third
    TAP_STATE_DEBOUNCE      ///< Triple-tap detected, in debounce period
} tap_state_t;

/**
 * @brief Individual tap event data
 */
typedef struct {
    uint32_t timestamp_ms;  ///< Tap timestamp
    float magnitude_g;      ///< Peak acceleration magnitude
    uint8_t axis_mask;      ///< Axes that contributed to tap (bit flags: X=1, Y=2, Z=4)
    bool is_valid;          ///< Event validity flag
} tap_event_t;

/**
 * @brief Tap detection configuration
 */
typedef struct {
    float threshold_g;          ///< Minimum acceleration to register as tap
    uint16_t window_ms;         ///< Maximum time window for triple-tap sequence
    uint16_t min_interval_ms;   ///< Minimum time between individual taps
    uint16_t max_interval_ms;   ///< Maximum time between individual taps
    uint16_t debounce_ms;       ///< Debounce period after successful detection
    bool filter_vibration;     ///< Enable vehicle vibration filtering
    float vibration_threshold;  ///< Sustained vibration level to ignore (g)
} tap_config_t;

/**
 * @brief Triple-tap detection callback function type
 *
 * @param tap_events Array of the three tap events that triggered detection
 * @param user_ctx User context data
 */
typedef void (*triple_tap_callback_t)(const tap_event_t tap_events[3], void *user_ctx);

/**
 * @brief Tap detection statistics
 */
typedef struct {
    uint32_t total_taps;            ///< Total tap events detected
    uint32_t triple_tap_count;      ///< Successful triple-tap detections
    uint32_t false_positives;       ///< False positive count (filtered out)
    uint32_t vibration_events;      ///< Vehicle vibration events detected
    float avg_magnitude_g;          ///< Average tap magnitude
    uint32_t last_detection_ms;     ///< Timestamp of last successful detection
} tap_stats_t;

/**
 * @brief Initialize tap detector
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_init(const tap_config_t *config);

/**
 * @brief Register triple-tap callback
 *
 * @param callback Callback function to call on triple-tap detection
 * @param user_ctx User context data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_register_callback(triple_tap_callback_t callback, void *user_ctx);

/**
 * @brief Process accelerometer data for tap detection
 *
 * Call this function with fresh accelerometer readings. The detector will
 * analyze the data and call the registered callback on triple-tap detection.
 *
 * @param accel_data Current accelerometer reading
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_process_data(const accel_data_t *accel_data);

/**
 * @brief Start automatic tap detection
 *
 * Creates a task that continuously reads from the accelerometer and processes
 * tap events. Alternative to manual calls to tap_detector_process_data().
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_start_auto(void);

/**
 * @brief Stop automatic tap detection
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_stop_auto(void);

/**
 * @brief Get current detection state
 *
 * @return tap_state_t Current state
 */
tap_state_t tap_detector_get_state(void);

/**
 * @brief Get detection statistics
 *
 * @param stats Pointer to store statistics
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_get_stats(tap_stats_t *stats);

/**
 * @brief Reset detection statistics
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_reset_stats(void);

/**
 * @brief Update detection configuration
 *
 * @param config New configuration parameters
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_update_config(const tap_config_t *config);

/**
 * @brief Force reset detection state
 *
 * Clears any partial tap sequence and returns to idle state.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_reset_state(void);

/**
 * @brief Convert tap state to string
 *
 * @param state Tap state enum
 * @return const char* String representation
 */
const char* tap_state_to_string(tap_state_t state);

/**
 * @brief Check if detector is currently in debounce period
 *
 * @return true In debounce period (ignoring new taps)
 * @return false Ready to detect taps
 */
bool tap_detector_is_debouncing(void);

/**
 * @brief Get time remaining in current debounce period
 *
 * @return uint32_t Milliseconds remaining (0 if not debouncing)
 */
uint32_t tap_detector_debounce_remaining_ms(void);

/**
 * @brief Deinitialize tap detector
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tap_detector_deinit(void);

#ifdef __cplusplus
}
#endif