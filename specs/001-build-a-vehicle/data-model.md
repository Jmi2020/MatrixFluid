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

**Validation Rules**
- Sensor readings must be debounced (default 100 ms) before state change.
- SENSOR_ERROR is emitted when sensors disagree or a read fails.

### DisplayState
Represents the current display status of the LED matrix.

```cpp
enum class DisplayState : uint8_t {
    OFF = 0,
    SHOWING_GREEN = 1,
    SHOWING_YELLOW = 2,
    SHOWING_RED = 3,
    SHOWING_ERROR = 4,
    SELF_TEST = 5
};
```

**State Transitions**
- OFF → SHOWING_* on scheduled wake or manual portal request.
- SHOWING_* → OFF after configured timeout.
- Any state → SHOWING_ERROR if a safety fault occurs.
- SELF_TEST → OFF after startup diagnostics complete.

### ConnectionState
Represents Wi-Fi portal availability.

```cpp
enum class ConnectionState : uint8_t {
    DISABLED = 0,
    AP_IDLE = 1,
    AP_ACTIVE = 2,
    CLIENT_CONNECTED = 3
};
```

## Core Structures

### SensorReading
Raw sensor input data with timestamp.

```cpp
struct SensorReading {
    bool half_sensor;       // HIGH = fluid above half
    bool empty_sensor;      // HIGH = fluid above near-empty
    uint32_t timestamp_ms;  // Reading timestamp
    bool is_valid;          // False if read error
};
```

**Validation**
- Readings marked invalid when GPIO read fails or sensor open-circuit detected.
- Timestamp wraps at 49.7 days (handle rollover in comparisons).

### ScheduleConfig
Represents timing parameters for automatic wake cycles.

```cpp
struct ScheduleConfig {
    uint32_t interval_ms;       // Period between automatic updates (default TBD)
    uint32_t display_timeout_ms;// How long LEDs stay on (default 7000)
    uint32_t startup_delay_ms;  // Delay before first cycle after boot
};
```

**Constraints**
- interval_ms minimum 60000 ms (1 minute) to avoid nuisance lighting [TBD].
- display_timeout_ms must be between 1000 ms and 10000 ms.
- startup_delay_ms defaults to the interval unless otherwise configured.

### SystemConfig
Runtime configuration parameters stored in flash.

```cpp
struct SystemConfig {
    uint8_t led_brightness;        // 0-5 (capped for safety)
    ScheduleConfig schedule;       // Timing parameters
    bool wifi_enabled;             // Enable/disable AP portal
    uint16_t error_blink_ms;       // Error pattern period (default 500)
};
```

**Constraints**
- led_brightness MUST NOT exceed 5.
- wifi_enabled controls portal availability but does not disable scheduling.

### PortalRequest
Tracks manual refresh requests from Wi-Fi clients.

```cpp
struct PortalRequest {
    bool pending;              // True when user requested refresh
    uint32_t requested_ms;     // Timestamp of request
    char client_ip[16];        // IPv4 text for logging
};
```

**Notes**
- pending cleared after request serviced or timeout reached.
- requested_ms used to avoid duplicate triggers within debounce window (default 5 s).

### SystemState
Complete system state snapshot for diagnostics.

```cpp
struct SystemState {
    FluidLevel fluid_level;
    DisplayState display_state;
    ConnectionState connection_state;
    uint32_t uptime_ms;
    uint32_t last_update_ms;
    uint32_t next_wake_ms;
    uint32_t total_updates;
    PortalRequest portal;
    float battery_voltage;     // Optional power monitoring
    int8_t temperature_c;      // Optional thermal monitoring
};
```

**Persistence**
- total_updates can be periodically stored to NVS/flash for field diagnostics.
- Portal request log may be persisted if telemetry is required.

## Data Flow

### Sensor → State
```
Fluid sensors → SensorReading → Debounce/validation → FluidLevel → SystemState
```

### Scheduler → Display
```
ScheduleConfig + SystemState → Scheduler task → Display controller → DisplayState → LED matrix
```

### Portal Request → Display
```
HTTP POST /refresh → PortalRequest.pending → Scheduler immediate cycle → DisplayState
```

### State → Wi-Fi Portal
```
SystemState → JSON/render template → HTTP response to client
```

## Memory Layout (Draft)

### NVS/Flash Storage Suggestions
```
Key           | Data              | Notes
--------------|-------------------|------------------------------
config/led    | uint8_t           | brightness (0-5)
config/sched  | ScheduleConfig    | interval + timeout
stats/updates | uint32_t          | total update count
```

### RAM Usage Estimates
- SystemState: ~48 bytes
- Sensor history buffer[10]: ~80 bytes
- LED frame buffer: 192 bytes (64 LEDs × 3 bytes)
- Wi-Fi portal scratch: configurable (1-2 KB)

## Thread Safety

### Shared Resources
- SystemState protected by mutex or critical section when accessed from scheduler and portal tasks.
- LED frame buffer updated via double-buffer pattern before pushing to RMT driver.
- PortalRequest updated from HTTP handler; scheduler must clear using atomic or guarded operations.

### Timing Guarantees
- Scheduler task should run at low priority but with deterministic wake using esp_timer or FreeRTOS timer.
- Manual portal refresh should enqueue request and return quickly to avoid blocking HTTP handler.

