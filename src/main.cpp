#include <Arduino.h>
#include "config.h"
#include "state_manager.h"

// =============================================================================
// MatrixFluid Main Application
// Vehicle Fluid Level Indicator - ESP32-S3-Matrix
// =============================================================================

// Global variables
uint32_t last_diagnostic_time = 0;
bool system_initialized = false;

// =============================================================================
// Arduino Setup Function
// =============================================================================

void setup() {
    // Initialize serial communication
    Serial.begin(SERIAL_BAUD_RATE);
    delay(2000); // Wait for serial connection
    
    // Print startup banner
    Serial.println("==========================================");
    Serial.println("MatrixFluid - Vehicle Fluid Level Indicator");
    Serial.println("Version: " FIRMWARE_VERSION);
    Serial.println("Built: " BUILD_TIMESTAMP);
    Serial.println("Platform: ESP32-S3-Matrix");
    Serial.println("==========================================");
    
    // Validate configuration
    if (!validateConfig()) {
        Serial.println("[MAIN] ERROR: Invalid configuration detected!");
        Serial.println("[MAIN] System halted for safety");
        while (true) {
            delay(1000);
        }
    }
    
    Serial.println("[MAIN] Configuration validated");
    Serial.printf("[MAIN] LED brightness limit: %d/255 (%.1f%%)\n", 
                 LED_BRIGHTNESS_MAX, (float)LED_BRIGHTNESS_MAX / 255.0f * 100.0f);
    Serial.printf("[MAIN] Display timeout: %dms\n", DISPLAY_TIMEOUT_MS);
    Serial.printf("[MAIN] Tap threshold: %.2fg\n", TAP_THRESHOLD_G);
    
    // Initialize system
    Serial.println("[MAIN] Initializing system...");
    
    if (!initializeSystem()) {
        Serial.println("[MAIN] ERROR: System initialization failed!");
        Serial.println("[MAIN] Check hardware connections and restart");
        
        // Flash error pattern
        while (true) {
            // Simple error indication without full system
            digitalWrite(LED_BUILTIN, HIGH);
            delay(250);
            digitalWrite(LED_BUILTIN, LOW);
            delay(250);
        }
    }
    
    system_initialized = true;
    
    Serial.println("[MAIN] System initialization complete");
    Serial.println("[MAIN] Ready for operation");
    Serial.println();
    Serial.println("Usage: Triple-tap the device to check fluid level");
    Serial.println("Status indicators:");
    Serial.println("  Green checkmark = Good level (above half)");
    Serial.println("  Yellow triangle = Caution (below half)");
    Serial.println("  Red octagon = Low level (near empty)");
    Serial.println("  Red X (blinking) = Sensor error");
    Serial.println();
    
    // Print safety reminders
    Serial.println("SAFETY REMINDERS:");
    Serial.println("- LED brightness is limited to " + String(LED_BRIGHTNESS_MAX) + "/255 for safety");
    Serial.println("- Display auto-shutoff after " + String(DISPLAY_TIMEOUT_MS / 1000) + " seconds");
    Serial.println("- Do not operate while driving");
    Serial.println("- Ensure secure mounting");
    Serial.println();
    
    last_diagnostic_time = millis();
}

// =============================================================================
// Arduino Main Loop
// =============================================================================

void loop() {
    if (!system_initialized) {
        delay(1000);
        return;
    }
    
    // Update system state
    updateSystem();
    
    // Periodic diagnostics output
    uint32_t now = millis();
    if (DEBUG_ENABLED && now - last_diagnostic_time > 30000) { // Every 30 seconds
        printSystemDiagnostics();
        last_diagnostic_time = now;
    }
    
    // Small delay to prevent excessive CPU usage
    delay(10); // 100Hz main loop
}

// =============================================================================
// Diagnostic Functions
// =============================================================================

void printSystemDiagnostics() {
    const SystemState& state = getSystemState();
    
    Serial.println("\n=== SYSTEM DIAGNOSTICS ===");
    Serial.printf("Uptime: %s\n", formatUptime(state.uptime_ms).c_str());
    Serial.printf("Fluid Level: %s\n", ModelUtils::fluidLevelToString(state.fluid_level).c_str());
    Serial.printf("Display: %s (active: %s)\n", 
                 ModelUtils::displayStateToString(state.display_state).c_str(),
                 state.display_active ? "YES" : "NO");
    Serial.printf("Sensors: Half=%s Empty=%s Valid=%s\n",
                 state.half_sensor_status ? "HIGH" : "LOW",
                 state.empty_sensor_status ? "HIGH" : "LOW",
                 state.sensors_valid ? "YES" : "NO");
    Serial.printf("Tap Detection: %s\n", state.tap_detection_armed ? "ARMED" : "LOCKOUT");
    Serial.printf("Total Activations: %lu\n", state.total_activations);
    Serial.printf("Memory: %lu bytes free\n", state.free_heap_bytes);
    Serial.printf("Temperature: %.1f°C\n", state.temperature_c);
    
    if (state.consecutive_errors > 0) {
        Serial.printf("⚠️  Consecutive Errors: %d\n", state.consecutive_errors);
    }
    
    if (!state.isHealthy()) {
        Serial.println("⚠️  SYSTEM HEALTH: WARNING");
    }
    
    Serial.println("===========================\n");
}

String formatUptime(uint32_t uptime_ms) {
    uint32_t seconds = uptime_ms / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    String result = "";
    if (days > 0) result += String(days) + "d ";
    if (hours > 0) result += String(hours) + "h ";
    if (minutes > 0) result += String(minutes) + "m ";
    result += String(seconds) + "s";
    
    return result;
}

// =============================================================================
// Emergency Handlers
// =============================================================================

void handleWatchdogTimeout() {
    Serial.println("[MAIN] ERROR: Watchdog timeout detected!");
    Serial.println("[MAIN] System restart required");
    
    // Try to save critical state before restart
    // (Implementation would depend on EEPROM/Flash storage)
    
    ESP.restart();
}

void handleCriticalError(const String& error_message) {
    Serial.println("[MAIN] CRITICAL ERROR: " + error_message);
    Serial.println("[MAIN] System entering safe mode");
    
    // Disable all non-essential features
    // Keep only basic error indication
    
    while (true) {
        // Flash built-in LED to indicate critical error
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
        
        // Print error message periodically
        static uint32_t last_error_print = 0;
        if (millis() - last_error_print > 10000) {
            Serial.println("[MAIN] CRITICAL ERROR: " + error_message);
            last_error_print = millis();
        }
    }
}

// =============================================================================
// Serial Command Interface (Debug)
// =============================================================================

#if DEBUG_ENABLED
void serialEvent() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "status") {
            printSystemDiagnostics();
        } else if (command == "test") {
            Serial.println("[CMD] Triggering self-test...");
            // Trigger self-test via state manager
        } else if (command == "reset") {
            Serial.println("[CMD] Resetting system...");
            ESP.restart();
        } else if (command == "help") {
            Serial.println("Available commands:");
            Serial.println("  status - Show system diagnostics");
            Serial.println("  test   - Run self-test sequence");
            Serial.println("  reset  - Restart system");
            Serial.println("  help   - Show this help");
        } else if (command.length() > 0) {
            Serial.println("[CMD] Unknown command: " + command);
            Serial.println("[CMD] Type 'help' for available commands");
        }
    }
}
#endif

// =============================================================================
// Interrupt Handlers
// =============================================================================

// Watchdog timer handler (if implemented)
void IRAM_ATTR onWatchdogTimeout() {
    // This would be called by hardware watchdog
    // Flag for main loop to handle restart
}

// Power management handler
void IRAM_ATTR onPowerEvent() {
    // Handle power supply events
    // Low voltage detection, etc.
}
