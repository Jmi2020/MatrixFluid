# MatrixFluid - Vehicle Fluid Level Indicator

A safety-critical embedded device for monitoring vehicle fluid levels using the Waveshare ESP32-S3-Matrix board with an 8×8 RGB LED display, timed wake cycles, and an onboard Wi-Fi portal.

## ⚠️ SAFETY WARNINGS

**CRITICAL SAFETY REQUIREMENTS:**
- LED brightness is hardcoded to maximum 5/255 (~2%) to prevent overheating
- Device must be mounted securely to prevent vibration damage
- Use fused power connection (1A recommended) 
- Connect to switched 12V power (ignition-controlled)
- Do not operate while driving

**Hardware Limits:**
- Never exceed 5/255 LED brightness setting
- Operating temperature: -20°C to +85°C
- Maximum continuous operation: Limited by thermal design
- Power consumption: <150mA active, <2mA sleep

## Features

- **Timed status cycles** - Automatically wakes at a configurable interval to report tank status
- **Visual status indication** - Green checkmark, yellow caution, red stop, error X
- **Wi-Fi access point** - Hosts a captive portal for manual refresh and configuration
- **OTA updates** - Upload new firmware binaries directly from the portal (no USB required)
- **Auto-shutoff** - 7-second timeout for power conservation
- **Error handling** - Defaults to caution state on sensor failures
- **Low-glare output** - LED brightness capped at 5/255 for thermal safety
- **USB demo mode** - When connected to a USB host (or toggled via the portal) the matrix loops kiosk animations

## Hardware Requirements

- Waveshare ESP32-S3-Matrix board (8×8 LED matrix; onboard IMU unused)
- 4× Fluid level sensors (non-contact or float, normally open)
- 12V to 5V buck converter for vehicle power
- Mounting hardware and wiring

## Quick Start

1. **Hardware Setup**
   ```
   GPIO4 ← Full sensor (drives 3V3 when submerged)
   GPIO2 ← Above-half sensor (drives 3V3 when submerged)
   GPIO5 ← Below-half sensor (drives 3V3 when submerged)
   GPIO3 ← Reserve/near-empty sensor (drives 3V3 when submerged)
   5V ← Buck converter output
   GND ← Common ground
   ```

2. **Software Installation**
   ```bash
   git clone https://github.com/yourusername/MatrixFluid.git
   cd MatrixFluid
   idf.py set-target esp32s3
   idf.py build
   idf.py -p /dev/cu.usbmodem* flash monitor
   ```

3. **Operation**
   - The controller wakes on a schedule (default: every 15 minutes) and shows the current level
   - Connect to the `MatrixFluid-Config` Wi-Fi network to trigger on-demand updates, configure demo mode, and stage OTA firmware
   - The display automatically turns off after 7 seconds

## Status Indicators

| Icon | Color | Meaning | Sensor State |
|------|-------|---------|--------------|
| ✓ | Green | Tank full | All sensors HIGH |
| ✓ | Green | Level OK (above half) | Full sensor LOW, remaining sensors HIGH |
| ⚠ | Yellow | Below half | Full & above-half sensors LOW, lower sensors HIGH |
| ⬢ | Red | Reserve low | Only reserve sensor HIGH |
| ⬢ | Red | Empty | All sensors LOW |
| ✗ | Red (blinking) | Sensor error | Invalid combination |

## Configuration

Adjust display safety limits in `components/led_matrix/include/led_matrix.h`:

```c
#define LED_MAX_BRIGHTNESS      5   // Proven safe limit (≈2% duty cycle)
#define LED_DEFAULT_BRIGHTNESS  3   // Boot-time brightness
```

Sensor timings and debounce rules live in `components/sensors/include/fluid_sensors.h`; update `FLUID_DEBOUNCE_MS` and `FLUID_READ_INTERVAL_MS` if you validate new hardware. Wi-Fi defaults (SSID, credentials, web options) are defined in `components/wifi_config/include/wifi_config.h`.

## Optional Features

### Wi-Fi Access Point
When enabled (default), the device hosts http://192.168.4.1 with:
- `/status` - JSON status data
- `/config` - Configuration management
- `/test` - Manual display testing
- `/stream` - WebSocket real-time updates
- `/api/demo` - Toggle kiosk demo mode (USB host or portal request)
- `/api/ota` - Accepts a firmware `.bin` upload and schedules a reboot to install it

Portal cards show both the live sensor reading and the pattern currently on the LEDs, plus OTA progress (Idle → Uploading → Ready to reboot).

#### OTA Refresh
1. Build a fresh image: `idf.py build` (or obtain a trusted, signed `.bin` from CI).
2. Confirm the new binary’s version (`idf.py size` or `idf.py build && grep "Project name" build/bootloader/log.txt`).
3. Power the unit, join the `MatrixFluid-Config` SSID, and browse to http://192.168.4.1/.
4. Scroll to **Firmware Update**, choose the new `.bin`, then click **Upload & Install**. Leave the page open while progress updates.
5. When the banner changes to *Ready to reboot*, the device will restart (portal drops for ~10 s). If not, tap **LED Status Snapshot** to verify `ota_pending_reboot=false` and reboot manually.
6. After reconnecting, press **Device Health Snapshot** or curl `/api/health` to ensure the reported `ota_last_error` is `0` and the `status` block shows the updated build ID.

> **Tip:** If you keep a previous image on hand, you can always revert by re-uploading that `.bin` through the same panel.

## Development

### Build System
- **ESP-IDF 5.x** with CMake/Ninja; run `idf.py set-target esp32s3` once so `sdkconfig` stays consistent.
- **FreeRTOS** tasks coordinate sensors, display, Wi-Fi, and demo mode; tune stack sizes in `main/main.c` if you add workloads.
- **RMT-driven WS2812 control** lives in `components/led_matrix/`, enforcing brightness caps and safe startup clearing.

### Testing
- Build iteratively with `idf.py build` and attach logs using `idf.py -p /dev/cu.usbmodem* monitor`.
- Add Unity suites under `components/<module>/test/` or `test_apps/` and execute with `idf.py -T <test_app> build flash monitor` before merging changes.
- Continue hardware-in-the-loop validation: jumper sensors for edge cases, log scheduled wake-ups, measure current draw, and document thermal runs.

### Architecture
```
MatrixFluid/
├── main/                     # Application entry (main.c, startup orchestration)
├── components/
│   ├── led_matrix/           # WS2812 driver, patterns, brightness guards
│   ├── display_controller/   # Periodic display scheduler and LED orchestration
│   ├── sensors/              # Fluid sensor debouncing and callbacks
│   ├── wifi_config/          # Configuration AP + HTTP server
│   ├── demo_mode/            # USB host detection and kiosk animations
│   └── tap_detection/        # Legacy gesture implementation (deprecated)
├── docs/                     # Auxiliary guides and reports
├── specs/                    # Design specs and safety artifacts
├── Research/                 # Experiments and collected data
└── build/                    # Generated output (clean before committing)
```

## Troubleshooting

**No scheduled display cycle:**
- Check serial monitor for scheduler ticks (`display_controller` logs)
- Confirm the device clock interval in `components/display_controller/include/display_controller.h`
- Trigger a manual update via the Wi-Fi portal to confirm LEDs are functional

**Wrong fluid level shown:**
- Verify sensor wiring with multimeter
- Check sensor mounting positions
- Review sensor logic table in debug output

**Display too bright/dim:**
- Update `LED_DEFAULT_BRIGHTNESS` in `components/led_matrix/include/led_matrix.h`
- Never exceed the enforced `LED_MAX_BRIGHTNESS` (5/255)
- Rebuild and flash with `idf.py flash`

**Device resets:**
- Check power supply stability (need >500mA capacity)
- Monitor for overheating
- Review serial output for stack overflow/panic

**Demo mode won't start:**
- Ensure the USB-C cable is connected to a host PC (not just a charger)
- Toggle Demo Mode from the portal if the device is running on vehicle power
- Check the portal status cards for `Power Source: USB`
- Confirm the `Display` card matches the `Fluid Level` card to rule out stale sensor data

## Safety Compliance

This device implements multiple safety measures:
- **Hardware protection:** Brightness limiting prevents LED overheating
- **Software validation:** Runtime checks enforce safety constraints  
- **Error recovery:** Graceful degradation on component failures
- **Power management:** Automatic shutoff and low-duty-cycle scheduling reduce consumption
- **Manual override control:** Wi-Fi portal requires explicit confirmation before refreshing display

## License

MIT License - See LICENSE file for details.

## Support

- **Documentation:** See `docs/` directory for detailed guides
- **Issues:** GitHub issue tracker with hardware setup details
- **Debug:** Enable serial monitor at 115200 baud for diagnostics

---
**Version:** 1.0.0 | **Platform:** ESP32-S3 | **Framework:** ESP-IDF 5.x
