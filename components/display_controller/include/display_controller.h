/**
 * @file display_controller.h
 * @brief Timer-based Display Controller for MatrixFluid
 *
 * Replaces accelerometer-based activation with reliable timer-based control
 * and web interface integration for manual triggers.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "fluid_sensors.h"
#include "led_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display activation modes
 */
typedef enum {
    DISPLAY_MODE_OFF = 0,           ///< Display disabled
    DISPLAY_MODE_MANUAL_ONLY,       ///< Manual triggers only (web interface)
    DISPLAY_MODE_PERIODIC,          ///< Automatic periodic display
    DISPLAY_MODE_ON_CHANGE,         ///< Display when fluid level changes
    DISPLAY_MODE_ALWAYS_ON          ///< Always display current level
} display_mode_t;

/**
 * @brief Display controller configuration
 */
typedef struct {
    display_mode_t mode;                ///< Display activation mode
    uint32_t periodic_interval_ms;     ///< Interval for periodic mode (ms)
    uint32_t display_duration_ms;      ///< How long to show display (ms)
    uint8_t brightness;                ///< Display brightness (0-5)
    bool fade_in_out;                  ///< Enable fade in/out effects
    bool show_startup_sequence;        ///< Show sequence on startup
} display_controller_config_t;

/**
 * @brief Display trigger source
 */
typedef enum {
    TRIGGER_SOURCE_MANUAL = 0,      ///< Manual web interface trigger
    TRIGGER_SOURCE_PERIODIC,        ///< Automatic periodic timer
    TRIGGER_SOURCE_FLUID_CHANGE,    ///< Fluid level change detected
    TRIGGER_SOURCE_STARTUP,         ///< System startup
    TRIGGER_SOURCE_SYSTEM,          ///< System-triggered message (e.g. Wi-Fi banner)
    TRIGGER_SOURCE_DEMO             ///< Demo mode
} trigger_source_t;

/**
 * @brief Display controller statistics
 */
typedef struct {
    uint32_t total_displays;        ///< Total number of displays shown
    uint32_t manual_triggers;       ///< Manual trigger count
    uint32_t periodic_triggers;     ///< Periodic trigger count
    uint32_t fluid_change_triggers; ///< Fluid change trigger count
    uint32_t last_display_time;     ///< Last display timestamp (ms)
    trigger_source_t last_trigger;  ///< Source of last trigger
} display_stats_t;

/**
 * @brief Display event callback function type
 *
 * @param source Trigger source
 * @param fluid_level Current fluid level
 * @param user_ctx User context pointer
 */
typedef void (*display_event_callback_t)(trigger_source_t source, fluid_level_t fluid_level, void *user_ctx);

/**
 * @brief Initialize display controller
 *
 * @param config Display controller configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_init(const display_controller_config_t *config);

/**
 * @brief Start display controller
 *
 * Begins timers and monitoring based on configuration
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_start(void);

/**
 * @brief Stop display controller
 *
 * Stops all timers and clears display
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_stop(void);

/**
 * @brief Trigger manual display
 *
 * Shows current fluid level immediately
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_trigger_manual(void);

/**
 * @brief Update display configuration
 *
 * @param config New configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_set_config(const display_controller_config_t *config);

/**
 * @brief Get current display configuration
 *
 * @param config Pointer to store current configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_get_config(display_controller_config_t *config);

/**
 * @brief Update current fluid level
 *
 * Call this when fluid level changes to trigger display if configured
 *
 * @param new_level New fluid level
 * @param old_level Previous fluid level
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_update_fluid_level(fluid_level_t new_level, fluid_level_t old_level);

/**
 * @brief Register display event callback
 *
 * @param callback Callback function
 * @param user_ctx User context pointer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_register_callback(display_event_callback_t callback, void *user_ctx);

/**
 * @brief Get display controller statistics
 *
 * @param stats Pointer to store statistics
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_get_stats(display_stats_t *stats);

/**
 * @brief Check if display is currently active
 *
 * @return true if display is showing
 */
bool display_controller_is_active(void);

/**
 * @brief Get the fluid level currently being displayed.
 */
fluid_level_t display_controller_get_current_level(void);

/**
 * @brief Force display off
 *
 * Immediately turns off display regardless of timers
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_force_off(void);

/**
 * @brief Show an arbitrary message on the display.
 *
 * @param text Null-terminated text to display (converted to uppercase)
 * @param color Text color
 * @param duration_ms How long to display message (0 to use configured duration)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_show_message(const char *text,
                                          led_color_t color,
                                          uint32_t duration_ms);

/**
 * @brief Set brightness
 *
 * Updates brightness immediately if display is active
 *
 * @param brightness New brightness (0-5)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_set_brightness(uint8_t brightness);

/**
 * @brief Show startup sequence
 *
 * Displays a welcome/initialization sequence
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_show_startup_sequence(void);

/**
 * @brief Get display mode name
 *
 * @param mode Display mode
 * @return String representation of mode
 */
const char* display_controller_mode_to_string(display_mode_t mode);

/**
 * @brief Get trigger source name
 *
 * @param source Trigger source
 * @return String representation of source
 */
const char* display_controller_trigger_to_string(trigger_source_t source);

/**
 * @brief Deinitialize display controller
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_controller_deinit(void);

#ifdef __cplusplus
}
#endif
