# Data Model: Vehicle Fluid Level Indicator

**Feature**: 001-build-a-vehicle | **Date**: 2025-01-18

## Core Enumerations

### FluidLevel
Represents the current fluid level state based on sensor readings.

```cpp
enum class FluidLevel : uint8_t {
    ABOVE_HALF = 0,   // Both sensors HIGH
    BELOW_HALF = 1,   // Half sensor LOW, Empty sensor HIGH
    NEAR_EMPTY = 2,   // Both sensors LOW
    SENSOR_ERROR = 3  // Invalid sensor combination
};
```

**Validation Rules**:
- Sensor readings must be debounced for 100ms before state change
- ERROR state triggered by impossible sensor combinations
- State transitions logged for debugging

### DisplayState
Represents the current display status of the LED matrix.

```cpp
enum class DisplayState : uint8_t {
    OFF = 0,              // Matrix powered off
    SHOWING_GREEN = 1,    // Green checkmark displayed
    SHOWING_YELLOW = 2,   // Yellow caution displayed
    SHOWING_RED = 3,      // Red stop sign displayed
    SHOWING_ERROR = 4,    // Blinking red X pattern
    SELF_TEST = 5         // Startup test pattern
};
```

**State Transitions**:
- OFF → SHOWING_* on triple-tap detection
- SHOWING_* → OFF after timeout period
- Any state → SHOWING_ERROR on critical failure
- SELF_TEST → OFF after 2 seconds (startup only)

### ConnectionState
Represents wireless connectivity status (optional feature).

```cpp
enum class ConnectionState : uint8_t {
    DISABLED = 0,     // Wireless features compile-time disabled
    IDLE = 1,         // Available but not broadcasting
    WIFI_ACTIVE = 2,  // Wi-Fi AP running
    BLE_ACTIVE = 3,   // BLE advertising
    BOTH_ACTIVE = 4   // Wi-Fi and BLE both running
};
```

## Core Structures

### TapEvent
Captures accelerometer tap detection data.

```cpp
struct TapEvent {
    uint32_t timestamp_ms;  // millis() when detected
    float magnitude_g;      // Peak acceleration in g
    uint8_t axis_mask;     // Which axes triggered (bit flags)
};
```

**Constraints**:
- magnitude_g must be >= 1.5g to register
- timestamp_ms rolls over at 49.7 days (handle wraparound)
- axis_mask: bit 0=X, bit 1=Y, bit 2=Z

### SensorReading
Raw sensor input data with timestamp.

```cpp
struct SensorReading {
    bool half_sensor;       // HIGH = fluid above half
    bool empty_sensor;      // HIGH = fluid above empty
    uint32_t timestamp_ms;  // Reading time
    bool is_valid;         // False if read error
};
```

**Validation**:
- Readings invalid if sensors disconnected
- Timestamp must be monotonic (account for rollover)
- Both sensors LOW = urgent state

### SystemConfig
Runtime configuration parameters.

```cpp
struct SystemConfig {
    uint8_t led_brightness;       // 0-40 (capped at ~15%)
    uint16_t display_timeout_ms;  // Auto-off delay (default 7000)
    float tap_threshold_g;        // Min acceleration (default 1.5)
    uint16_t tap_window_ms;      // Max time between taps (default 500)
    bool wifi_enabled;           // Compile-time flag
    bool ble_enabled;            // Compile-time flag
    uint16_t error_blink_ms;     // Error pattern period (default 500)
};
```

**Constraints**:
- led_brightness MUST NOT exceed 40 (hardware safety)
- display_timeout_ms range: 1000-30000
- tap_threshold_g range: 0.5-3.0
- tap_window_ms range: 100-1000

### SystemState
Complete system state snapshot.

```cpp
struct SystemState {
    FluidLevel fluid_level;
    DisplayState display_state;
    ConnectionState connection_state;
    uint32_t uptime_ms;
    uint32_t last_activation_ms;
    uint32_t total_activations;
    float battery_voltage;  // If monitoring power
    int8_t temperature_c;   // From internal sensor
};
```

**Persistence**:
- total_activations saved to EEPROM every 100 activations
- Other fields volatile (reset on power cycle)

### TapDetector
Internal state machine for triple-tap detection.

```cpp
struct TapDetector {
    TapEvent tap_buffer[3];     // Circular buffer
    uint8_t tap_count;          // Current count in window
    uint32_t window_start_ms;   // First tap timestamp
    bool detection_armed;       // Ready to detect
    uint32_t last_detection_ms; // Debounce timer
};
```

**Algorithm**:
1. On tap event, add to buffer
2. If 3 taps within window → trigger activation
3. Reset if window expires or activation triggered
4. Ignore taps for 1000ms post-activation (debounce)

## Data Flow

### Sensor → State
```
Physical Sensors → SensorReading → Debounce → FluidLevel
```

### Tap → Display
```
Accelerometer → TapEvent → TapDetector → Display Activation → DisplayState
```

### State → Output
```
FluidLevel + DisplayState → LED Matrix Pattern → Physical LEDs
```

### Wireless Broadcasting (Optional)
```
SystemState → JSON/BLE Format → Wi-Fi HTTP/BLE Advertisement
```

## Memory Layout

### EEPROM/Flash Storage
```
Address  | Data              | Size
---------|-------------------|------
0x00     | Magic byte (0xAF) | 1
0x01     | Config version    | 1
0x02-0x05| Total activations | 4
0x06-0x15| SystemConfig      | 16
0x16-0x1F| Reserved          | 10
```

### RAM Usage Estimates
- SystemState: 24 bytes
- TapDetector: 48 bytes
- SensorReading buffer[10]: 80 bytes
- LED frame buffer: 192 bytes (64 LEDs × 3 bytes)
- Total core: ~350 bytes

## Thread Safety

### Shared Resources
- SystemState: Protected by mutex/critical section
- LED frame buffer: Double-buffered for clean updates
- Sensor readings: Atomic operations or ISR-safe

### Task Priorities (FreeRTOS)
1. Accelerometer ISR: Highest
2. Sensor reading task: High
3. Display update task: Normal
4. Wi-Fi/BLE task: Low
5. Idle task: Lowest

## Validation Rules

### State Consistency
- FluidLevel must match sensor logic table
- DisplayState timeout must be enforced
- Connection state must reflect actual radio status

### Boundary Conditions
- Handle millis() rollover at 49.7 days
- Cap brightness even if config corrupted
- Default to safe states on any error

### Error Recovery
- Sensor error → Show caution (yellow) or error pattern
- Config corruption → Load safe defaults
- Accelerometer failure → Disable tap detection, show error
- Memory full → Stop logging, continue operation