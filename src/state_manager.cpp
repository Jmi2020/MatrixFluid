#include "state_manager.h"
#include "sensors/fluid_sensors.h"
#include "sensors/accelerometer.h"
#include "display/led_controller.h"
#include "detection/tap_detector.h"

// =============================================================================
// System State Manager Implementation
// Coordinates all subsystems and manages state transitions
// =============================================================================

class StateManager {
private:
    // Subsystem instances
    FluidSensors fluid_sensors;
    Accelerometer accelerometer;
    LEDController led_controller;
    TapDetector tap_detector;
    
    // System state
    SystemState current_state;
    SystemConfig config;
    PerformanceMetrics metrics;
    
    // Timing
    uint32_t last_update_time;
    uint32_t loop_start_time;
    
    bool initialized;
    bool diagnostics_enabled;

public:
    StateManager() : initialized(false), diagnostics_enabled(DEBUG_ENABLED) {
        config.setDefaults();
        current_state.reset();
        metrics.reset();
    }
    
    bool begin() {
        // Initialize all subsystems
        if (!fluid_sensors.begin()) return false;
        if (!accelerometer.begin()) return false;
        if (!led_controller.begin()) return false;
        
        tap_detector.begin();
        
        // Self-test sequence
        led_controller.showSelfTest();
        
        initialized = true;
        last_update_time = millis();
        
        if (diagnostics_enabled) {
            Serial.println("[STATE] System initialized successfully");
        }
        
        return true;
    }
    
    void update() {
        if (!initialized) return;
        
        loop_start_time = micros();
        
        // Update subsystems
        fluid_sensors.update();
        accelerometer.update();
        led_controller.update();
        
        // Check for tap detection
        if (accelerometer.checkForTap()) {
            AccelData accel_data = accelerometer.getLastReading();
            TapEvent tap_event(accel_data.timestamp_ms, accel_data.magnitude, 0x07);
            
            TapResult result = tap_detector.processTap(tap_event);
            if (result == TapResult::TRIPLE_TAP) {
                handleTripleTap();
            }
        }
        
        // Update system state
        updateSystemState();
        
        // Performance tracking
        uint32_t loop_time = micros() - loop_start_time;
        metrics.updateLoopTiming(loop_time);
        
        last_update_time = millis();
    }
    
private:
    void handleTripleTap() {
        FluidLevel level = fluid_sensors.getCurrentLevel();
        
        switch (level) {
            case FluidLevel::ABOVE_HALF:
                led_controller.showGoodLevel();
                break;
            case FluidLevel::BELOW_HALF:
                led_controller.showCautionLevel();
                break;
            case FluidLevel::NEAR_EMPTY:
                led_controller.showLowLevel();
                break;
            case FluidLevel::SENSOR_ERROR:
            default:
                led_controller.showErrorState();
                break;
        }
        
        current_state.total_activations++;
        current_state.last_activation_ms = millis();
        
        if (diagnostics_enabled) {
            Serial.printf("[STATE] Triple-tap detected - showing %s\n", 
                         ModelUtils::fluidLevelToString(level).c_str());
        }
    }
    
    void updateSystemState() {
        current_state.updateUptime();
        current_state.fluid_level = fluid_sensors.getCurrentLevel();
        current_state.display_state = led_controller.getCurrentState();
        current_state.display_active = led_controller.isActive();
        current_state.tap_detection_armed = tap_detector.isDetectionArmed();
        current_state.sensors_valid = !fluid_sensors.hasError();
        current_state.accelerometer_status = true; // Simplified
    }

public:
    const SystemState& getSystemState() const { return current_state; }
    const SystemConfig& getSystemConfig() const { return config; }
    const PerformanceMetrics& getPerformanceMetrics() const { return metrics; }
};

// Global instance
StateManager g_state_manager;

// Public interface functions
bool initializeSystem() {
    return g_state_manager.begin();
}

void updateSystem() {
    g_state_manager.update();
}

const SystemState& getSystemState() {
    return g_state_manager.getSystemState();
}
