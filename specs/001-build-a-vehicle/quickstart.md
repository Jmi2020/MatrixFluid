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
- ESP-IDF v5.x toolchain (`idf.py` available in your shell)
- USB-C cable for programming
- Serial terminal (`idf.py monitor` or equivalent)

## Hardware Setup

### Wiring Diagram

```
ESP32-S3-Matrix Board
┌─────────────────────┐
│                     │
│  [USB-C Port]       │
│                     │
│  GPIO2 ←── Half Sensor (NO contact to 3V3)
│  GPIO3 ←── Empty Sensor (NO contact to 3V3)
│  3V3  ←── Sensor Common
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
1. Connect half-full sensor between GPIO2 and 3V3
2. Connect near-empty sensor between GPIO3 and 3V3
3. Sensors should be normally open (NO) switches
4. When fluid is present, the switch closes and drives the GPIO high (3V3)

## Software Installation

### Step 1: Clone Repository
```bash
git clone https://github.com/yourusername/MatrixFluid.git
cd MatrixFluid
```

### Step 2: Install Dependencies
Set up the ESP-IDF environment following Espressif's guide. Typical steps:
```bash
python -m pip install --upgrade esptool
git clone --recursive https://github.com/espressif/esp-idf.git $HOME/esp/esp-idf
$HOME/esp/esp-idf/install.sh esp32s3
source $HOME/esp/esp-idf/export.sh  # Run in every new shell
```

### Step 3: Configure Settings (Optional)
Tune component headers as required:
```c
// components/led_matrix/include/led_matrix.h
#define LED_MAX_BRIGHTNESS      5   // Proven safe limit (~2% duty cycle)
#define LED_DEFAULT_BRIGHTNESS  3   // Boot brightness

// components/display_controller/include/display_controller.h
#define DISPLAY_UPDATE_INTERVAL_MINUTES 15   // Scheduled wake cadence
#define DISPLAY_TIMEOUT_MS              7000 // LEDs on-time per cycle

// components/wifi_config/include/wifi_config.h
#define WIFI_AP_SSID     "MatrixFluid-Config"
#define WIFI_AP_PASSWORD "fluid123"
```

### Step 4: Build and Upload
```bash
# Select target once per checkout
idf.py set-target esp32s3

# Build the firmware
idf.py build

# Flash (adjust port as needed)
idf.py -p /dev/cu.usbmodem* flash

# Monitor serial output (115200 baud)
idf.py -p /dev/cu.usbmodem* monitor
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
| Tank Full | Connected to 3V3 | Connected to 3V3 | Green checkmark |
| Tank Half | Open | Connected to 3V3 | Yellow caution |
| Tank Empty | Open | Open | Red stop sign |

### 3. Scheduler Test
1. Allow the device to idle until the configured interval elapses (default 15 minutes).
2. Confirm the display activates automatically and shows the correct icon.
3. Verify the display turns off after the timeout and logs an entry in the serial monitor.

### 4. Wi-Fi Portal Test
1. Connect a phone or laptop to the `MatrixFluid-Config` network.
2. Browse to http://192.168.4.1 and press the manual refresh button.
3. Confirm the display activates immediately and the portal response matches the LED status.

### 5. Serial Debug
Monitor serial output at 115200 baud:
```
[BOOT] MatrixFluid v1.0.0
[INIT] LED Matrix: OK
[INIT] Sensors: OK
[INIT] Scheduler: interval=15m timeout=7s
[INIT] Wi-Fi AP: MatrixFluid-Config
[SCHED] Interval elapsed -> refreshing status
[DISPLAY] Showing: ABOVE_HALF
[DISPLAY] Auto-off after timeout
[PORTAL] Manual refresh served to 192.168.4.2
```

## Vehicle Installation

### Mounting Location
- Choose vibration-dampened location
- Keep device visible to the driver without glare
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
1. The device wakes on a schedule (default every 15 minutes) and shows the latest status automatically.
2. Connect to the `MatrixFluid-Config` Wi-Fi network and open http://192.168.4.1 to trigger an immediate refresh when needed.
3. Observe the displayed icon:
   - 🟢 Green checkmark = Good (above half)
   - 🟡 Yellow triangle = Caution (below half)
   - 🔴 Red octagon = Low (near empty)
   - ❌ Blinking red X = Sensor error
4. The display turns off automatically after the configured timeout (default 7 seconds).

### Wi-Fi Access (If Enabled)
1. Connect to the `MatrixFluid-Config` Wi-Fi network (default password `fluid123`).
2. Open a browser to http://192.168.4.1.
3. View status history, trigger a manual refresh, or adjust timing/brightness if those controls are exposed.

## Troubleshooting

### No Scheduled Display Cycle
- Check serial monitor for scheduler logs (`[SCHED]` entries)
- Review interval and timeout definitions in `components/display_controller/include/display_controller.h`
- Trigger a manual refresh via the Wi-Fi portal to verify LEDs and sensors

### Wrong Fluid Level Shown
- Verify sensor wiring (use multimeter)
- Check sensor logic in serial monitor
- Ensure sensors mounted at correct levels

### Display Too Bright/Dim
- Update `LED_DEFAULT_BRIGHTNESS` in `components/led_matrix/include/led_matrix.h`
- Respect the enforced `LED_MAX_BRIGHTNESS` of 5 (out of 255)
- Rebuild and flash after changes

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
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

## Safety Notes

⚠️ **Important Safety Information**:
- Never exceed 5/255 LED brightness (overheating risk)
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
