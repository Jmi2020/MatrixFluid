#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// MatrixFluid Configuration
// Vehicle Fluid Level Indicator for ESP32-S3-Matrix
// =============================================================================

// Version Information
#define FIRMWARE_VERSION "1.0.0"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

// =============================================================================
// SAFETY-CRITICAL CONSTANTS - DO NOT MODIFY
// =============================================================================

// LED BRIGHTNESS SAFETY LIMIT - NEVER EXCEED 40/255
// Exceeding this limit may cause overheating and hardware damage
#define LED_BRIGHTNESS_MAX 40       // Hardware safety limit (~15.7% of 255)
#define LED_BRIGHTNESS_DEFAULT 30   // Conservative default (~11.8% of 255)

// Display timeout for power conservation and safety
#define DISPLAY_TIMEOUT_MS 7000     // 7 seconds max display time
#define DISPLAY_MIN_TIMEOUT_MS 1000 // 1 second minimum

// =============================================================================
// Hardware Pin Configuration
// =============================================================================

// LED Matrix (WS2812B on ESP32-S3-Matrix)
#define LED_PIN 14              // GPIO14 - Fixed on Waveshare board
#define LED_COUNT 64            // 8x8 matrix
#define LED_COLOR_ORDER GRB     // WS2812B standard

// Fluid Level Sensors (Active LOW - normally open switches)
#define HALF_SENSOR_PIN 2       // GPIO2 - Half full sensor
#define EMPTY_SENSOR_PIN 3      // GPIO3 - Near empty sensor

// Accelerometer (QMI8658 on ESP32-S3-Matrix)
#define ACCEL_I2C_SDA 8         // GPIO8 - Fixed on board
#define ACCEL_I2C_SCL 9         // GPIO9 - Fixed on board
#define ACCEL_INT_PIN 4         // GPIO4 - Interrupt pin
#define ACCEL_I2C_ADDR 0x6B     // Default QMI8658 address

// =============================================================================
// Tap Detection Parameters
// =============================================================================

// Triple-tap timing (tuned for vehicle environment)
#define TAP_THRESHOLD_G 1.5         // Minimum acceleration to register tap
#define TAP_WINDOW_MS 500           // Max time between taps (150-500ms optimal)
#define TAP_DEBOUNCE_MS 50          // Post-detection debounce
#define TAP_REQUIRED_COUNT 3        // Triple-tap requirement
#define TAP_LOCKOUT_MS 1000         // Prevent repeated activation

// Vibration filtering
#define VIBRATION_FILTER_MS 100     // Ignore taps within this period
#define MIN_TAP_INTERVAL_MS 150     // Minimum time between individual taps

// =============================================================================
// Sensor Configuration
// =============================================================================

// Sensor debouncing
#define SENSOR_DEBOUNCE_MS 100      // Stabilization time for sensor readings
#define SENSOR_READ_INTERVAL_MS 50  // How often to check sensors

// Sensor validation
#define SENSOR_ERROR_THRESHOLD 5    // Consecutive invalid reads before error
#define SENSOR_RECOVERY_DELAY_MS 1000 // Delay before retry after error

// =============================================================================
// Power Management
// =============================================================================

// Sleep configuration
#define IDLE_SLEEP_TIMEOUT_MS 30000 // Enter light sleep after 30s idle
#define DEEP_SLEEP_THRESHOLD_MS 300000 // Deep sleep after 5 minutes (unused)

// Power monitoring (if available)
#define BATTERY_ADC_PIN 1           // GPIO1 for voltage divider
#define BATTERY_LOW_VOLTAGE 11.5    // Low battery threshold (12V system)

// =============================================================================
// Wireless Features (Compile-time Optional)
// =============================================================================

#ifndef ENABLE_WIFI
#define ENABLE_WIFI false           // Set to true to enable Wi-Fi AP
#endif

#ifndef ENABLE_BLE
#define ENABLE_BLE false            // Set to true to enable BLE advertising
#endif

// Wi-Fi Configuration
#define WIFI_SSID "TankMonitor"
#define WIFI_PASSWORD ""            // Open network by default
#define WIFI_CHANNEL 1
#define WIFI_AP_IP IPAddress(192, 168, 4, 1)
#define WIFI_GATEWAY IPAddress(192, 168, 4, 1)
#define WIFI_SUBNET IPAddress(255, 255, 255, 0)
#define HTTP_PORT 80

// BLE Configuration
#define BLE_DEVICE_NAME "TankMon"
#define BLE_ADV_INTERVAL_MS 2000    // 2 second advertising interval
#define BLE_TX_POWER 0              // 0 dBm for ~10m range

// =============================================================================
// Display Patterns
// =============================================================================

// Icon brightness scaling (applied to LED_BRIGHTNESS_DEFAULT)
#define ICON_BRIGHTNESS_SCALE 1.0   // 100% of configured brightness

// Pattern timing
#define SELF_TEST_DURATION_MS 500   // Time to show each pattern on boot
#define ERROR_BLINK_PERIOD_MS 500   // Error pattern blink rate
#define FADE_TRANSITION_MS 100      // Smooth on/off transitions

// =============================================================================
// Debug and Logging
// =============================================================================

#define SERIAL_BAUD_RATE 115200
#define DEBUG_ENABLED true          // Enable serial debug output

// Debug categories (bitfield)
#define DEBUG_BOOT       (1 << 0)   // Boot and initialization
#define DEBUG_SENSORS    (1 << 1)   // Sensor readings
#define DEBUG_TAP        (1 << 2)   // Tap detection
#define DEBUG_DISPLAY    (1 << 3)   // LED display
#define DEBUG_WIRELESS   (1 << 4)   // Wi-Fi/BLE activity
#define DEBUG_POWER      (1 << 5)   // Power management

#define DEBUG_MASK (DEBUG_BOOT | DEBUG_SENSORS | DEBUG_TAP | DEBUG_DISPLAY)

// =============================================================================
// Memory and Performance
// =============================================================================

// Task stack sizes (FreeRTOS)
#define MAIN_TASK_STACK_SIZE 4096
#define SENSOR_TASK_STACK_SIZE 2048
#define DISPLAY_TASK_STACK_SIZE 2048
#define WIRELESS_TASK_STACK_SIZE 4096

// Buffer sizes
#define SENSOR_HISTORY_SIZE 10      // Number of recent readings to store
#define TAP_EVENT_BUFFER_SIZE 5     // Tap event circular buffer

// =============================================================================
// Validation Macros
// =============================================================================

// Compile-time safety checks
#if LED_BRIGHTNESS_DEFAULT > LED_BRIGHTNESS_MAX
#error "LED_BRIGHTNESS_DEFAULT exceeds safety limit!"
#endif

#if LED_BRIGHTNESS_MAX > 40
#error "LED_BRIGHTNESS_MAX exceeds hardware safety limit of 40!"
#endif

#if TAP_WINDOW_MS < MIN_TAP_INTERVAL_MS * TAP_REQUIRED_COUNT
#error "TAP_WINDOW_MS too small for required tap count!"
#endif

// Runtime validation function
inline bool validateConfig() {
    return (LED_BRIGHTNESS_DEFAULT <= LED_BRIGHTNESS_MAX) &&
           (LED_BRIGHTNESS_MAX <= 40) &&
           (DISPLAY_TIMEOUT_MS >= DISPLAY_MIN_TIMEOUT_MS) &&
           (TAP_THRESHOLD_G > 0.0f && TAP_THRESHOLD_G < 5.0f);
}

#endif // CONFIG_H
