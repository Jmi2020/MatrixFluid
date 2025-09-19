# Quick Start Guide: Vehicle Fluid Level Indicator

**Feature**: 001-build-a-vehicle | **Version**: 1.0.0

## Prerequisites

### Hardware Required
- Waveshare ESP32-S3-Matrix board
- 2× Fluid level sensors (float switches or similar)
- 12V to 5V buck converter (for vehicle power)
- Connecting wires and terminals
- Mounting hardware (velcro/brackets)

### Software Required
- PlatformIO (VS Code extension recommended)
- USB-C cable for programming
- Serial terminal (built into PlatformIO)

## Hardware Setup

### Wiring Diagram

```
ESP32-S3-Matrix Board
┌─────────────────────┐
│                     │
│  [USB-C Port]       │
│                     │
│  GPIO2 ←── Half Sensor (NO contact to GND)
│  GPIO3 ←── Empty Sensor (NO contact to GND)
│  GND   ←── Sensor Common
│                     │
│  5V    ←── Buck Converter Output (+5V)
│  GND   ←── Buck Converter Ground
│                     │
│  [8×8 LED Matrix]   │
│  (Pre-connected)    │
│                     │
│  [QMI8658 IMU]      │
│  (Pre-connected)    │
│                     │
└─────────────────────┘

Vehicle Power
┌─────────────────────┐
│  12V+ (Switched)    │──→ Buck Converter Input+
│  Ground             │──→ Buck Converter Input-
└─────────────────────┘
```

### Sensor Connection
1. Connect half-full sensor between GPIO2 and GND
2. Connect near-empty sensor between GPIO3 and GND
3. Sensors should be normally open (NO) switches
4. When fluid present, switch closes (pulls GPIO to GND)

## Software Installation

### Step 1: Clone Repository
```bash
git clone https://github.com/yourusername/MatrixFluid.git
cd MatrixFluid
```

### Step 2: Install Dependencies
```bash
# Install PlatformIO Core (if not using VS Code)
pip install platformio

# Or use VS Code with PlatformIO extension
```

### Step 3: Configure Settings (Optional)
Edit `src/config.h` if needed:
```cpp
// Adjust these values if needed
#define DISPLAY_TIMEOUT_MS 7000    // 7 seconds
#define TAP_THRESHOLD_G 1.5        // 1.5g acceleration
#define LED_BRIGHTNESS 40          // Max 40/255 (~15%)
#define ENABLE_WIFI false          // Set true for Wi-Fi
#define ENABLE_BLE false           // Set true for BLE
```

### Step 4: Build and Upload
```bash
# Build the project
pio run

# Upload to board (connect USB-C first)
pio run --target upload

# Monitor serial output
pio device monitor
```

## Initial Testing

### 1. Power-On Self Test
When powered on, the device should:
- Briefly display all three patterns (green, yellow, red)
- Each pattern shows for 0.5 seconds
- Then turn off and enter normal operation

### 2. Sensor Test
Simulate fluid levels using jumper wires:

| Test Case | GPIO2 (Half) | GPIO3 (Empty) | Expected Display |
|-----------|--------------|---------------|-----------------|
| Tank Full | Open | Open | Green checkmark |
| Tank Half | Connected to GND | Open | Yellow caution |
| Tank Empty | Connected to GND | Connected to GND | Red stop sign |

### 3. Tap Detection Test
1. Hold the board steady
2. Tap firmly three times in quick succession
3. Display should activate showing current fluid level
4. Display auto-offs after 7 seconds

### 4. Serial Debug
Monitor serial output at 115200 baud:
```
[BOOT] MatrixFluid v1.0.0
[INIT] LED Matrix: OK
[INIT] Accelerometer: OK
[INIT] Sensors: OK
[TAP] Detected triple-tap!
[DISPLAY] Showing: ABOVE_HALF
[DISPLAY] Auto-off after timeout
```

## Vehicle Installation

### Mounting Location
- Choose vibration-dampened location
- Ensure easy reach for tapping
- Keep away from heat sources
- Protect from moisture

### Power Connection
1. Connect to switched 12V (turns off with ignition)
2. Use inline fuse (1A recommended)
3. Ensure solid ground connection
4. Test voltage with multimeter first

### Sensor Installation
1. Mount sensors in fluid tank
2. Half sensor at 50% level
3. Empty sensor at 10-15% level
4. Route wires away from heat/moving parts
5. Use weatherproof connections

## Operation

### Normal Use
1. Triple-tap the device to check fluid level
2. Observe the displayed icon:
   - 🟢 Green checkmark = Good (above half)
   - 🟡 Yellow triangle = Caution (below half)
   - 🔴 Red octagon = Low (near empty)
   - ❌ Blinking red X = Sensor error
3. Display turns off automatically after 7 seconds

### Wi-Fi Access (If Enabled)
1. After activation, connect to "TankMonitor" Wi-Fi
2. Open browser to http://192.168.4.1
3. View detailed status and history

### BLE Monitoring (If Enabled)
1. Use any BLE scanner app
2. Look for "TankMon" device
3. View advertised data (no pairing needed)

## Troubleshooting

### No Display on Triple-Tap
- Check serial monitor for tap detection
- Adjust TAP_THRESHOLD_G if too sensitive/insensitive
- Ensure accelerometer is initialized

### Wrong Fluid Level Shown
- Verify sensor wiring (use multimeter)
- Check sensor logic in serial monitor
- Ensure sensors mounted at correct levels

### Display Too Bright/Dim
- Adjust LED_BRIGHTNESS in config.h
- Maximum safe value is 40 (out of 255)
- Recompile and upload after changes

### Device Resets/Crashes
- Check power supply stability
- Ensure adequate current capacity (>500mA)
- Monitor serial for error messages
- Check for overheating

## Maintenance

### Regular Checks
- Verify sensor operation monthly
- Check mounting security
- Inspect wiring for damage
- Clean device if dusty

### Firmware Updates
```bash
# Pull latest changes
git pull

# Rebuild and upload
pio run --target upload
```

## Safety Notes

⚠️ **Important Safety Information**:
- Never exceed 40/255 LED brightness (overheating risk)
- Ensure proper ventilation around device
- Use fused power connection in vehicle
- Do not operate while driving
- Mount securely to prevent damage

## Support

For issues or questions:
- Check serial monitor output first
- Review troubleshooting section
- Open issue on GitHub with debug log
- Include hardware setup details

## License

This project is open source under MIT License.
See LICENSE file for details.