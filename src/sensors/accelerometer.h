#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <Arduino.h>
#include <Wire.h>
#include "../config.h"

// =============================================================================
// QMI8658 Accelerometer Interface
// Triple-tap detection with vibration filtering for ESP32-S3-Matrix
// =============================================================================

// Tap event structure (matches data model)
struct TapEvent {
    uint32_t timestamp_ms;  // millis() when detected
    float magnitude_g;      // Peak acceleration in g
    uint8_t axis_mask;     // Which axes triggered (bit flags: X=1, Y=2, Z=4)
    
    TapEvent() : timestamp_ms(0), magnitude_g(0.0f), axis_mask(0) {}
    TapEvent(uint32_t ts, float mag, uint8_t axes) : timestamp_ms(ts), magnitude_g(mag), axis_mask(axes) {}
};

// Triple-tap detector state machine
struct TapDetector {
    TapEvent tap_buffer[TAP_REQUIRED_COUNT];  // Circular buffer for tap sequence
    uint8_t tap_count;                        // Current taps in sequence
    uint32_t window_start_ms;                 // First tap timestamp
    bool detection_armed;                     // Ready to detect taps
    uint32_t last_detection_ms;               // Last successful detection
    uint32_t last_tap_ms;                     // Last individual tap
    
    TapDetector() : tap_count(0), window_start_ms(0), detection_armed(true), 
                   last_detection_ms(0), last_tap_ms(0) {}
};

// QMI8658 register definitions
namespace QMI8658_REG {
    constexpr uint8_t WHO_AM_I = 0x00;
    constexpr uint8_t REVISION_ID = 0x01;
    constexpr uint8_t CTRL1 = 0x02;
    constexpr uint8_t CTRL2 = 0x03;
    constexpr uint8_t CTRL3 = 0x04;
    constexpr uint8_t CTRL4 = 0x05;
    constexpr uint8_t CTRL5 = 0x06;
    constexpr uint8_t CTRL6 = 0x07;
    constexpr uint8_t CTRL7 = 0x08;
    constexpr uint8_t CTRL8 = 0x09;
    constexpr uint8_t CTRL9 = 0x0A;
    constexpr uint8_t CAL1_L = 0x0B;
    constexpr uint8_t CAL1_H = 0x0C;
    constexpr uint8_t CAL2_L = 0x0D;
    constexpr uint8_t CAL2_H = 0x0E;
    constexpr uint8_t CAL3_L = 0x0F;
    constexpr uint8_t CAL3_H = 0x10;
    constexpr uint8_t CAL4_L = 0x11;
    constexpr uint8_t CAL4_H = 0x12;
    constexpr uint8_t FIFO_WTM_TH = 0x13;
    constexpr uint8_t FIFO_CTRL = 0x14;
    constexpr uint8_t FIFO_SMPL_CNT = 0x15;
    constexpr uint8_t FIFO_STATUS = 0x16;
    constexpr uint8_t FIFO_DATA = 0x17;
    constexpr uint8_t I2CM_STATUS = 0x2C;
    constexpr uint8_t STATUSINT = 0x2D;
    constexpr uint8_t STATUS0 = 0x2E;
    constexpr uint8_t STATUS1 = 0x2F;
    constexpr uint8_t TIMESTAMP_LOW = 0x30;
    constexpr uint8_t TIMESTAMP_MID = 0x31;
    constexpr uint8_t TIMESTAMP_HIGH = 0x32;
    constexpr uint8_t TEMP_L = 0x33;
    constexpr uint8_t TEMP_H = 0x34;
    constexpr uint8_t AX_L = 0x35;
    constexpr uint8_t AX_H = 0x36;
    constexpr uint8_t AY_L = 0x37;
    constexpr uint8_t AY_H = 0x38;
    constexpr uint8_t AZ_L = 0x39;
    constexpr uint8_t AZ_H = 0x3A;
    constexpr uint8_t GX_L = 0x3B;
    constexpr uint8_t GX_H = 0x3C;
    constexpr uint8_t GY_L = 0x3D;
    constexpr uint8_t GY_H = 0x3E;
    constexpr uint8_t GZ_L = 0x3F;
    constexpr uint8_t GZ_H = 0x40;
    
    // Expected WHO_AM_I value
    constexpr uint8_t WHO_AM_I_VALUE = 0x05;
}

// Accelerometer raw data structure
struct AccelData {
    int16_t x_raw, y_raw, z_raw;
    float x_g, y_g, z_g;
    uint32_t timestamp_ms;
    float magnitude;
    bool is_valid;
    
    AccelData() : x_raw(0), y_raw(0), z_raw(0), x_g(0), y_g(0), z_g(0), 
                 timestamp_ms(0), magnitude(0), is_valid(false) {}
};

class Accelerometer {
private:
    // Hardware state
    bool initialized;
    bool interrupt_enabled;
    uint8_t device_address;
    
    // Tap detection
    TapDetector tap_detector;
    AccelData last_reading;
    float baseline_magnitude;
    uint32_t last_update_time;
    
    // Calibration and filtering
    float offset_x, offset_y, offset_z;
    float scale_factor;
    bool calibration_complete;
    
    // Interrupt handling
    volatile bool interrupt_triggered;
    uint32_t last_interrupt_time;
    
    // Internal methods
    bool initializeDevice();
    bool readRawData(AccelData& data);
    void convertToG(AccelData& data);
    bool detectTap(const AccelData& data);
    void updateBaseline(float magnitude);
    bool validateReading(const AccelData& data);
    void handleInterrupt();
    
    // I2C communication
    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    bool readMultipleRegisters(uint8_t start_reg, uint8_t* buffer, uint8_t count);

public:
    // Constructor and initialization
    Accelerometer();
    bool begin();
    void end();
    
    // Main interface
    bool update();                          // Call regularly to check for taps
    bool checkForTap();                     // Check if triple-tap detected
    void resetTapDetector();                // Clear tap detection state
    
    // Data access
    AccelData getLastReading() const;       // Most recent accelerometer data
    TapEvent getLastTap() const;            // Most recent tap event
    uint8_t getTapCount() const;            // Current taps in sequence
    bool isDetectionArmed() const;          // Ready to detect taps
    
    // Configuration
    void setTapThreshold(float threshold_g); // Adjust sensitivity
    void setTapWindow(uint16_t window_ms);   // Adjust timing window
    float getTapThreshold() const;
    uint16_t getTapWindow() const;
    
    // Calibration
    void startCalibration();                // Begin calibration sequence
    bool isCalibrating() const;             // Calibration in progress
    void setCalibrationOffsets(float x, float y, float z);
    void getCalibrationOffsets(float& x, float& y, float& z) const;
    
    // Diagnostics
    bool selfTest();                        // Hardware self-test
    float getTemperature() const;           // Die temperature
    String getDeviceInfo() const;           // Device ID and revision
    void printDiagnostics() const;          // Debug output
    void enableDiagnostics(bool enable);    // Control debug output
    
    // Interrupt handling (static for ISR)
    static void IRAM_ATTR interruptHandler();
    void attachInterrupt();
    void detachInterrupt();
    
    // Testing support
    void simulateTap(float magnitude_g, uint8_t axis_mask = 0x07);
    void injectTestData(float x_g, float y_g, float z_g);
    bool getInterruptStatus() const;

private:
    bool diagnostics_enabled;
    uint32_t last_diagnostic_time;
    static Accelerometer* instance; // For ISR access
    
    // Configuration parameters
    float tap_threshold_g;
    uint16_t tap_window_ms;
    
    // Vibration filtering
    float vibration_filter_alpha;    // Low-pass filter coefficient
    float filtered_magnitude;       // Filtered magnitude for baseline
    uint32_t vibration_reject_count; // Number of rejected rapid taps
};

// Utility functions
float calculateMagnitude(float x, float y, float z);
bool isWithinTapWindow(uint32_t first_tap, uint32_t current_tap, uint16_t window_ms);
uint8_t getAxisMask(float x, float y, float z, float threshold);
const char* axisesToString(uint8_t axis_mask);

// Interrupt service routine (must be in IRAM)
void IRAM_ATTR accel_interrupt_isr();

#endif // ACCELEROMETER_H
