# MatrixFluid Constitution

## Core Principles

### I. Embedded Efficiency

Use a lightweight embedded approach. Prefer the ESP-IDF or Arduino framework in C/C++ for direct hardware control without unnecessary overhead. Avoid heavy abstractions that could introduce latency or high resource usage; keep the firmware lean and responsive.

### II. Hardware Safety & Constraints

Adhere strictly to hardware limits. The 8×8 LED matrix (WS2812B LEDs) must be run at low brightness (around 10% max) to prevent overheating the board. The device should never engage all LEDs at full power. Also, consider power usage – when the vehicle ignition is off, the system should draw minimal current (the design could allow deep sleep when inactive).

### III. User-Centric Activation

The display will only activate on a deliberate user gesture. Implement robust tap detection via the accelerometer (QMI8658) such that normal car motion won't trigger it. For example, require three taps in quick succession to toggle the display. This pattern is a non-negotiable requirement to avoid false signals. User interaction should be simple (tap and read the light) and not require any mobile app or complex steps for core functionality.

### IV. Clear Visual Signals

The output symbols and colors must be unambiguous and instantly recognizable. Green means "OK/above half", yellow means "caution/half to low", red means "attention needed/near empty". The symbols (check mark, caution icon, octagon stop) should be as clear as possible on the 8×8 grid. We prioritize clarity over artistic complexity – even a rough shape is fine as long as the color coding is correct and the driver understands it.

### V. Robustness in Vehicle Environment

The device should tolerate the temperature and vibration range of a car. Secure any loose components and ensure sensor readings are debounced or filtered for realistic conditions. The software must handle edge cases (e.g., if a sensor fails or gives inconsistent readings, handle gracefully by maybe defaulting to caution state). No continuous beeping or flashing – the feedback is only via the LED matrix and only when requested.

### VI. Optional Connectivity (Non-Intrusive)

Any wireless feature (Wi-Fi/BLE) is secondary and should not compromise the device's primary function or simplicity. If implemented, it should run in the background or on demand. For instance, an onboard Wi-Fi AP can provide a status page, but it must not require user to always connect – it's just a convenience. Ensure that adding this does not significantly increase power draw when not in use (e.g., you might keep the radio off until a tap wakes the device, or use BLE advertising sparingly). The system should work out-of-the-box without wireless configuration, as a standalone gadget.

### VII. No Overengineering & Testing Approach

This is essentially an embedded indicator light – keep the code straightforward. Use standard libraries (FastLED/NeoPixel for LEDs, a sensor library for QMI8658) instead of reinventing protocols, but avoid large frameworks that aren't needed. Given the project scope, exhaustive unit testing on the microcontroller isn't required; manual testing by simulating taps and sensor triggers will suffice. We will favor iterative tuning (in-car testing) over formal test suites, due to the physical nature of the project.

## Governance

The above principles guide development decisions. The team (or AI agent) must follow these rules: e.g., if brightness or activation method deviates from spec, that's a violation. Any proposal to change these must be discussed (e.g., if triple-tap is too hard to detect reliably, we'd revisit it, but only with good reason). The constitution supersedes any generic coding patterns – e.g., even if an auto-generated code suggests turning the display on continuously, we override that with our rule of tap-activation only. Focus on delivering a functional, safe, and user-friendly solution in line with these non-negotiables.

## Implementation Plan

## Technical Context & Stack

### Platform

Firmware will run on the Waveshare ESP32-S3-Matrix board (ESP32-S3 MCU with built-in 8×8 RGB LED matrix and QMI8658 accelerometer). We'll program it using the ESP-IDF framework (C/C++) or Arduino Core C++ (whichever allows faster development; Arduino might be simpler with available libraries). The choice depends on team familiarity – both are viable, and both support the required hardware.

### Language & Framework

C/C++17 for embedded. If Arduino, we use the Arduino ESP32 core (with setup/loop structure). If ESP-IDF, we structure as an ESP-IDF component with an app_main. In either case, we will manage real-time responsiveness (using FreeRTOS tasks or simple loop delays as needed).

### Key Libraries/Dependencies

- **LED Matrix**: Use Adafruit NeoPixel + NeoMatrix libraries or FastLED to control the WS2812B 8x8 LED array. These handle the low-level timing to drive the addressable LEDs. We will configure the matrix with 64 LEDs on GPIO14 and set a global brightness limit (~40/255 or 15%) to cap output.

- **Graphics Rendering**: If using Adafruit GFX (with NeoMatrix), we can easily draw shapes (pixels, lines) for the icons. Otherwise, we'll manually map 2D coordinates to the 1D LED array index (considering the zigzag layout of the matrix).

- **Accelerometer (QMI8658)**: Use an Arduino library like QMI8658c or the Waveshare SensorLib to initialize and read acceleration and gyroscope data. This abstracts I2C reads and provides calibrated values. We might also use basic Vector math on accel readings to detect magnitude.

- **Wi-Fi/BLE (if used)**: Use ESP32 WiFi library (for AP and web server) or NimBLE-Arduino for BLE. These come with the ESP32 framework. We plan a simple HTTP server for status or BLE advertising; no heavy protocols, no cloud dependency.

## System Architecture

The firmware will be relatively simple:

- **Sensor Module**: Code to read the two level sensors (GPIO inputs) and the accelerometer. Possibly run the accelerometer reading in its own task or timer interrupt to catch tap events with precise timing.

- **Tap Detection Logic**: A small state machine or counter in firmware memory that records when potential tap events happen (time stamps of significant accel spikes) and determines when three have occurred in the required pattern. This could be part of the main loop or an ISR that enqueues events.

- **Display Controller**: Functions to update the LED matrix. Possibly an updateDisplay(state) that takes an enum of {OK, LOW, EMPTY, OFF} and lights the appropriate pattern. By default, state is OFF. When activated, it will show one of OK/LOW/EMPTY for a few seconds then revert to OFF.

- **Wireless Interface**: (Optional) If we include this, have a separate component for it. For Wi-Fi AP, on boot the device can start an AP and server but we might keep the server dormant until a request comes. For BLE, initialization at start to broadcast periodically. Ensure these do not block the main loop significantly (use event-driven or separate task for server).

We will utilize interrupts or non-blocking delays where possible to keep responsiveness (e.g., use vTaskDelay or delay() in Arduino sparingly so as not to miss taps).

## Target Behavior Details

- **Idle Mode**: Device mostly sleeps or loops doing nothing major until a tap is detected. Possibly use light sleep mode and wake on accelerometer interrupt (if QMI8658 can wake the ESP32 via GPIO – needs hardware interrupt wiring).

- **Activation Sequence**: On triple-tap detection, determine fluid level and call updateDisplay with corresponding icon. Also, if using Wi-Fi, perhaps turn on an LED or send a console message so user knows the AP is active (or even use the matrix to scroll "WiFi" after the icon).

- **Timeout**: Use a software timer (or millis() checks) to turn the display back off after, say, 5-10 seconds. This will also disconnect/turn off Wi-Fi AP if we only want it on briefly (to be power-conscious).

- **Error handling**: If accelerometer read fails or sensors read an impossible combination (e.g., low sensor indicates full but mid sensor doesn't – depending on sensor design), we might just choose the safer indication (show red or yellow) to prompt checking the system. These scenarios will be logged to serial for debugging.

## Testing & Tuning Plan

We'll iteratively test the firmware:

1. In a bench setup (development PC), simulate taps by shaking the board and watch serial debug to ensure tap count logic works.
2. Simulate sensor states by jumping pins to ground/Vcc and confirm correct LED response (we can create a debug command or use serial input to force states).
3. Real-world car test: place board in vehicle, drive, note if any false triggers or missed triggers. Adjust the accelerometer threshold or time window accordingly.
4. Test with actual fluid level changes if possible (fill/drain the tank to trigger sensors).

We won't write formal unit tests given the hardware-focused nature, but will rely on these practical tests. The constitution's guidelines on manual testing apply here (no extensive TDD needed, but must verify in situ that triple-tap is discriminating enough).

## Performance and Resource Considerations

The ESP32-S3 has ample resources for this task (240MHz dual-core, 512KB SRAM + 2MB PSRAM) and the code footprint will be small. We expect CPU usage to be minimal (reading sensors and updating 64 LEDs is trivial). Even with Wi-Fi or BLE enabled, it should handle it.

Memory use will primarily come from libraries (FastLED might use ~ (64 LEDs * 3 bytes) = 192 bytes for LED data, negligible; QMI8658 lib similarly small). We will ensure the program fits in the 4MB flash.

**Power**: If on vehicle battery, the ESP32 should ideally deep sleep when off. However, since we need to detect taps, we might run in a light sleep with IMU interrupt – this area may need exploration, but as an MVP we can run it normally and assume car's accessory power is available when needed.

## Connectivity Implementation (Optional Scope)

If including the wireless feature, plan how user will interact:

- **Wi-Fi AP**: On activation, the driver might pull out a phone, connect to "TankMonitor" AP, and see the status page. We'll host a simple HTML showing the percentage or just the same green/yellow/red status. We must code the HTML and server handler. Security can be open or a simple password since range is limited.

- **BLE**: The driver could use a generic BLE scanner app to read the advert data. We can format the advert as Name: TankMon and put level in manufacturer data. This is techy but avoids network steps. No pairing needed.

Because these are additional, we'll modularize them so they can be enabled or disabled at compile time (using `#define USE_WIFI` for example). This way, if resource issues arise, we can drop them.

## Timeline & Phases

1. **Phase 1 – Core Functionality**: Get readings from sensors and IMU, and drive the LED matrix with a test pattern. Ensure we can light each color and that basic tap detection works (maybe initially just turn on any LED on a single tap to prove concept).

2. **Phase 2 – Implement Patterns & Logic**: Code the logic to map sensor input to the correct icon, and only display it on triple-tap. Test and refine the gesture recognition. This phase ends with a working in-car demo: tap = shows correct light, no tap = no light.

3. **Phase 3 – Wireless Feature (if included)**: Add the Wi-Fi AP mode and a small web server, or BLE broadcast. Test that these do not hamper the tap responsiveness (the main loop might need adjustments if Wi-Fi is running – consider using separate FreeRTOS tasks for the server to keep the main loop free for sensor scanning).

4. **Phase 4 – Polishing**: Optimize power (maybe disable Wi-Fi radio when not in use, adjust IMU settings to low-power mode if possible). Clean up code, add comments, and ensure the device starts reliably with desired initial conditions (e.g., matrix off, maybe a self-test blink at startup could be useful). Prepare documentation for usage (how to interpret lights, how to connect to AP if applicable).

## Deliverables

The final codebase (in a GitHub repo or similar) with source files (.ino or .c/.cpp and .h), a README explaining how to wire the sensors and how to use the device, and perhaps a diagram of the LED icons for reference. We will also include the Spec documents (like this plan and the spec description) for completeness in project docs.
