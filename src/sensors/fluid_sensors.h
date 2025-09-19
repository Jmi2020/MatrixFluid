#ifndef FLUID_SENSORS_H
#define FLUID_SENSORS_H

#include <Arduino.h>
#include "../config.h"

// =============================================================================
// Fluid Level Sensors Interface
// GPIO-based float switch reading with debouncing and validation
// =============================================================================

// Fluid level enumeration (matches data model)
enum class FluidLevel : uint8_t {
    ABOVE_HALF = 0,   // Both sensors HIGH (fluid present at both levels)
    BELOW_HALF = 1,   // Half sensor LOW, Empty sensor HIGH
    NEAR_EMPTY = 2,   // Both sensors LOW (no fluid at either level)
    SENSOR_ERROR = 3  // Invalid sensor combination (half HIGH, empty LOW)
};

// Sensor reading structure
struct SensorReading {
    bool half_sensor;       // true = fluid above half level
    bool empty_sensor;      // true = fluid above empty level
    uint32_t timestamp_ms;  // Reading timestamp
    bool is_valid;         // false if read error or disconnection
    
    SensorReading() : half_sensor(false), empty_sensor(false), timestamp_ms(0), is_valid(false) {}
};

// Sensor history for debouncing and trend analysis
struct SensorHistory {
    SensorReading readings[SENSOR_HISTORY_SIZE];
    uint8_t write_index;
    uint8_t count;
    
    SensorHistory() : write_index(0), count(0) {}
};

class FluidSensors {
private:
    // Sensor state
    SensorReading current_reading;
    FluidLevel current_level;
    SensorHistory history;
    
    // Debouncing state
    uint32_t last_read_time;
    uint32_t last_stable_time;
    FluidLevel pending_level;
    bool state_changing;
    
    // Error tracking
    uint8_t consecutive_errors;
    uint32_t last_error_time;
    bool error_recovery_mode;
    
    // Internal methods
    bool readRawSensors();
    FluidLevel calculateLevel(bool half, bool empty);
    void addToHistory(const SensorReading& reading);
    bool isStableReading(FluidLevel level);
    void handleSensorError();
    void validateSensorLogic(const SensorReading& reading);

public:
    // Constructor and initialization
    FluidSensors();
    void begin();
    
    // Main interface
    bool update();                          // Call regularly to update sensor state
    FluidLevel getCurrentLevel() const;     // Get current debounced fluid level
    SensorReading getLastReading() const;   // Get most recent raw reading
    bool hasError() const;                  // Check if sensors are in error state
    
    // Status and diagnostics
    uint32_t getLastChangeTime() const;     // When level last changed
    uint8_t getErrorCount() const;          // Number of consecutive errors
    float getStabilityScore() const;        // How stable recent readings are (0-1)
    String getLevelString() const;          // Human-readable level name
    
    // Testing and calibration
    void forceSensorTest();                 // Trigger self-test sequence
    SensorReading simulateReading(bool half, bool empty); // For testing
    void resetErrorCount();                 // Clear error counter
    
    // Advanced features
    bool isLevelChanging() const;           // Detect fluid level in transition
    uint32_t getTimeSinceChange() const;    // Time since last stable change
    void enableDiagnostics(bool enable);    // Debug output control
    
private:
    bool diagnostics_enabled;
    uint32_t last_diagnostic_time;
    void printDiagnostics();
};

// Utility functions
const char* fluidLevelToString(FluidLevel level);
bool isValidSensorCombination(bool half, bool empty);
FluidLevel sensorCombinationToLevel(bool half, bool empty);

// Constants for sensor logic validation
namespace SensorLogic {
    // Valid sensor combinations (sensor HIGH = fluid present)
    constexpr bool TANK_FULL_HALF = true;    // Fluid above half level
    constexpr bool TANK_FULL_EMPTY = true;   // Fluid above empty level
    
    constexpr bool TANK_HALF_HALF = false;   // No fluid at half level
    constexpr bool TANK_HALF_EMPTY = true;   // Fluid still above empty
    
    constexpr bool TANK_EMPTY_HALF = false;  // No fluid at half level
    constexpr bool TANK_EMPTY_EMPTY = false; // No fluid at empty level
    
    // Invalid combination (physically impossible)
    constexpr bool TANK_ERROR_HALF = true;   // Fluid at half...
    constexpr bool TANK_ERROR_EMPTY = false; // ...but not at empty (impossible)
}

#endif // FLUID_SENSORS_H
