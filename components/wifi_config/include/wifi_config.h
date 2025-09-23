/**
 * @file wifi_config.h
 * @brief WiFi Access Point for MatrixFluid Configuration
 *
 * Provides a self-hosted WiFi network for configuring the fluid level indicator
 * including display timing, brightness, and manual activation controls.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "fluid_sensors.h"
#include "demo_mode.h"
#include "alerts.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi AP Configuration
#define WIFI_AP_SSID            "MatrixFluid-Config"
#define WIFI_AP_PASSWORD        "fluid123"
#define WIFI_AP_CHANNEL         1
#define WIFI_AP_MAX_CONNECTIONS 4

// WiFi STA Configuration Limits
#define WIFI_STA_MAX_SSID_LEN    32
#define WIFI_STA_MAX_PASS_LEN    64

// HTTP Server Configuration
#define HTTP_SERVER_PORT        80
#define MAX_HTTP_RESPONSE_SIZE  2048
#define MAX_HTTP_REQUEST_SIZE   1024

/**
 * @brief Display configuration structure
 */
typedef struct {
    bool periodic_display_enabled;      ///< Enable periodic display
    uint32_t display_interval_seconds;  ///< Interval between displays (seconds)
    uint32_t display_duration_seconds;  ///< How long to show display (seconds)
    uint8_t display_brightness;         ///< Display brightness (0-5)
    bool manual_trigger_enabled;        ///< Allow manual web trigger
    bool auto_brightness;               ///< Adjust brightness based on time (reserved)
} display_config_t;

/**
 * @brief WiFi configuration initialization parameters
 */
typedef struct {
    bool enable_ap;                     ///< Enable WiFi AP
    bool enable_web_server;             ///< Enable HTTP server
    display_config_t default_display;   ///< Default display settings
} wifi_config_init_t;

/**
 * @brief Portal status snapshot for web responses
 */
typedef struct {
    fluid_level_t fluid_level;          ///< Latest fluid level
    fluid_level_t displayed_fluid_level;///< Level currently shown on matrix
    bool half_sensor_submerged;         ///< Half sensor indicates liquid
    bool empty_sensor_submerged;        ///< Empty sensor indicates liquid
    bool half_sensor_signal_high;       ///< Half sensor GPIO driven HIGH (~3V)
    bool empty_sensor_signal_high;      ///< Empty sensor GPIO driven HIGH (~3V)
    bool display_active;                ///< Display currently illuminated
    uint32_t uptime_seconds;            ///< Device uptime in seconds
    uint32_t total_display_count;       ///< Total displays shown
    uint32_t manual_trigger_count;      ///< Manual trigger count
    uint32_t periodic_trigger_count;    ///< Periodic trigger count
    uint32_t next_wake_seconds;         ///< Estimated seconds until next scheduled wake
    display_config_t display_config;    ///< Current display configuration snapshot
    bool demo_mode_active;              ///< Demo mode currently active
    bool demo_mode_requested;           ///< Demo mode requested via portal
    bool auto_demo_requested;           ///< Auto detection flagged demo mode
    power_source_t power_source;        ///< Detected power source
    bool ota_in_progress;               ///< OTA upload currently running
    uint32_t ota_bytes_written;         ///< OTA bytes written so far
    uint32_t ota_total_size;            ///< OTA expected size (bytes)
    bool ota_pending_reboot;            ///< OTA completed and reboot scheduled
    esp_err_t ota_last_error;           ///< Last OTA error code
    alert_portal_status_t alerts;       ///< Alert scheduler snapshot
    bool sta_enabled;                   ///< STA connection requested
    bool sta_has_credentials;           ///< Stored credentials available
    bool sta_connecting;                ///< STA attempting to connect
    bool sta_connected;                 ///< STA currently connected
    char sta_ssid[WIFI_STA_MAX_SSID_LEN + 1]; ///< Stored STA SSID (if any)
    char sta_ip[16];                    ///< STA IPv4 string
    int sta_last_disconnect_reason;     ///< Last disconnect reason code
    char sta_last_error[64];            ///< Last STA error message
} wifi_portal_status_t;

typedef struct {
    bool enabled;                       ///< STA connection requested
    bool has_credentials;               ///< Credentials saved in NVS
    bool connecting;                    ///< STA currently trying to connect
    bool connected;                     ///< STA has active connection
    char ssid[WIFI_STA_MAX_SSID_LEN + 1]; ///< Saved SSID (if any)
    char ip[16];                        ///< Current STA IPv4 (if connected)
    int last_disconnect_reason;         ///< Last disconnect reason code
    char last_error[64];                ///< Last recorded error string
} wifi_sta_status_t;

/**
 * @brief Update portal status snapshot
 *
 * @param status Latest status values (NULL ignored)
 */
void wifi_config_update_status(const wifi_portal_status_t *status);

/**
 * @brief Initialize WiFi Access Point and web server
 *
 * @param config Initialization configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_init(const wifi_config_init_t *config);

/**
 * @brief Start WiFi Access Point
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_start_ap(void);

/**
 * @brief Stop WiFi Access Point
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_stop_ap(void);

/**
 * @brief Start HTTP web server
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_start_server(void);

/**
 * @brief Stop HTTP web server
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_stop_server(void);

/**
 * @brief Get current display configuration
 *
 * @param config Pointer to store current configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_get_display_config(display_config_t *config);

/**
 * @brief Update display configuration
 *
 * @param config New display configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_set_display_config(const display_config_t *config);

/**
 * @brief Trigger manual display activation via web interface
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_trigger_display(void);

/**
 * @brief Get WiFi AP status information
 *
 * @param connected_clients Number of connected clients
 * @param ap_ip IP address of the access point
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_get_status(uint8_t *connected_clients, char *ap_ip);

/**
 * @brief Get current STA connection status
 */
esp_err_t wifi_config_get_sta_status(wifi_sta_status_t *status_out);

/**
 * @brief Print WiFi configuration info
 *
 * Prints AP SSID, IP, and web interface URL
 */
void wifi_config_print_info(void);

/**
 * @brief Check if manual display trigger was requested
 *
 * @return true if display should be triggered
 */
bool wifi_config_should_trigger_display(void);

/**
 * @brief Clear manual trigger flag
 */
void wifi_config_clear_trigger_flag(void);

/**
 * @brief Check if manual trigger is enabled via configuration
 */
bool wifi_config_manual_trigger_enabled(void);

bool wifi_config_demo_mode_requested(void);
void wifi_config_set_demo_mode_requested(bool enable);

/**
 * @brief Deinitialize WiFi configuration
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_config_deinit(void);

#ifdef __cplusplus
}
#endif
