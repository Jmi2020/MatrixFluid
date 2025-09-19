# Implementation Plan: Vehicle Fluid Level Indicator

**Branch**: `001-build-a-vehicle` | **Date**: 2025-01-18 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/001-build-a-vehicle/spec.md`

## Execution Flow (/plan command scope)
```
1. Load feature spec from Input path
   → If not found: ERROR "No feature spec at {path}"
2. Fill Technical Context (scan for NEEDS CLARIFICATION)
   → Detect Project Type from context (web=frontend+backend, mobile=app+api)
   → Set Structure Decision based on project type
3. Fill the Constitution Check section based on the content of the constitution document.
4. Evaluate Constitution Check section below
   → If violations exist: Document in Complexity Tracking
   → If no justification possible: ERROR "Simplify approach first"
   → Update Progress Tracking: Initial Constitution Check
5. Execute Phase 0 → research.md
   → If NEEDS CLARIFICATION remain: ERROR "Resolve unknowns"
6. Execute Phase 1 → contracts, data-model.md, quickstart.md, agent-specific template file
7. Re-evaluate Constitution Check section
   → If new violations: Refactor design, return to Phase 1
   → Update Progress Tracking: Post-Design Constitution Check
8. Plan Phase 2 → Describe task generation approach (DO NOT create tasks.md)
9. STOP - Ready for /tasks command
```

## Summary
Build a vehicle fluid level indicator using the Waveshare ESP32-S3-Matrix board that monitors two fluid sensors and displays status via an 8×8 RGB LED matrix when activated by triple-tap gesture. The device will use embedded C/C++ with Arduino Core or ESP-IDF framework, implementing robust tap detection, low-brightness display patterns, and optional Wi-Fi/BLE connectivity for remote monitoring.

## Technical Context
**Language/Version**: C/C++17 for embedded (Arduino Core or ESP-IDF)
**Primary Dependencies**:
- FastLED or Adafruit NeoPixel/NeoMatrix (LED control)
- QMI8658c or Waveshare SensorLib (accelerometer)
- ESP32 WiFi library (optional connectivity)
- NimBLE-Arduino (optional BLE)
**Storage**: Flash memory for config, no external storage needed
**Testing**: Manual hardware testing with bench setup and in-vehicle validation
**Target Platform**: Waveshare ESP32-S3-Matrix board (ESP32-S3 MCU, 8×8 WS2812B matrix, QMI8658 IMU)
**Project Type**: single (embedded firmware)
**Performance Goals**:
- Triple-tap detection latency <100ms
- Display update <50ms
- Power consumption <50mA when idle
**Constraints**:
- LED brightness ≤10% (40/255) to prevent overheating
- Must distinguish taps from vehicle vibrations
- Auto-shutoff after 5-10 seconds
**Scale/Scope**: Single embedded device, ~2-3K lines of code

## Constitution Check
*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- ✅ **Embedded Efficiency**: Using lightweight Arduino/ESP-IDF framework with direct hardware control
- ✅ **Hardware Safety**: LED brightness limited to 10% max, power-conscious design
- ✅ **User-Centric Activation**: Triple-tap gesture detection via accelerometer
- ✅ **Clear Visual Signals**: Green/yellow/red with distinct symbols (checkmark/caution/stop)
- ✅ **Vehicle Robustness**: Vibration filtering, sensor debouncing, graceful error handling
- ✅ **Optional Connectivity**: Wi-Fi/BLE as secondary feature, compile-time optional
- ✅ **No Overengineering**: Using standard libraries, manual testing approach

## Project Structure

### Documentation (this feature)
```
specs/001-build-a-vehicle/
├── plan.md              # This file (/plan command output)
├── research.md          # Phase 0 output (/plan command)
├── data-model.md        # Phase 1 output (/plan command)
├── quickstart.md        # Phase 1 output (/plan command)
├── contracts/           # Phase 1 output (/plan command)
└── tasks.md             # Phase 2 output (/tasks command - NOT created by /plan)
```

### Source Code (repository root)
```
# Option 1: Single project (DEFAULT) - SELECTED for embedded firmware
src/
├── main.cpp             # Main entry point (setup/loop or app_main)
├── sensors/             # Sensor reading modules
│   ├── fluid_sensors.h/cpp
│   └── accelerometer.h/cpp
├── display/             # LED matrix control
│   ├── led_controller.h/cpp
│   └── icons.h          # Icon patterns
├── detection/           # Tap detection logic
│   └── tap_detector.h/cpp
├── wireless/            # Optional connectivity
│   ├── wifi_server.h/cpp
│   └── ble_beacon.h/cpp
└── config.h             # System configuration

platformio.ini or CMakeLists.txt  # Build configuration
README.md                # User documentation
```

**Structure Decision**: Option 1 (Single project) - appropriate for embedded firmware

## Phase 0: Outline & Research
1. **Extract unknowns from Technical Context**:
   - Auto-shutoff duration (5-10 seconds suggested in user's plan)
   - Triple-tap timing parameters (window between taps, sensitivity)
   - Operating temperature range for vehicle environment
   - Error indication method for sensor failures
   - Remote monitoring update frequency

2. **Generate and dispatch research agents**:
   - Research ESP32-S3 power modes with accelerometer wake capability
   - Find best practices for WS2812B matrix on ESP32 GPIO14
   - Research QMI8658 interrupt configuration for tap detection
   - Investigate FreeRTOS task priorities for sensor vs display
   - Review Arduino vs ESP-IDF tradeoffs for this hardware

3. **Consolidate findings** in `research.md`:
   - Framework choice: Arduino Core (faster development, good library support)
   - Tap timing: 150-500ms between taps, 1.5g threshold
   - Temperature: -20°C to 85°C (automotive grade)
   - Power optimization: Light sleep with IMU wake interrupt
   - Update frequency: 1Hz for wireless, immediate for display

**Output**: research.md with all NEEDS CLARIFICATION resolved

## Phase 1: Design & Contracts
*Prerequisites: research.md complete*

1. **Extract entities from feature spec** → `data-model.md`:
   - FluidLevel enum (ABOVE_HALF, BELOW_HALF, NEAR_EMPTY, ERROR)
   - DisplayState enum (OFF, SHOWING_GREEN, SHOWING_YELLOW, SHOWING_RED)
   - TapEvent struct (timestamp, magnitude)
   - SystemConfig struct (brightness, timeout, tap_threshold)

2. **Generate API contracts** for wireless interface:
   - HTTP GET /status → JSON with fluid level
   - BLE advertisement format specification
   - WebSocket /stream for real-time updates (optional)

3. **Generate test scenarios**:
   - Bench test procedures for tap detection
   - Sensor simulation test cases
   - LED pattern validation tests
   - Power consumption measurement

4. **Create quickstart.md**:
   - Hardware setup instructions
   - Sensor wiring diagram
   - Initial firmware flash procedure
   - Basic operation guide

5. **Update CLAUDE.md** for this project

**Output**: data-model.md, /contracts/*, test procedures, quickstart.md, CLAUDE.md

## Phase 2: Task Planning Approach
*This section describes what the /tasks command will do - DO NOT execute during /plan*

**Task Generation Strategy**:
- Hardware setup and toolchain configuration tasks
- Sensor interface implementation tasks [P]
- LED matrix driver tasks [P]
- Tap detection algorithm tasks
- Display pattern creation tasks [P]
- Main control loop integration
- Optional wireless features (conditional)
- Testing and validation tasks

**Ordering Strategy**:
- Environment setup first
- Hardware abstraction before business logic
- Core features before optional features
- Integration testing last

**Estimated Output**: 20-25 numbered tasks covering setup through validation

## Phase 3+: Future Implementation
*These phases are beyond the scope of the /plan command*

**Phase 3**: Task execution (/tasks command creates tasks.md)
**Phase 4**: Implementation following embedded development workflow
**Phase 5**: Hardware validation with bench and vehicle testing

## Complexity Tracking
*No violations - design aligns with all constitutional principles*

## Progress Tracking
*This checklist is updated during execution flow*

**Phase Status**:
- [x] Phase 0: Research complete (/plan command)
- [x] Phase 1: Design complete (/plan command)
- [x] Phase 2: Task planning complete (/plan command - describe approach only)
- [ ] Phase 3: Tasks generated (/tasks command)
- [ ] Phase 4: Implementation complete
- [ ] Phase 5: Validation passed

**Gate Status**:
- [x] Initial Constitution Check: PASS
- [x] Post-Design Constitution Check: PASS
- [x] All NEEDS CLARIFICATION resolved
- [x] Complexity deviations documented (none)

---
*Based on Constitution v2.1.1 - See `/memory/constitution.md`*