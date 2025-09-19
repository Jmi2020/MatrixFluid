#ifndef MODELS_H
#define MODELS_H

#include <Arduino.h>
#include "config.h"

// =============================================================================
// Core Data Models
// Structures and enumerations for system state management
// =============================================================================

// Forward declarations from other modules
enum class FluidLevel : uint8_t;
enum class DisplayState : uint8_t;
struct TapEvent;

// =============================================================================
// System Configuration Model
// =============================================================================

struct SystemConfig {
    // Display settings
    uint8_t led_brightness;           // 0-40 (hardware safety limit)
    uint16_t display_timeout_ms;      // Auto-off delay
    uint8_t blink_rate_hz;            // Error blink frequency
    
    // Tap detection settings
    float tap_threshold_g;            // Minimum acceleration
    uint16_t tap_window_ms;           // Max time between taps
    uint16_t tap_lockout_ms;          // Post-detection lockout
    
    // Sensor settings
    uint16_t sensor_debounce_ms;      // Stabilization time
    uint8_t sensor_error_threshold;   // Max consecutive errors
    uint16_t sensor_read_interval_ms; // Reading frequency
    
    // Power management
    uint32_t idle_timeout_ms;         // Sleep after inactivity
    bool low_power_mode;              // Enable power optimizations
    
    // Wireless settings (if enabled)
    bool wifi_enabled;                // Wi-Fi AP enable
    bool ble_enabled;                 // BLE advertising enable
    uint16_t wifi_timeout_ms;         // Wi-Fi auto-off timeout
    uint16_t ble_interval_ms;         // BLE advertising interval
    
    // Debug and diagnostics
    bool debug_enabled;               // Serial debug output
    uint8_t debug_level;              // Verbosity level (0-3)
    bool stats_logging;               // Performance statistics
    
    // Validation and defaults
    SystemConfig() {
        setDefaults();
    }
    
    void setDefaults() {
        led_brightness = LED_BRIGHTNESS_DEFAULT;
        display_timeout_ms = DISPLAY_TIMEOUT_MS;
        blink_rate_hz = 2;  // 2 Hz for error blinking
        
        tap_threshold_g = TAP_THRESHOLD_G;
        tap_window_ms = TAP_WINDOW_MS;
        tap_lockout_ms = TAP_LOCKOUT_MS;
        
        sensor_debounce_ms = SENSOR_DEBOUNCE_MS;
        sensor_error_threshold = SENSOR_ERROR_THRESHOLD;
        sensor_read_interval_ms = SENSOR_READ_INTERVAL_MS;
        
        idle_timeout_ms = IDLE_SLEEP_TIMEOUT_MS;
        low_power_mode = true;
        
        wifi_enabled = ENABLE_WIFI;
        ble_enabled = ENABLE_BLE;
        wifi_timeout_ms = 300000;  // 5 minutes
        ble_interval_ms = BLE_ADV_INTERVAL_MS;
        
        debug_enabled = DEBUG_ENABLED;
        debug_level = 1;
        stats_logging = true;
    }
    
    bool validate() const {
        return (led_brightness <= LED_BRIGHTNESS_MAX) &&
               (display_timeout_ms >= DISPLAY_MIN_TIMEOUT_MS) &&
               (tap_threshold_g > 0.0f && tap_threshold_g < 5.0f) &&
               (sensor_debounce_ms >= 50 && sensor_debounce_ms <= 1000) &&
               (debug_level <= 3);
    }
    
    void enforceLimits() {
        led_brightness = min(led_brightness, LED_BRIGHTNESS_MAX);
        display_timeout_ms = max(display_timeout_ms, DISPLAY_MIN_TIMEOUT_MS);
        tap_threshold_g = constrain(tap_threshold_g, 0.5f, 3.0f);
        sensor_debounce_ms = constrain(sensor_debounce_ms, 50, 1000);
        debug_level = min(debug_level, (uint8_t)3);
    }
};

// =============================================================================
// System State Model
// =============================================================================

struct SystemState {
    // Current status
    FluidLevel fluid_level;           // Current fluid level
    DisplayState display_state;       // Current display status
    bool display_active;              // Display currently on
    bool tap_detection_armed;         // Ready to detect taps
    
    // Timing information
    uint32_t uptime_ms;               // System uptime
    uint32_t last_activation_ms;      // Last display activation
    uint32_t last_level_change_ms;    // Last fluid level change
    uint32_t last_tap_ms;             // Last tap detection
    
    // Usage statistics
    uint32_t total_activations;       // Lifetime activation count
    uint32_t total_taps;              // Total taps detected
    uint32_t total_errors;            // Sensor/system errors
    uint32_t total_runtime_ms;        // Cumulative on-time
    
    // Sensor status
    bool half_sensor_status;          // Half-level sensor state
    bool empty_sensor_status;         // Empty-level sensor state
    bool accelerometer_status;        // IMU status
    bool sensors_valid;               // All sensors operational
    uint8_t consecutive_errors;       // Error count
    
    // System health
    float temperature_c;              // Internal temperature
    float supply_voltage_v;           // Power supply voltage
    uint32_t free_heap_bytes;         // Available memory
    uint8_t cpu_usage_percent;        // Approximate CPU utilization
    
    // Wireless status (if enabled)
    bool wifi_connected;              // Wi-Fi status
    bool ble_advertising;             // BLE status
    uint8_t wifi_clients;             // Connected clients
    int8_t wifi_rssi;                 // Signal strength
    
    // Constructor with defaults
    SystemState() {
        reset();
    }
    
    void reset() {
        fluid_level = static_cast<FluidLevel>(3); // SENSOR_ERROR initially
        display_state = static_cast<DisplayState>(0); // OFF
        display_active = false;
        tap_detection_armed = true;
        
        uptime_ms = 0;
        last_activation_ms = 0;
        last_level_change_ms = 0;
        last_tap_ms = 0;
        
        total_activations = 0;
        total_taps = 0;
        total_errors = 0;
        total_runtime_ms = 0;
        
        half_sensor_status = false;
        empty_sensor_status = false;
        accelerometer_status = false;
        sensors_valid = false;
        consecutive_errors = 0;
        
        temperature_c = 25.0f;
        supply_voltage_v = 5.0f;
        free_heap_bytes = 100000;
        cpu_usage_percent = 0;
        
        wifi_connected = false;
        ble_advertising = false;
        wifi_clients = 0;
        wifi_rssi = -100;
    }
    
    void updateUptime() {
        uptime_ms = millis();
    }
    
    bool isHealthy() const {
        return sensors_valid && 
               consecutive_errors < 3 &&
               free_heap_bytes > 10000 &&
               temperature_c < 70.0f &&
               supply_voltage_v > 4.5f;
    }
};

// =============================================================================
// Performance Metrics Model
// =============================================================================

struct PerformanceMetrics {
    // Timing metrics
    uint32_t loop_time_us;            // Main loop execution time
    uint32_t max_loop_time_us;        // Maximum loop time
    uint32_t avg_loop_time_us;        // Average loop time
    uint32_t loop_count;              // Total loop iterations
    
    // Tap detection metrics
    uint32_t tap_detections;          // Successful triple-taps
    uint32_t false_positives;         // Incorrect detections
    uint32_t vibration_rejects;       // Vibration filter hits
    float tap_success_rate;           // Detection accuracy
    uint32_t avg_tap_sequence_ms;     // Average sequence time
    
    // Sensor metrics
    uint32_t sensor_reads;            // Total sensor readings
    uint32_t sensor_errors;           // Read failures
    uint32_t level_changes;           // Fluid level transitions
    float sensor_stability;           // Reading consistency (0-1)
    
    // Display metrics
    uint32_t display_activations;     // Total activations
    uint32_t display_timeouts;        // Auto-shutoffs
    uint32_t total_display_time_ms;   // Cumulative on-time
    uint8_t avg_brightness;           // Average brightness used
    
    // Memory metrics
    uint32_t min_free_heap;           // Lowest free memory
    uint32_t heap_fragmentation;      // Memory fragmentation
    uint32_t stack_high_water;        // Maximum stack usage
    
    // Power metrics (if monitoring available)
    float avg_current_ma;             // Average current draw
    float peak_current_ma;            // Peak current
    uint32_t battery_cycles;          // Power cycles
    
    // Constructor
    PerformanceMetrics() {
        reset();
    }
    
    void reset() {
        loop_time_us = 0;
        max_loop_time_us = 0;
        avg_loop_time_us = 0;
        loop_count = 0;
        
        tap_detections = 0;
        false_positives = 0;
        vibration_rejects = 0;
        tap_success_rate = 0.0f;
        avg_tap_sequence_ms = 0;
        
        sensor_reads = 0;
        sensor_errors = 0;
        level_changes = 0;
        sensor_stability = 1.0f;
        
        display_activations = 0;
        display_timeouts = 0;
        total_display_time_ms = 0;
        avg_brightness = LED_BRIGHTNESS_DEFAULT;
        
        min_free_heap = 999999;
        heap_fragmentation = 0;
        stack_high_water = 0;
        
        avg_current_ma = 0.0f;
        peak_current_ma = 0.0f;
        battery_cycles = 0;
    }
    
    void updateLoopTiming(uint32_t execution_time_us) {
        loop_time_us = execution_time_us;
        loop_count++;
        
        if (execution_time_us > max_loop_time_us) {
            max_loop_time_us = execution_time_us;
        }
        
        // Running average
        avg_loop_time_us = ((avg_loop_time_us * (loop_count - 1)) + execution_time_us) / loop_count;
    }
    
    float getLoopFrequency() const {
        return avg_loop_time_us > 0 ? 1000000.0f / avg_loop_time_us : 0.0f;
    }
    
    bool isPerformanceHealthy() const {
        return avg_loop_time_us < 10000 &&      // <10ms average loop time
               max_loop_time_us < 50000 &&      // <50ms max loop time
               min_free_heap > 5000 &&          // >5KB free memory
               tap_success_rate > 0.8f;         // >80% tap detection success
    }
};

// =============================================================================
// Error Information Model
// =============================================================================

enum class ErrorType : uint8_t {
    NO_ERROR = 0,
    SENSOR_READ_FAILED = 1,
    SENSOR_DISCONNECTED = 2,
    SENSOR_INVALID_STATE = 3,
    ACCELEROMETER_FAILED = 4,
    DISPLAY_FAILED = 5,
    MEMORY_LOW = 6,
    POWER_VOLTAGE_LOW = 7,
    TEMPERATURE_HIGH = 8,
    CONFIGURATION_INVALID = 9,
    WIFI_FAILED = 10,
    BLE_FAILED = 11,
    SYSTEM_WATCHDOG = 12,
    UNKNOWN_ERROR = 255
};

struct ErrorInfo {
    ErrorType type;                   // Error classification
    uint32_t timestamp_ms;            // When error occurred
    uint8_t severity;                 // 0=Info, 1=Warning, 2=Error, 3=Critical
    uint16_t error_code;              // Specific error code
    String description;               // Human-readable description
    uint32_t count;                   // How many times this error occurred
    bool is_active;                   // Currently affecting system
    bool requires_restart;            // Needs system restart to clear
    
    ErrorInfo() : type(ErrorType::NO_ERROR), timestamp_ms(0), severity(0), 
                 error_code(0), description(""), count(0), is_active(false), requires_restart(false) {}
    
    ErrorInfo(ErrorType err_type, uint8_t sev, const String& desc) :
        type(err_type), timestamp_ms(millis()), severity(sev), error_code(0),
        description(desc), count(1), is_active(true), requires_restart(false) {}
    
    String getSeverityString() const {
        switch (severity) {
            case 0: return "INFO";
            case 1: return "WARNING";
            case 2: return "ERROR";
            case 3: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
    
    bool isCritical() const {
        return severity >= 2;
    }
};

// =============================================================================
// JSON Serialization Support
// =============================================================================

#if ENABLE_WIFI
class JSONSerializer {
public:
    static String serializeSystemState(const SystemState& state);
    static String serializeSystemConfig(const SystemConfig& config);
    static String serializePerformanceMetrics(const PerformanceMetrics& metrics);
    static String serializeErrorInfo(const ErrorInfo& error);
    
    static bool deserializeSystemConfig(const String& json, SystemConfig& config);

private:
    static String floatToString(float value, uint8_t decimals = 2);
    static String boolToString(bool value);
    static void addJsonField(String& json, const String& key, const String& value, bool isLast = false);
};
#endif

// =============================================================================
// EEPROM Persistence Support
// =============================================================================

class ConfigPersistence {
public:
    static bool saveConfig(const SystemConfig& config);
    static bool loadConfig(SystemConfig& config);
    static bool saveStatistics(const PerformanceMetrics& metrics);
    static bool loadStatistics(PerformanceMetrics& metrics);
    static void factoryReset();
    
private:
    static const uint16_t CONFIG_MAGIC = 0xABCD;
    static const uint8_t CONFIG_VERSION = 1;
    static const uint16_t CONFIG_BASE_ADDR = 0x00;
    static const uint16_t STATS_BASE_ADDR = 0x40;
    
    static uint8_t calculateChecksum(const uint8_t* data, uint16_t length);
    static bool writeEEPROM(uint16_t address, const uint8_t* data, uint16_t length);
    static bool readEEPROM(uint16_t address, uint8_t* data, uint16_t length);
};

// =============================================================================
// Utility Functions
// =============================================================================

namespace ModelUtils {
    String fluidLevelToString(FluidLevel level);
    String displayStateToString(DisplayState state);
    String errorTypeToString(ErrorType type);
    
    bool isValidFluidLevel(uint8_t level);
    bool isValidDisplayState(uint8_t state);
    
    uint32_t getUptimeSeconds();
    String formatUptime(uint32_t uptime_ms);
    String formatMemorySize(uint32_t bytes);
    String formatPercentage(float value);
}

#endif // MODELS_H
