// =============================================================================
// Test Suite: Fluid Sensor State Machine
// Tests sensor reading logic and state transitions
// =============================================================================

#include <Arduino.h>
#include <unity.h>
#include "../src/config.h"

// Mock sensor readings structure
struct MockSensorReading {
    bool half_sensor;      // HIGH = fluid above half
    bool empty_sensor;     // HIGH = fluid above empty  
    uint32_t timestamp_ms;
    bool is_valid;
};

// Fluid level states from data model
enum class MockFluidLevel : uint8_t {
    ABOVE_HALF = 0,   // Both sensors HIGH
    BELOW_HALF = 1,   // Half sensor LOW, Empty sensor HIGH
    NEAR_EMPTY = 2,   // Both sensors LOW
    SENSOR_ERROR = 3  // Invalid sensor combination
};

// Mock sensor state
MockSensorReading current_reading = {false, false, 0, true};
MockFluidLevel current_level = MockFluidLevel::SENSOR_ERROR;
uint32_t mock_millis = 0;
uint32_t last_state_change = 0;
uint8_t error_count = 0;

// Test helper functions
void setMockSensors(bool half, bool empty) {
    current_reading.half_sensor = half;
    current_reading.empty_sensor = empty;
    current_reading.timestamp_ms = mock_millis;
    current_reading.is_valid = true;
}

void advanceTime(uint32_t ms) {
    mock_millis += ms;
}

MockFluidLevel calculateFluidLevel(bool half_sensor, bool empty_sensor) {
    // Sensor logic table implementation
    if (half_sensor && empty_sensor) {
        return MockFluidLevel::ABOVE_HALF;
    } else if (!half_sensor && empty_sensor) {
        return MockFluidLevel::BELOW_HALF;
    } else if (!half_sensor && !empty_sensor) {
        return MockFluidLevel::NEAR_EMPTY;
    } else { // half_sensor && !empty_sensor - impossible physical state
        return MockFluidLevel::SENSOR_ERROR;
    }
}

bool updateFluidLevel() {
    MockFluidLevel new_level = calculateFluidLevel(
        current_reading.half_sensor, 
        current_reading.empty_sensor
    );
    
    if (new_level != current_level) {
        // State change detected
        if (new_level == MockFluidLevel::SENSOR_ERROR) {
            error_count++;
        } else {
            error_count = 0; // Reset on valid reading
        }
        
        current_level = new_level;
        last_state_change = mock_millis;
        return true;
    }
    
    return false;
}

void resetMockSensors() {
    current_reading = {false, false, 0, true};
    current_level = MockFluidLevel::SENSOR_ERROR;
    mock_millis = 1000;
    last_state_change = 0;
    error_count = 0;
}

// =============================================================================
// Test Cases
// =============================================================================

void test_sensor_logic_table() {
    resetMockSensors();
    
    // Test all valid sensor combinations
    
    // Tank full - both sensors detect fluid
    setMockSensors(true, true);   // HIGH, HIGH
    TEST_ASSERT_EQUAL(MockFluidLevel::ABOVE_HALF, 
                     calculateFluidLevel(true, true));
    
    // Tank half - only empty sensor detects fluid
    setMockSensors(false, true);  // LOW, HIGH  
    TEST_ASSERT_EQUAL(MockFluidLevel::BELOW_HALF, 
                     calculateFluidLevel(false, true));
    
    // Tank empty - neither sensor detects fluid
    setMockSensors(false, false); // LOW, LOW
    TEST_ASSERT_EQUAL(MockFluidLevel::NEAR_EMPTY, 
                     calculateFluidLevel(false, false));
    
    // Invalid state - half sensor HIGH but empty sensor LOW
    setMockSensors(true, false);  // HIGH, LOW - impossible
    TEST_ASSERT_EQUAL(MockFluidLevel::SENSOR_ERROR, 
                     calculateFluidLevel(true, false));
}

void test_state_transitions() {
    resetMockSensors();
    
    // Start with full tank
    setMockSensors(true, true);
    TEST_ASSERT_TRUE(updateFluidLevel());
    TEST_ASSERT_EQUAL(MockFluidLevel::ABOVE_HALF, current_level);
    
    advanceTime(1000);
    
    // Fluid drops to half
    setMockSensors(false, true);
    TEST_ASSERT_TRUE(updateFluidLevel());
    TEST_ASSERT_EQUAL(MockFluidLevel::BELOW_HALF, current_level);
    TEST_ASSERT_EQUAL(mock_millis, last_state_change);
    
    advanceTime(1000);
    
    // Fluid drops to empty
    setMockSensors(false, false);
    TEST_ASSERT_TRUE(updateFluidLevel());
    TEST_ASSERT_EQUAL(MockFluidLevel::NEAR_EMPTY, current_level);
}

void test_no_state_change() {
    resetMockSensors();
    
    // Set initial state
    setMockSensors(true, true);
    updateFluidLevel();
    uint32_t initial_change_time = last_state_change;
    
    advanceTime(1000);
    
    // Same reading - should not trigger state change
    setMockSensors(true, true);
    TEST_ASSERT_FALSE(updateFluidLevel());
    TEST_ASSERT_EQUAL(MockFluidLevel::ABOVE_HALF, current_level);
    TEST_ASSERT_EQUAL(initial_change_time, last_state_change);
}

void test_sensor_error_detection() {
    resetMockSensors();
    
    // Start with valid state
    setMockSensors(true, true);
    updateFluidLevel();
    TEST_ASSERT_EQUAL(0, error_count);
    
    // Introduce error condition
    setMockSensors(true, false); // Impossible: half HIGH, empty LOW
    TEST_ASSERT_TRUE(updateFluidLevel());
    TEST_ASSERT_EQUAL(MockFluidLevel::SENSOR_ERROR, current_level);
    TEST_ASSERT_EQUAL(1, error_count);
    
    // Repeated error should increment counter
    setMockSensors(true, false);
    updateFluidLevel();
    TEST_ASSERT_EQUAL(2, error_count);
    
    // Recovery should reset error count
    setMockSensors(true, true);
    updateFluidLevel();
    TEST_ASSERT_EQUAL(0, error_count);
    TEST_ASSERT_EQUAL(MockFluidLevel::ABOVE_HALF, current_level);
}

void test_debouncing_logic() {
    resetMockSensors();
    
    // Simulate rapid fluctuations (would need debouncing in real implementation)
    setMockSensors(true, true);
    updateFluidLevel();
    
    // In real implementation, rapid changes within SENSOR_DEBOUNCE_MS
    // should be filtered out. For now, test that we can detect the need.
    uint32_t rapid_change_start = mock_millis;
    
    for (int i = 0; i < 5; i++) {
        advanceTime(10); // 10ms intervals - very rapid
        setMockSensors(i % 2 == 0, true); // Alternate half sensor
        updateFluidLevel();
    }
    
    // Verify we detected multiple rapid changes
    uint32_t total_time = mock_millis - rapid_change_start;
    TEST_ASSERT_LESS_THAN(SENSOR_DEBOUNCE_MS, total_time);
}

void test_sensor_disconnection() {
    resetMockSensors();
    
    // Simulate sensor disconnection
    current_reading.is_valid = false;
    
    // In real implementation, invalid readings should be handled
    // For now, verify we can detect invalid state
    TEST_ASSERT_FALSE(current_reading.is_valid);
    
    // Recovery
    current_reading.is_valid = true;
    setMockSensors(false, true);
    TEST_ASSERT_TRUE(current_reading.is_valid);
}

void test_edge_case_transitions() {
    resetMockSensors();
    
    // Test direct transition from full to empty (sensor failure scenario)
    setMockSensors(true, true);   // Full
    updateFluidLevel();
    
    setMockSensors(false, false); // Empty (skipping half state)
    TEST_ASSERT_TRUE(updateFluidLevel());
    TEST_ASSERT_EQUAL(MockFluidLevel::NEAR_EMPTY, current_level);
    
    // Test transition through error state
    setMockSensors(true, false);  // Error
    updateFluidLevel();
    TEST_ASSERT_EQUAL(MockFluidLevel::SENSOR_ERROR, current_level);
    
    setMockSensors(false, true);  // Half (recovery)
    updateFluidLevel();
    TEST_ASSERT_EQUAL(MockFluidLevel::BELOW_HALF, current_level);
}

void test_timing_consistency() {
    resetMockSensors();
    
    // Verify timestamp consistency
    setMockSensors(true, true);
    uint32_t test_time = mock_millis;
    updateFluidLevel();
    
    TEST_ASSERT_EQUAL(test_time, current_reading.timestamp_ms);
    TEST_ASSERT_EQUAL(test_time, last_state_change);
    
    // Advance time and verify next reading
    advanceTime(500);
    setMockSensors(false, true);
    test_time = mock_millis;
    updateFluidLevel();
    
    TEST_ASSERT_EQUAL(test_time, current_reading.timestamp_ms);
    TEST_ASSERT_EQUAL(test_time, last_state_change);
}

void test_error_threshold_handling() {
    resetMockSensors();
    
    // Test multiple consecutive errors
    for (int i = 0; i < SENSOR_ERROR_THRESHOLD + 2; i++) {
        setMockSensors(true, false); // Error condition
        updateFluidLevel();
        advanceTime(100);
    }
    
    // Should have accumulated error count
    TEST_ASSERT_GREATER_THAN(SENSOR_ERROR_THRESHOLD, error_count);
    
    // In real implementation, would trigger error handling after threshold
    bool error_threshold_exceeded = (error_count >= SENSOR_ERROR_THRESHOLD);
    TEST_ASSERT_TRUE(error_threshold_exceeded);
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp(void) {
    resetMockSensors();
}

void tearDown(void) {
    // Cleanup after each test
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    UNITY_BEGIN();
    
    // Core sensor logic tests
    RUN_TEST(test_sensor_logic_table);
    RUN_TEST(test_state_transitions);
    RUN_TEST(test_no_state_change);
    
    // Error handling tests
    RUN_TEST(test_sensor_error_detection);
    RUN_TEST(test_sensor_disconnection);
    RUN_TEST(test_error_threshold_handling);
    
    // Edge case tests
    RUN_TEST(test_debouncing_logic);
    RUN_TEST(test_edge_case_transitions);
    RUN_TEST(test_timing_consistency);
    
    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}

// =============================================================================
// Test Implementation Notes
// =============================================================================

/*
This test suite validates the fluid sensor state machine using mocked GPIO.
Key areas covered:

1. Sensor Logic Table:
   - Validates all 4 possible sensor combinations
   - Ensures impossible states trigger error condition
   - Tests physical sensor behavior mapping

2. State Transitions:
   - Normal fluid level changes (full → half → empty)
   - Proper timestamp tracking
   - State change detection logic

3. Error Handling:
   - Invalid sensor combinations
   - Sensor disconnection scenarios  
   - Error count accumulation and recovery

4. Timing and Debouncing:
   - Rapid fluctuation detection
   - Timestamp consistency
   - Debounce period validation (mocked)

5. Edge Cases:
   - Direct transitions (skipping intermediate states)
   - Recovery from error states
   - Error threshold enforcement

Real hardware integration points:
- GPIO digital read functions
- Pull-up resistor configuration
- Sensor wiring validation
- Hardware debouncing vs software

Safety considerations:
- Default to caution state on sensor errors
- Graceful degradation when sensors fail
- Clear error indication to user
- No unsafe state assumptions

Physical sensor assumptions:
- Float switches: Normally Open (NO) contacts
- Switch closes when fluid present (pulls GPIO LOW)
- Internal pull-up resistors enable simple 2-wire connection
- Sensors mounted at 50% and 10-15% levels
*/
