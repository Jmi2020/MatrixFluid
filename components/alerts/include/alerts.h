#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "fluid_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Alert delivery frequency options.
 */
typedef enum {
    ALERT_FREQUENCY_DAILY = 0,
    ALERT_FREQUENCY_WEEKLY = 1,
    ALERT_FREQUENCY_WEEKDAYS = 2
} alert_frequency_t;

/**
 * @brief Persistent alert configuration saved in NVS.
 */
typedef struct {
    bool enabled;
    alert_frequency_t frequency;
    uint8_t days_bitmap;          ///< Bitmask for days (bit0=Sunday .. bit6=Saturday)
    uint8_t hour;                 ///< 0-23 local hour
    uint8_t minute;               ///< 0-59 local minute
    int16_t tz_offset_minutes;    ///< Offset from UTC in minutes
    char webhook_url[128];        ///< Endpoint to POST alert JSON
    char auth_header[96];         ///< Optional Authorization header value
    char recipient[64];           ///< Optional recipient/email identifier
} alert_config_t;

/**
 * @brief Snapshot of portal-facing alert information.
 */
typedef struct {
    bool enabled;
    uint8_t frequency;
    uint8_t days_bitmap;
    uint8_t hour;
    uint8_t minute;
    int16_t tz_offset_minutes;
    bool last_success;
    bool last_attempt_failed;
    uint32_t last_attempt_epoch;
    uint32_t last_success_epoch;
    uint32_t next_run_epoch;
    char webhook_url[128];
    char auth_header[96];
    char recipient[64];
    char last_error[96];
} alert_portal_status_t;

/**
 * @brief Live sensor snapshot forwarded from the UI layer.
 */
typedef struct {
    fluid_level_t fluid_level;
    bool full_submerged;
    bool half_submerged;
    bool low_submerged;
    bool empty_submerged;
    bool full_signal_high;
    bool half_signal_high;
    bool low_signal_high;
    bool empty_signal_high;
    uint32_t uptime_seconds;
    char power_source[16];
} alert_snapshot_t;

/**
 * @brief Initialise alert subsystem (load config, start scheduler).
 */
void alerts_init(void);

/**
 * @brief Update latest device status snapshot for future notifications.
 */
void alerts_update_snapshot(const alert_snapshot_t *snapshot);

/**
 * @brief Retrieve portal-facing status information.
 */
void alerts_get_portal_status(alert_portal_status_t *status_out);

/**
 * @brief Apply new configuration from the portal.
 */
bool alerts_apply_config(const alert_config_t *config);

/**
 * @brief Fetch current configuration.
 */
void alerts_get_config(alert_config_t *config_out);

#ifdef __cplusplus
}
#endif
