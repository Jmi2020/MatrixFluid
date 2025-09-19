#include "fluid_sensors.h"

// =============================================================================
// Fluid Level Sensors Implementation
// GPIO-based float switch reading with debouncing and validation
// =============================================================================

FluidSensors::FluidSensors() :
    current_level(FluidLevel::SENSOR_ERROR),
    last_read_time(0),
    last_stable_time(0),
    pending_level(FluidLevel::SENSOR_ERROR),
    state_changing(false),
    consecutive_errors(0),
    last_error_time(0),
    error_recovery_mode(false),
    diagnostics_enabled(DEBUG_ENABLED),
    last_diagnostic_time(0)
{
    current_reading = SensorReading();
    history = SensorHistory();
}

void FluidSensors::begin() {
    // Configure GPIO pins for sensor inputs
    pinMode(HALF_SENSOR_PIN, INPUT_PULLUP);   // Enable internal pull-up
    pinMode(EMPTY_SENSOR_PIN, INPUT_PULLUP);  // Enable internal pull-up
    
    // Initial reading to establish baseline
    delay(100); // Allow pull-ups to stabilize
    
    if (readRawSensors()) {
        current_level = calculateLevel(current_reading.half_sensor, current_reading.empty_sensor);
        pending_level = current_level;
        last_stable_time = millis();
        
        if (diagnostics_enabled) {
            Serial.println("[SENSORS] Fluid sensors initialized");
            printDiagnostics();
        }
    } else {
        current_level = FluidLevel::SENSOR_ERROR;
        handleSensorError();
        
        if (diagnostics_enabled) {
            Serial.println("[SENSORS] ERROR: Failed to initialize sensors");
        }
    }
}

bool FluidSensors::update() {
    uint32_t now = millis();
    
    // Rate limiting for sensor reads
    if (now - last_read_time < SENSOR_READ_INTERVAL_MS) {
        return false; // No update needed yet
    }
    
    last_read_time = now;
    
    // Read sensors
    if (!readRawSensors()) {
        handleSensorError();
        return false;
    }
    
    // Calculate new level from raw reading
    FluidLevel new_level = calculateLevel(current_reading.half_sensor, current_reading.empty_sensor);
    
    // Add to history for trend analysis
    addToHistory(current_reading);
    
    // Handle level changes with debouncing
    if (new_level != current_level) {
        if (!state_changing) {
            // Start of new state change
            pending_level = new_level;
            state_changing = true;
            last_stable_time = now;
            
            if (diagnostics_enabled) {
                Serial.printf("[SENSORS] Level change detected: %s -> %s (pending)\n", 
                             fluidLevelToString(current_level), 
                             fluidLevelToString(new_level));
            }
        } else if (pending_level == new_level) {
            // Consistent with pending change
            if (now - last_stable_time >= SENSOR_DEBOUNCE_MS) {
                // Debounce period complete - commit change
                current_level = new_level;
                state_changing = false;
                consecutive_errors = 0; // Reset error count on successful change
                
                if (diagnostics_enabled) {
                    Serial.printf("[SENSORS] Level change confirmed: %s\n", 
                                 fluidLevelToString(current_level));
                }
                return true; // Level changed
            }
        } else {
            // Reading changed again during debounce - restart
            pending_level = new_level;
            last_stable_time = now;
        }
    } else {
        // Reading matches current level
        if (state_changing && pending_level == current_level) {
            // Cancel pending change - returned to stable state
            state_changing = false;
            
            if (diagnostics_enabled) {
                Serial.println("[SENSORS] Level change cancelled - returned to stable state");
            }
        }
    }
    
    // Periodic diagnostics
    if (diagnostics_enabled && now - last_diagnostic_time > 10000) { // Every 10 seconds
        printDiagnostics();
        last_diagnostic_time = now;
    }
    
    return false; // No level change
}

bool FluidSensors::readRawSensors() {
    uint32_t now = millis();
    
    // Read GPIO pins (sensors are active LOW with pull-ups)
    bool half_raw = !digitalRead(HALF_SENSOR_PIN);   // Invert: LOW = switch closed = fluid present
    bool empty_raw = !digitalRead(EMPTY_SENSOR_PIN); // Invert: LOW = switch closed = fluid present
    
    // Create new reading
    SensorReading reading;
    reading.half_sensor = half_raw;
    reading.empty_sensor = empty_raw;
    reading.timestamp_ms = now;
    reading.is_valid = true;
    
    // Validate sensor logic
    validateSensorLogic(reading);
    
    if (reading.is_valid) {
        current_reading = reading;
        return true;
    }
    
    return false;
}

FluidLevel FluidSensors::calculateLevel(bool half, bool empty) {
    // Implement sensor logic table from research.md
    if (half && empty) {
        return FluidLevel::ABOVE_HALF;   // Both sensors detect fluid
    } else if (!half && empty) {
        return FluidLevel::BELOW_HALF;   // Only empty sensor has fluid
    } else if (!half && !empty) {
        return FluidLevel::NEAR_EMPTY;   // Neither sensor has fluid
    } else { // half && !empty - physically impossible
        return FluidLevel::SENSOR_ERROR; // Half has fluid but empty doesn't
    }
}

void FluidSensors::addToHistory(const SensorReading& reading) {
    history.readings[history.write_index] = reading;
    history.write_index = (history.write_index + 1) % SENSOR_HISTORY_SIZE;
    
    if (history.count < SENSOR_HISTORY_SIZE) {
        history.count++;
    }
}

void FluidSensors::validateSensorLogic(SensorReading& reading) {
    // Check for physically impossible sensor combinations
    if (reading.half_sensor && !reading.empty_sensor) {
        // Fluid at half level but not at empty level - impossible
        reading.is_valid = false;
        
        if (diagnostics_enabled) {
            Serial.println("[SENSORS] ERROR: Invalid sensor combination (half=HIGH, empty=LOW)");
        }
    }
    
    // Additional validation could include:
    // - Checking for sensor disconnection (both pins floating HIGH)
    // - Rate-of-change validation (fluid can't change too quickly)
    // - Historical consistency checks
}

void FluidSensors::handleSensorError() {
    consecutive_errors++;
    last_error_time = millis();
    
    if (consecutive_errors >= SENSOR_ERROR_THRESHOLD) {
        error_recovery_mode = true;
        
        if (diagnostics_enabled) {
            Serial.printf("[SENSORS] ERROR: %d consecutive errors - entering recovery mode\n", 
                         consecutive_errors);
        }
        
        // In recovery mode, default to caution state for safety
        current_level = FluidLevel::SENSOR_ERROR;
    }
}

// =============================================================================
// Public Interface Methods
// =============================================================================

FluidLevel FluidSensors::getCurrentLevel() const {
    return current_level;
}

SensorReading FluidSensors::getLastReading() const {
    return current_reading;
}

bool FluidSensors::hasError() const {
    return (current_level == FluidLevel::SENSOR_ERROR) || 
           (consecutive_errors >= SENSOR_ERROR_THRESHOLD);
}

uint32_t FluidSensors::getLastChangeTime() const {
    return last_stable_time;
}

uint8_t FluidSensors::getErrorCount() const {
    return consecutive_errors;
}

float FluidSensors::getStabilityScore() const {
    if (history.count < 3) return 0.0f;
    
    // Calculate stability based on recent reading consistency
    FluidLevel last_level = calculateLevel(history.readings[0].half_sensor, 
                                          history.readings[0].empty_sensor);
    uint8_t consistent_count = 1;
    
    for (uint8_t i = 1; i < history.count; i++) {
        FluidLevel level = calculateLevel(history.readings[i].half_sensor, 
                                        history.readings[i].empty_sensor);
        if (level == last_level) {
            consistent_count++;
        }
    }
    
    return (float)consistent_count / (float)history.count;
}

String FluidSensors::getLevelString() const {
    return String(fluidLevelToString(current_level));
}

bool FluidSensors::isLevelChanging() const {
    return state_changing;
}

uint32_t FluidSensors::getTimeSinceChange() const {
    return millis() - last_stable_time;
}

void FluidSensors::enableDiagnostics(bool enable) {
    diagnostics_enabled = enable;
}

void FluidSensors::forceSensorTest() {
    if (diagnostics_enabled) {
        Serial.println("[SENSORS] Starting self-test sequence...");
        printDiagnostics();
        
        // Test GPIO pin states
        Serial.printf("[SENSORS] Raw GPIO states - Half:%d Empty:%d\n", 
                     digitalRead(HALF_SENSOR_PIN), digitalRead(EMPTY_SENSOR_PIN));
        
        // Test all logic combinations
        Serial.println("[SENSORS] Testing sensor logic:");
        Serial.printf("  FULL   (H=1,E=1): %s\n", fluidLevelToString(calculateLevel(true, true)));
        Serial.printf("  HALF   (H=0,E=1): %s\n", fluidLevelToString(calculateLevel(false, true)));
        Serial.printf("  EMPTY  (H=0,E=0): %s\n", fluidLevelToString(calculateLevel(false, false)));
        Serial.printf("  ERROR  (H=1,E=0): %s\n", fluidLevelToString(calculateLevel(true, false)));
    }
}

SensorReading FluidSensors::simulateReading(bool half, bool empty) {
    SensorReading sim_reading;
    sim_reading.half_sensor = half;
    sim_reading.empty_sensor = empty;
    sim_reading.timestamp_ms = millis();
    sim_reading.is_valid = isValidSensorCombination(half, empty);
    
    return sim_reading;
}

void FluidSensors::resetErrorCount() {
    consecutive_errors = 0;
    error_recovery_mode = false;
    
    if (diagnostics_enabled) {
        Serial.println("[SENSORS] Error count reset - exiting recovery mode");
    }
}

void FluidSensors::printDiagnostics() {
    Serial.println("=== FLUID SENSOR DIAGNOSTICS ===");
    Serial.printf("Current Level: %s\n", fluidLevelToString(current_level));
    Serial.printf("Raw Sensors: Half=%s Empty=%s\n", 
                 current_reading.half_sensor ? "HIGH" : "LOW",
                 current_reading.empty_sensor ? "HIGH" : "LOW");
    Serial.printf("State: %s\n", state_changing ? "CHANGING" : "STABLE");
    Serial.printf("Errors: %d/%d\n", consecutive_errors, SENSOR_ERROR_THRESHOLD);
    Serial.printf("Stability: %.1f%%\n", getStabilityScore() * 100.0f);
    Serial.printf("Time since change: %lu ms\n", getTimeSinceChange());
    Serial.printf("History count: %d/%d\n", history.count, SENSOR_HISTORY_SIZE);
    Serial.println("================================");
}

// =============================================================================
// Utility Functions
// =============================================================================

const char* fluidLevelToString(FluidLevel level) {
    switch (level) {
        case FluidLevel::ABOVE_HALF:  return "ABOVE_HALF";
        case FluidLevel::BELOW_HALF:  return "BELOW_HALF";
        case FluidLevel::NEAR_EMPTY:  return "NEAR_EMPTY";
        case FluidLevel::SENSOR_ERROR: return "SENSOR_ERROR";
        default: return "UNKNOWN";
    }
}

bool isValidSensorCombination(bool half, bool empty) {
    // Check if sensor combination is physically possible
    // Invalid: half sensor HIGH but empty sensor LOW (impossible)
    return !(half && !empty);
}

FluidLevel sensorCombinationToLevel(bool half, bool empty) {
    if (!isValidSensorCombination(half, empty)) {
        return FluidLevel::SENSOR_ERROR;
    }
    
    if (half && empty) return FluidLevel::ABOVE_HALF;
    if (!half && empty) return FluidLevel::BELOW_HALF;
    if (!half && !empty) return FluidLevel::NEAR_EMPTY;
    
    return FluidLevel::SENSOR_ERROR; // Should not reach here
}
