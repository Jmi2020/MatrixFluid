/**
 * @file demo_mode.h
 * @brief Demo Mode Detection and Management
 *
 * Detects whether the device is connected to a USB host (demo mode)
 * or running from the vehicle power rail. Provides pin assignment displays
 * and looping demo animations for kiosk setups.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Demo mode configuration
#define DEMO_ANIMATION_INTERVAL_MS    3000    ///< Time between demo animations
#define DEMO_PIN_DISPLAY_MS           5000    ///< Time to show pin assignments

/**
 * @brief Demo mode state
 */
typedef enum {
    DEMO_MODE_DISABLED = 0,     ///< Operational mode
    DEMO_MODE_USB_POWER,        ///< USB host detected, demo eligible
    DEMO_MODE_SHOW_PINS,        ///< Showing pin assignments
    DEMO_MODE_ANIMATE           ///< Running demo animations
} demo_mode_state_t;

/**
 * @brief Power source detection
 */
typedef enum {
    POWER_SOURCE_UNKNOWN = 0,
    POWER_SOURCE_USB,           ///< USB host detected on USB-Serial/JTAG
    POWER_SOURCE_5V_BUCK        ///< External 5V supply (vehicle power)
} power_source_t;

/**
 * @brief Demo mode configuration
 */
typedef struct {
    bool enable_detection;      ///< Enable automatic demo mode detection
    uint32_t check_interval_ms; ///< Poll interval for detecting USB host
    bool verbose_logging;       ///< Enable verbose demo mode logging
} demo_config_t;

/**
 * @brief Initialize demo mode detection
 *
 * @param config Demo mode configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t demo_mode_init(const demo_config_t *config);

/**
 * @brief Check if device is in demo mode
 *
 * Uses USB-Serial/JTAG host presence to determine demo eligibility.
 *
 * @return true if in demo mode, false if operational
 */
bool demo_mode_is_active(void);

/**
 * @brief Get current power source
 *
 * @return power_source_t Current power source
 */
power_source_t demo_mode_get_power_source(void);

/**
 * @brief Get current demo mode state
 *
 * @return demo_mode_state_t Current state
 */
demo_mode_state_t demo_mode_get_state(void);

const char *demo_mode_state_to_string(demo_mode_state_t state);
const char *demo_mode_power_source_to_string(power_source_t source);
/**
 * @brief Start demo mode sequence
 *
 * Begins showing pin assignments and demo animations
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t demo_mode_start(void);

/**
 * @brief Stop demo mode
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t demo_mode_stop(void);

/**
 * @brief Demo mode task function
 *
 * Should be called periodically to update demo animations
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t demo_mode_update(void);

/**
 * @brief Print demo mode status
 *
 * Logs current power source, detection status, and demo state
*/
void demo_mode_print_status(void);

#ifdef __cplusplus
}
#endif
