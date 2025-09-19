# MatrixFluid - Vehicle Fluid Level Indicator

A safety-critical embedded device for monitoring vehicle fluid levels using the Waveshare ESP32-S3-Matrix board with 8×8 RGB LED display and gesture activation.

## ⚠️ SAFETY WARNINGS

**CRITICAL SAFETY REQUIREMENTS:**
- LED brightness is hardcoded to maximum 40/255 (~15%) to prevent overheating
- Device must be mounted securely to prevent vibration damage
- Use fused power connection (1A recommended) 
- Connect to switched 12V power (ignition-controlled)
- Do not operate while driving

**Hardware Limits:**
- Never exceed 40/255 LED brightness setting
- Operating temperature: -20°C to +85°C
- Maximum continuous operation: Limited by thermal design
- Power consumption: <150mA active, <2mA sleep

## Features

- **Triple-tap activation** - Requires deliberate gesture to prevent accidental activation
- **Visual status indication** - Green checkmark, yellow caution, red stop, error X
- **Vibration filtering** - Distinguishes intentional taps from road vibrations  
- **Auto-shutoff** - 7-second timeout for power conservation
- **Error handling** - Defaults to caution state on sensor failures
- **Optional connectivity** - Wi-Fi AP and BLE broadcasting (compile-time flags)

## Hardware Requirements

- Waveshare ESP32-S3-Matrix board (8×8 LED + QMI8658 IMU)
- 2× Fluid level sensors (float switches, normally open)
- 12V to 5V buck converter for vehicle power
- Mounting hardware and wiring

## Quick Start

1. **Hardware Setup**
   ```
   GPIO2 ← Half-full sensor (NO switch to GND)
   GPIO3 ← Near-empty sensor (NO switch to GND)
   5V ← Buck converter output
   GND ← Common ground
   ```

2. **Software Installation**
   ```bash
   git clone https://github.com/yourusername/MatrixFluid.git
   cd MatrixFluid
   pio run --target upload
   pio device monitor
   ```

3. **Operation**
   - Triple-tap the device to check fluid level
   - Observe the displayed icon (green/yellow/red/error)
   - Display automatically turns off after 7 seconds

## Status Indicators

| Icon | Color | Meaning | Sensor State |
|------|-------|---------|--------------|
| ✓ | Green | Good level (above half) | Both sensors HIGH |
| ⚠ | Yellow | Caution (below half) | Half LOW, Empty HIGH |
| ⬢ | Red | Low (near empty) | Both sensors LOW |
| ✗ | Red (blinking) | Sensor error | Invalid combination |

## Configuration

Edit `src/config.h` for customization:

```cpp
#define LED_BRIGHTNESS_DEFAULT 30   // Never exceed 40!
#define DISPLAY_TIMEOUT_MS 7000     // Auto-off delay
#define TAP_THRESHOLD_G 1.5         // Tap sensitivity
#define ENABLE_WIFI false           // Optional features
#define ENABLE_BLE false
```

## Optional Features

### Wi-Fi Access Point
When enabled, provides HTTP API at http://192.168.4.1:
- `/status` - JSON status data
- `/config` - Configuration management
- `/test` - Manual display testing
- `/stream` - WebSocket real-time updates

### BLE Advertisement
When enabled, broadcasts status data:
- Device name: "TankMon"
- Manufacturer data includes fluid level, battery, temperature
- No pairing required - scan and read

## Development

### Build System
- **PlatformIO** with ESP32-S3 Arduino framework
- **C++17** standard with embedded optimizations
- **FastLED** library for WS2812B matrix control

### Testing
Hardware-in-the-loop testing with:
- Sensor simulation using jumper wires
- Tap simulation with controlled acceleration
- Power consumption measurement
- Thermal testing for LED safety

### Architecture
```
src/
├── main.cpp                 # Main entry point
├── config.h                 # System configuration
├── sensors/                 # Hardware abstraction
│   ├── fluid_sensors.cpp    # GPIO sensor reading
│   └── accelerometer.cpp    # QMI8658 IMU driver
├── display/                 # LED matrix control
│   ├── led_controller.cpp   # FastLED wrapper
│   └── icons.h              # 8x8 pixel patterns
├── detection/               # Gesture recognition
│   └── tap_detector.cpp     # Triple-tap state machine
└── wireless/                # Optional connectivity
    ├── wifi_server.cpp      # HTTP API server
    └── ble_beacon.cpp       # Advertisement broadcaster
```

## Troubleshooting

**No display on triple-tap:**
- Check serial monitor for tap detection messages
- Verify accelerometer initialization
- Adjust TAP_THRESHOLD_G if needed

**Wrong fluid level shown:**
- Verify sensor wiring with multimeter
- Check sensor mounting positions
- Review sensor logic table in debug output

**Display too bright/dim:**
- Modify LED_BRIGHTNESS_DEFAULT in config.h
- Never exceed safety limit of 40/255
- Recompile and upload after changes

**Device resets:**
- Check power supply stability (need >500mA capacity)
- Monitor for overheating
- Review serial output for stack overflow/panic

## Safety Compliance

This device implements multiple safety measures:
- **Hardware protection:** Brightness limiting prevents LED overheating
- **Software validation:** Runtime checks enforce safety constraints  
- **Error recovery:** Graceful degradation on component failures
- **Power management:** Automatic shutoff prevents battery drain
- **Vibration immunity:** Filtering prevents false activation

## License

MIT License - See LICENSE file for details.

## Support

- **Documentation:** See `docs/` directory for detailed guides
- **Issues:** GitHub issue tracker with hardware setup details
- **Debug:** Enable serial monitor at 115200 baud for diagnostics

---
**Version:** 1.0.0 | **Platform:** ESP32-S3 | **Framework:** Arduino Core
