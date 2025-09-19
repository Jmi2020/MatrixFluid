#ifndef TAP_DETECTOR_H
#define TAP_DETECTOR_H

#include <Arduino.h>
#include "../config.h"
#include "../sensors/accelerometer.h"

// =============================================================================
// Triple-Tap Detection Algorithm
// High-level tap detection with configurable parameters and robust filtering
// =============================================================================

// Tap detection result
enum class TapResult : uint8_t {
    NO_TAP = 0,           // No tap detected
    SINGLE_TAP = 1,       // First tap in sequence
    DOUBLE_TAP = 2,       // Second tap detected
    TRIPLE_TAP = 3,       // Triple-tap sequence complete!
    TAP_TIMEOUT = 4,      // Sequence timed out
    TAP_REJECTED = 5      // Tap rejected (vibration, too fast, etc.)
};

// Tap sequence state
struct TapSequence {
    TapEvent taps[TAP_REQUIRED_COUNT];  // Recorded tap events
    uint8_t count;                      // Current tap count in sequence
    uint32_t sequence_start_ms;         // First tap timestamp
    bool is_active;                     // Sequence in progress
    uint32_t last_tap_ms;               // Most recent tap time
    
    TapSequence() : count(0), sequence_start_ms(0), is_active(false), last_tap_ms(0) {}
};

// Detection statistics
struct TapStatistics {
    uint32_t total_taps;                // All taps detected
    uint32_t valid_sequences;           // Successful triple-taps
    uint32_t timeout_sequences;         // Sequences that timed out
    uint32_t rejected_taps;             // Vibration/noise rejections
    uint32_t false_positives;           // Accidental activations
    float average_sequence_time_ms;     // Average successful sequence duration
    
    TapStatistics() : total_taps(0), valid_sequences(0), timeout_sequences(0),
                     rejected_taps(0), false_positives(0), average_sequence_time_ms(0) {}
};

class TapDetector {
private:
    // Configuration
    float tap_threshold_g;
    uint16_t tap_window_ms;
    uint16_t min_tap_interval_ms;
    uint16_t lockout_period_ms;
    
    // State management
    TapSequence current_sequence;
    uint32_t last_detection_ms;
    bool detection_armed;
    bool lockout_active;
    
    // Filtering and validation
    float baseline_magnitude;
    float vibration_threshold;
    uint32_t vibration_window_ms;
    uint8_t max_rapid_taps;
    
    // Statistics and diagnostics
    TapStatistics stats;
    bool diagnostics_enabled;
    uint32_t last_diagnostic_time;
    
    // Internal methods
    bool validateTapEvent(const TapEvent& tap);
    bool isVibrationPattern(const TapEvent& tap);
    void updateBaseline(float magnitude);
    TapResult addTapToSequence(const TapEvent& tap);
    void resetSequence();
    void updateStatistics(TapResult result);
    void printSequenceDiagnostics();

public:
    // Constructor and initialization
    TapDetector();
    void begin();
    
    // Main interface
    TapResult processTap(const TapEvent& tap);   // Process incoming tap event
    bool isTripleTapDetected();                  // Check for recent triple-tap
    void reset();                                // Reset detection state
    
    // Configuration
    void setTapThreshold(float threshold_g);     // Sensitivity adjustment
    void setTapWindow(uint16_t window_ms);       // Timing window
    void setMinInterval(uint16_t interval_ms);   // Anti-vibration interval
    void setLockoutPeriod(uint16_t period_ms);   // Post-detection lockout
    
    float getTapThreshold() const;
    uint16_t getTapWindow() const;
    uint16_t getMinInterval() const;
    uint16_t getLockoutPeriod() const;
    
    // State information
    uint8_t getCurrentTapCount() const;          // Taps in current sequence
    bool isSequenceActive() const;               // Detection in progress
    bool isDetectionArmed() const;               // Ready to detect
    uint32_t getSequenceAge() const;             // Time since first tap
    
    // Advanced features
    void setVibrationThreshold(float threshold); // Vibration rejection sensitivity
    void enableAdaptiveBaseline(bool enable);    // Dynamic baseline tracking
    void setFalsePositiveFilter(bool enable);    // Additional validation
    
    // Statistics and diagnostics
    const TapStatistics& getStatistics() const; // Performance metrics
    void resetStatistics();                      // Clear counters
    float getSuccessRate() const;                // Valid sequences / total attempts
    String getStatusString() const;              // Human-readable status
    void printDiagnostics() const;               // Debug output
    void enableDiagnostics(bool enable);         // Control debug output
    
    // Testing and calibration
    void simulateTripleTap();                    // Generate test sequence
    void injectTapEvent(float magnitude_g, uint32_t timestamp_ms = 0);
    bool testSequenceTiming();                   // Validate timing parameters
    void calibrateThresholds();                  // Auto-calibration mode

private:
    // Advanced filtering
    bool adaptive_baseline_enabled;
    bool false_positive_filter_enabled;
    float adaptation_rate;
    uint32_t baseline_samples;
    
    // Calibration support
    bool calibration_mode;
    uint32_t calibration_start_time;
    float calibration_min_magnitude;
    float calibration_max_magnitude;
    
    // Timing validation
    bool isTimingValid(uint32_t first_tap, uint32_t last_tap) const;
    bool isIntervalValid(uint32_t prev_tap, uint32_t curr_tap) const;
    
    // Pattern analysis
    bool analyzeSequencePattern(const TapSequence& sequence);
    float calculateSequenceScore(const TapSequence& sequence);
    
    // Helper methods
    void logTapEvent(const TapEvent& tap, TapResult result);
    String tapResultToString(TapResult result) const;
};

// Utility functions for tap analysis
namespace TapAnalysis {
    float calculateTapMagnitude(const TapEvent& tap);
    bool isReasonableTapInterval(uint32_t interval_ms);
    bool isHumanTapPattern(const TapSequence& sequence);
    float estimateVibrationLevel(const TapEvent& taps[], uint8_t count);
    uint8_t countRapidTaps(const TapEvent& taps[], uint8_t count, uint16_t threshold_ms);
}

// Constants for tap detection tuning
namespace TapTuning {
    // Default thresholds (from config.h)
    constexpr float DEFAULT_THRESHOLD_G = TAP_THRESHOLD_G;
    constexpr uint16_t DEFAULT_WINDOW_MS = TAP_WINDOW_MS;
    constexpr uint16_t DEFAULT_MIN_INTERVAL_MS = MIN_TAP_INTERVAL_MS;
    constexpr uint16_t DEFAULT_LOCKOUT_MS = TAP_LOCKOUT_MS;
    
    // Vibration rejection
    constexpr float VIBRATION_THRESHOLD_SCALE = 2.0f;  // 2x normal threshold
    constexpr uint16_t VIBRATION_WINDOW_MS = 100;      // Rapid tap window
    constexpr uint8_t MAX_RAPID_TAPS = 5;               // Reject if too many rapid taps
    
    // Adaptive parameters
    constexpr float BASELINE_ADAPTATION_RATE = 0.01f;  // 1% per sample
    constexpr uint32_t BASELINE_STABILIZATION_TIME = 5000; // 5 seconds
    
    // Success criteria
    constexpr float MIN_SUCCESS_RATE = 0.8f;           // 80% success for good tuning
    constexpr uint16_t MAX_SEQUENCE_TIME_MS = 2000;    // 2 second max for natural tapping
}

#endif // TAP_DETECTOR_H
