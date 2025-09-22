# Tasks: Vehicle Fluid Level Indicator

**Input**: Design documents from `/specs/001-build-a-vehicle/`
**Prerequisites**: plan.md (required), research.md, data-model.md, contracts/

## Execution Flow (main)
```
1. Load plan.md from feature directory
   → If not found: ERROR "No implementation plan found"
   → Extract: tech stack, libraries, structure
2. Load optional design documents:
   → data-model.md: Extract entities → model tasks
   → contracts/: Each file → contract test task
   → research.md: Extract decisions → setup tasks
3. Generate tasks by category:
   → Setup: project init, dependencies, linting
   → Tests: contract tests, integration tests
   → Core: models, services, CLI commands
   → Integration: DB, middleware, logging
   → Polish: unit tests, performance, docs
4. Apply task rules:
   → Different files = mark [P] for parallel
   → Same file = sequential (no [P])
   → Tests before implementation (TDD)
5. Number tasks sequentially (T001, T002...)
6. Generate dependency graph
7. Create parallel execution examples
8. Validate task completeness:
   → All contracts have tests?
   → All entities have models?
   → All endpoints implemented?
9. Return: SUCCESS (tasks ready for execution)
```

## Format: `[ID] [P?] Description`
- **[P]**: Can run in parallel (different files, no dependencies)
- Include exact file paths in descriptions

## Path Conventions
- **Embedded project**: `src/` at repository root

> **Update (2025-XX-XX):** Implementation now lives in `main/` and `components/` under ESP-IDF; legacy `src/` references remain for context when reading past tasks.
- **Test files**: Legacy `test/` directory (current development uses `components/<module>/test/`)
- **Build config**: Historic `platformio.ini` (superseded by `idf.py` project configuration)

## Phase 3.1: Setup
> Legacy note: Tasks referencing `tap_detection` or accelerometer support originate from the initial gesture-based concept and are retained for historical context. Current work prioritises timed scheduling and Wi-Fi portal integration.
- [X] T001 Create project directory structure per plan.md (src/, src/sensors/, src/display/, src/detection/, src/wireless/)
- [X] T002 Initialize PlatformIO project with platformio.ini for esp32-s3-devkitc-1 and Arduino framework
- [X] T003 Add library dependencies to platformio.ini (FastLED, QMI8658 sensor library)
- [X] T004 [P] Create src/config.h with system constants and safety limits (LED_BRIGHTNESS=40, DISPLAY_TIMEOUT_MS=7000)
- [X] T005 [P] Create README.md with project overview and safety warnings

## Phase 3.2: Tests First (Hardware Simulation) ⚠️ MUST COMPLETE BEFORE 3.3
**CRITICAL: These test stubs MUST be written before ANY implementation**
- [X] T006 [P] Create test/test_tap_detection.cpp with tap detector test cases (triple-tap, vibration rejection)
- [X] T007 [P] Create test/test_sensor_logic.cpp for fluid sensor state machine validation
- [X] T008 [P] Create test/test_led_patterns.cpp for LED icon rendering verification
- [X] T009 [P] Create test/test_http_api.cpp for Wi-Fi API endpoint tests (if ENABLE_WIFI)
- [X] T010 [P] Create test/test_ble_format.cpp for BLE advertisement format validation (if ENABLE_BLE)

## Phase 3.3: Core Implementation - Hardware Abstraction Layer
- [X] T011 [P] Implement src/sensors/fluid_sensors.h and .cpp with GPIO reading and debouncing
- [X] T012 [P] Implement src/sensors/accelerometer.h and .cpp with QMI8658 initialization and interrupt setup
- [X] T013 [P] Implement src/display/icons.h with 8x8 pixel patterns for checkmark, caution, stop, error
- [X] T014 [P] Implement src/display/led_controller.h and .cpp with FastLED matrix control and brightness limiting

## Phase 3.4: Core Implementation - Business Logic
- [X] T015 Implement src/detection/tap_detector.h and .cpp with triple-tap state machine
- [X] T016 Create src/models.h with FluidLevel, DisplayState, TapEvent, SystemConfig structs
- [X] T017 Implement src/state_manager.cpp to coordinate sensor readings, display state, and timeouts
- [X] T018 Create src/main.cpp with setup() and loop() functions, system initialization

## Phase 3.5: Integration - Main Control Flow
- [ ] T019 Integrate tap detector with accelerometer interrupts in main.cpp
- [ ] T020 Connect fluid sensor readings to display state transitions
- [ ] T021 Implement display timeout logic with millis() tracking
- [ ] T022 Add power-on self-test sequence (show all patterns on boot)
- [ ] T023 Implement error handling for sensor failures (default to caution state)

## Phase 3.6: Optional Features - Wireless
- [ ] T024 [P] Implement src/wireless/wifi_server.h and .cpp with HTTP endpoints (conditional on ENABLE_WIFI)
- [ ] T025 [P] Implement src/wireless/ble_beacon.h and .cpp with advertisement format (conditional on ENABLE_BLE)
- [ ] T026 Integrate wireless modules with compile-time flags in main.cpp
- [ ] T027 Create HTML status page template in src/wireless/index_html.h

## Phase 3.7: Hardware Testing & Tuning
- [ ] T028 Create test/manual_bench_test.md with step-by-step hardware testing procedures
- [ ] T029 Tune TAP_THRESHOLD_G and TAP_WINDOW_MS based on physical testing
- [ ] T030 Verify LED brightness safety with thermal testing (must stay ≤5/255)
- [ ] T031 Test power consumption in different states with multimeter
- [ ] T032 Validate sensor logic with jumper wire simulation

## Phase 3.8: Polish & Documentation
- [ ] T033 [P] Add comprehensive inline documentation to all header files
- [ ] T034 [P] Create docs/wiring_diagram.md with detailed connection instructions
- [ ] T035 [P] Write docs/troubleshooting.md for common issues
- [ ] T036 Optimize memory usage (review static allocations)
- [ ] T037 Add version info and build timestamp to startup message
- [ ] T038 Run full integration test following quickstart.md procedures

## Dependencies
- Setup (T001-T005) blocks everything
- Test stubs (T006-T010) before implementation
- Hardware abstraction (T011-T014) before business logic (T015-T018)
- Business logic before integration (T019-T023)
- Core complete before optional wireless (T024-T027)
- Implementation before hardware testing (T028-T032)
- Everything before final polish (T033-T038)

## Parallel Execution Examples

### Setup Phase (T004-T005)
```bash
# Can run simultaneously as different files:
Task: "Create src/config.h with system constants"
Task: "Create README.md with project overview"
```

### Test Creation (T006-T010)
```bash
# All test files can be created in parallel:
Task: "Create test/test_tap_detection.cpp"
Task: "Create test/test_sensor_logic.cpp"
Task: "Create test/test_led_patterns.cpp"
Task: "Create test/test_http_api.cpp"
Task: "Create test/test_ble_format.cpp"
```

### Hardware Abstraction (T011-T014)
```bash
# Independent modules in different files:
Task: "Implement src/sensors/fluid_sensors.cpp"
Task: "Implement src/sensors/accelerometer.cpp"
Task: "Implement src/display/icons.h"
Task: "Implement src/display/led_controller.cpp"
```

### Documentation (T033-T035)
```bash
# Different documentation files:
Task: "Add inline documentation to headers"
Task: "Create docs/wiring_diagram.md"
Task: "Write docs/troubleshooting.md"
```

## Notes
- [P] tasks = different files, no shared dependencies
- Hardware testing requires physical board and sensors
- Wireless features are compile-time optional (check ENABLE_WIFI/ENABLE_BLE)
- Safety critical: LED_BRIGHTNESS must never exceed 5/255
- Use PlatformIO Monitor for serial debugging (115200 baud)
- Commit after each completed phase

## Validation Checklist
*GATE: Checked by main() before returning*

- [x] All contracts have corresponding tests (HTTP API, BLE format)
- [x] All entities have implementation tasks (FluidLevel, DisplayState, etc.)
- [x] All tests come before implementation (T006-T010 before T011+)
- [x] Parallel tasks truly independent (different files)
- [x] Each task specifies exact file path
- [x] No task modifies same file as another [P] task
- [x] Safety constraints documented (LED brightness limit)
- [x] Hardware testing procedures included
