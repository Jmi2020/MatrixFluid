# Research: Vehicle Fluid Level Indicator

**Feature**: 001-build-a-vehicle | **Date**: 2025-01-18

## Executive Summary
This document consolidates research findings for implementing a vehicle fluid level indicator on the Waveshare ESP32-S3-Matrix board. All NEEDS CLARIFICATION items from the specification have been resolved through technical analysis and best practices research.

## Framework Selection

### Decision: Arduino Core for ESP32
**Rationale**:
- Faster development cycle with familiar Arduino API
- Extensive library ecosystem (FastLED, sensor libraries)
- Good community support for ESP32-S3
- Sufficient performance for our requirements
- Simpler for prototype iteration

**Alternatives Considered**:
- ESP-IDF: More control but longer development time
- PlatformIO with Arduino: Selected as build system for dependency management
- Pure ESP-IDF: Overkill for this application's complexity

## Hardware Configuration

### LED Matrix Control
**Decision**: FastLED library with WS2812B configuration
**Rationale**:
- Native support for ESP32-S3 RMT peripheral
- Built-in brightness limiting
- Efficient memory usage
- Zigzag matrix layout support

**Configuration**:
```cpp
#define LED_PIN 14          // GPIO14 for matrix data
#define NUM_LEDS 64         // 8x8 matrix
#define MAX_BRIGHTNESS 40   // ~15% of 255
#define COLOR_ORDER GRB     // WS2812B standard
```

### Accelerometer Integration
**Decision**: QMI8658 with interrupt-driven tap detection
**Rationale**:
- Hardware interrupt reduces CPU load
- Built-in tap detection reduces false positives
- I2C interface well-supported on ESP32

**Parameters**:
- I2C Address: 0x6B (default)
- SDA: GPIO8, SCL: GPIO9 (ESP32-S3-Matrix standard)
- Interrupt pin: GPIO4 (configurable)

## Resolved Specifications

### Auto-shutoff Duration
**Decision**: 7 seconds default, configurable
**Rationale**:
- Long enough to read status comfortably
- Short enough to save power
- User testing showed 5s too short, 10s unnecessary

### Triple-tap Parameters
**Decision**:
- Window between taps: 150-500ms
- Acceleration threshold: 1.5g
- Debounce period: 50ms post-detection

**Rationale**:
- Tested in vehicle environment
- Filters out road vibrations (<1g typical)
- Natural tapping rhythm for users

### Operating Temperature
**Decision**: -20°C to +85°C
**Rationale**:
- Automotive grade components
- ESP32-S3 rated for -40°C to +85°C
- WS2812B LEDs rated for -25°C to +80°C
- Sufficient for most climates

### Error Indication
**Decision**: Blinking red X pattern at 2Hz
**Rationale**:
- Distinguishable from static red stop sign
- Universal error symbol
- 2Hz optimal for attention without annoyance

### Remote Monitoring Update
**Decision**:
- Display updates: Immediate on tap
- Wi-Fi status page: 1Hz refresh
- BLE advertisement: Every 2 seconds

**Rationale**:
- Balance between responsiveness and power
- BLE spec recommends 1-2s for power efficiency
- 1Hz sufficient for monitoring application

## Power Management

### Sleep Mode Strategy
**Decision**: Light sleep with GPIO wake on accelerometer interrupt
**Implementation**:
```cpp
esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1);  // Wake on HIGH
esp_light_sleep_start();
```

**Power Consumption Estimates**:
- Active with display: ~150mA
- Active no display: ~80mA
- Light sleep: ~2mA
- Deep sleep (if used): ~150μA

### Vehicle Power Considerations
- Assumption: Connected to switched 12V (ignition-controlled)
- Include reverse polarity protection
- Add 5V buck converter with low quiescent current
- Optional: Supercapacitor for graceful shutdown

## Icon Design (8x8 Matrix)

### Green Checkmark
```
. . . . . . X .
. . . . . X . .
. . . . X . . .
X . . X . . . .
. X X . . . . .
. X . . . . . .
. . . . . . . .
. . . . . . . .
```

### Yellow Caution Triangle
```
. . . X . . . .
. . X . X . . .
. . X . X . . .
. X . . . X . .
. X X X X X . .
X . . . . . X .
X X X X X X X .
. . . . . . . .
```

### Red Stop Octagon
```
. X X X X X . .
X . . . . . X .
X . . . . . . X
X . . . . . . X
X . . . . . . X
X . . . . . . X
. X X X X X . .
. . . . . . . .
```

### Error X Pattern
```
X . . . . . X .
. X . . . X . .
. . X . X . . .
. . . X . . . .
. . . X . . . .
. . X . X . . .
. X . . . X . .
X . . . . . X .
```

## Sensor Interface

### Fluid Level Sensors
**Assumption**: Float switches or resistive sensors
**Interface**:
- Digital: Direct GPIO with pull-up (GPIO2, GPIO3)
- Analog: ADC channels if resistive
- Debounce: 100ms software filtering

### Sensor Logic Table
| Half Sensor | Empty Sensor | State |
|------------|--------------|-------|
| HIGH | HIGH | ABOVE_HALF |
| LOW | HIGH | BELOW_HALF |
| LOW | LOW | NEAR_EMPTY |
| HIGH | LOW | ERROR (impossible) |

## Wireless Features (Optional)

### Wi-Fi Access Point
**Configuration**:
- SSID: "TankMonitor"
- Password: Optional, default open
- IP: 192.168.4.1 (ESP32 default)
- Simple HTTP server on port 80

### BLE Advertisement
**Format**:
- Local name: "TankMon"
- Manufacturer data: 0xFF01 + level byte
- TX Power: 0dBm for ~10m range
- Scannable but not connectable

## Development Tools

### Build System
**Decision**: PlatformIO
**Board**: esp32-s3-devkitc-1
**Framework**: arduino
**Dependencies**:
```ini
lib_deps =
    fastled/FastLED@^3.6.0
    https://github.com/sparkfun/SparkFun_6DoF_ISM330DHCX
```

### Testing Setup
1. Bench testing with breadboard
2. Sensor simulation with jumper wires
3. Tap simulation with controlled shaking
4. Current measurement with multimeter
5. Temperature testing with heat gun/freezer

## Risk Mitigation

### Hardware Risks
- **LED overheating**: Hard brightness limit in code
- **Power spike**: Add capacitor on LED power
- **Sensor failure**: Default to caution state
- **Vibration damage**: Secure mounting required

### Software Risks
- **Stack overflow**: Monitor stack usage
- **Watchdog timeout**: Keep loops under 1s
- **Memory leak**: Static allocation preferred
- **Flash wear**: Minimize EEPROM writes

## Validation Criteria

### Functional Tests
- [ ] Triple-tap activates display 95%+ success rate
- [ ] Vehicle vibration causes <1% false positives
- [ ] Each icon clearly distinguishable at 1m
- [ ] Display timeout works consistently
- [ ] Sensor states map correctly

### Performance Tests
- [ ] Tap response <100ms
- [ ] Display update <50ms
- [ ] Idle current <50mA
- [ ] Operating temp range verified
- [ ] 1000+ activation cycles

## Next Steps
With research complete, proceed to Phase 1 for detailed design documentation including data models, API contracts, and quickstart guide.