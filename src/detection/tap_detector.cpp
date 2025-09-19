#include "tap_detector.h"

// =============================================================================
// Triple-Tap Detection Algorithm Implementation
// High-level tap detection with configurable parameters and robust filtering
// =============================================================================

TapDetector::TapDetector() :
    tap_threshold_g(TapTuning::DEFAULT_THRESHOLD_G),
    tap_window_ms(TapTuning::DEFAULT_WINDOW_MS),
    min_tap_interval_ms(TapTuning::DEFAULT_MIN_INTERVAL_MS),
    lockout_period_ms(TapTuning::DEFAULT_LOCKOUT_MS),
    last_detection_ms(0),
    detection_armed(true),
    lockout_active(false),
    baseline_magnitude(1.0f),
    vibration_threshold(tap_threshold_g * TapTuning::VIBRATION_THRESHOLD_SCALE),
    vibration_window_ms(TapTuning::VIBRATION_WINDOW_MS),
    max_rapid_taps(TapTuning::MAX_RAPID_TAPS),
    diagnostics_enabled(DEBUG_ENABLED),
    last_diagnostic_time(0),
    adaptive_baseline_enabled(false),
    false_positive_filter_enabled(true),
    adaptation_rate(TapTuning::BASELINE_ADAPTATION_RATE),
    baseline_samples(0),
    calibration_mode(false),
    calibration_start_time(0),
    calibration_min_magnitude(999.0f),
    calibration_max_magnitude(0.0f)
{
    current_sequence = TapSequence();
    stats = TapStatistics();
}

void TapDetector::begin() {
    reset();
    
    if (diagnostics_enabled) {
        Serial.println("[TAP] Tap detector initialized");
        Serial.printf("[TAP] Threshold: %.2fg, Window: %dms, Min interval: %dms\n",
                     tap_threshold_g, tap_window_ms, min_tap_interval_ms);
    }
}

TapResult TapDetector::processTap(const TapEvent& tap) {
    uint32_t now = tap.timestamp_ms;
    
    // Check lockout period
    if (lockout_active && now - last_detection_ms < lockout_period_ms) {
        updateStatistics(TapResult::TAP_REJECTED);
        
        if (diagnostics_enabled) {
            Serial.printf("[TAP] Tap rejected - lockout active (%lums remaining)\n",
                         lockout_period_ms - (now - last_detection_ms));
        }
        return TapResult::TAP_REJECTED;
    }
    
    // Clear lockout if period has expired
    if (lockout_active && now - last_detection_ms >= lockout_period_ms) {
        lockout_active = false;
        detection_armed = true;
        
        if (diagnostics_enabled) {
            Serial.println("[TAP] Lockout period expired - detection armed");
        }
    }
    
    // Validate tap event
    if (!validateTapEvent(tap)) {
        updateStatistics(TapResult::TAP_REJECTED);
        return TapResult::TAP_REJECTED;
    }
    
    // Check for vibration pattern
    if (isVibrationPattern(tap)) {
        updateStatistics(TapResult::TAP_REJECTED);
        
        if (diagnostics_enabled) {
            Serial.printf("[TAP] Tap rejected - vibration pattern (%.2fg)\n", tap.magnitude_g);
        }
        return TapResult::TAP_REJECTED;
    }
    
    // Update baseline tracking
    updateBaseline(tap.magnitude_g);
    
    // Add tap to current sequence
    TapResult result = addTapToSequence(tap);
    
    // Update statistics
    updateStatistics(result);
    stats.total_taps++;
    
    // Handle triple-tap detection
    if (result == TapResult::TRIPLE_TAP) {
        last_detection_ms = now;
        lockout_active = true;
        detection_armed = false;
        stats.valid_sequences++;
        
        // Calculate average sequence time
        uint32_t sequence_duration = now - current_sequence.sequence_start_ms;
        stats.average_sequence_time_ms = 
            (stats.average_sequence_time_ms * (stats.valid_sequences - 1) + sequence_duration) 
            / stats.valid_sequences;
        
        if (diagnostics_enabled) {
            Serial.printf("[TAP] TRIPLE-TAP DETECTED! Sequence duration: %lums\n", sequence_duration);
            printSequenceDiagnostics();
        }
        
        resetSequence();
    }
    
    // Log tap event for diagnostics
    if (diagnostics_enabled) {
        logTapEvent(tap, result);
    }
    
    return result;
}

bool TapDetector::validateTapEvent(const TapEvent& tap) {
    // Check magnitude threshold
    if (tap.magnitude_g < tap_threshold_g) {
        return false;
    }
    
    // Check timestamp validity
    if (tap.timestamp_ms == 0) {
        return false;
    }
    
    // Check for reasonable magnitude (not sensor noise)
    if (tap.magnitude_g > 20.0f) { // 20g is unreasonable for hand tapping
        return false;
    }
    
    // Check minimum interval since last tap
    if (current_sequence.last_tap_ms > 0 && 
        tap.timestamp_ms - current_sequence.last_tap_ms < min_tap_interval_ms) {
        return false;
    }
    
    return true;
}

bool TapDetector::isVibrationPattern(const TapEvent& tap) {
    // Check if magnitude is too high (likely vibration)
    if (tap.magnitude_g > vibration_threshold) {
        return true;
    }
    
    // Count rapid taps in recent history to detect vibration
    if (current_sequence.count > 0) {
        uint8_t rapid_taps = 0;
        uint32_t check_window_start = tap.timestamp_ms - vibration_window_ms;
        
        for (uint8_t i = 0; i < current_sequence.count; i++) {
            if (current_sequence.taps[i].timestamp_ms >= check_window_start) {
                rapid_taps++;
            }
        }
        
        if (rapid_taps >= max_rapid_taps) {
            return true;
        }
    }
    
    return false;
}

void TapDetector::updateBaseline(float magnitude) {
    if (adaptive_baseline_enabled) {
        baseline_magnitude = (baseline_magnitude * (1.0f - adaptation_rate)) + 
                            (magnitude * adaptation_rate);
        baseline_samples++;
    }
}

TapResult TapDetector::addTapToSequence(const TapEvent& tap) {
    uint32_t now = tap.timestamp_ms;
    
    // Check for sequence timeout
    if (current_sequence.is_active && 
        now - current_sequence.sequence_start_ms > tap_window_ms) {
        
        if (diagnostics_enabled) {
            Serial.printf("[TAP] Sequence timeout after %lums\n", 
                         now - current_sequence.sequence_start_ms);
        }
        
        resetSequence();
        stats.timeout_sequences++;
    }
    
    // Start new sequence if none active
    if (!current_sequence.is_active) {
        current_sequence.sequence_start_ms = now;
        current_sequence.is_active = true;
        current_sequence.count = 0;
    }
    
    // Add tap to sequence
    current_sequence.taps[current_sequence.count] = tap;
    current_sequence.count++;
    current_sequence.last_tap_ms = now;
    
    // Determine result based on tap count
    TapResult result;
    switch (current_sequence.count) {
        case 1:
            result = TapResult::SINGLE_TAP;
            break;
        case 2:
            result = TapResult::DOUBLE_TAP;
            break;
        case 3:
            // Validate triple-tap sequence
            if (analyzeSequencePattern(current_sequence)) {
                result = TapResult::TRIPLE_TAP;
            } else {
                // Pattern validation failed - reset and start over
                resetSequence();
                result = TapResult::TAP_REJECTED;
                stats.false_positives++;
            }
            break;
        default:
            // Too many taps - reset sequence
            resetSequence();
            result = TapResult::TAP_REJECTED;
            break;
    }
    
    return result;
}

void TapDetector::resetSequence() {
    current_sequence = TapSequence();
}

bool TapDetector::analyzeSequencePattern(const TapSequence& sequence) {
    if (sequence.count != TAP_REQUIRED_COUNT) return false;
    
    // Check overall timing
    uint32_t total_duration = sequence.taps[2].timestamp_ms - sequence.taps[0].timestamp_ms;
    if (total_duration > tap_window_ms) {
        return false;
    }
    
    // Check individual intervals
    for (uint8_t i = 1; i < sequence.count; i++) {
        uint32_t interval = sequence.taps[i].timestamp_ms - sequence.taps[i-1].timestamp_ms;
        if (interval < min_tap_interval_ms) {
            return false; // Too rapid
        }
    }
    
    // Additional pattern validation if enabled
    if (false_positive_filter_enabled) {
        // Check magnitude consistency (taps should be reasonably similar)
        float min_mag = sequence.taps[0].magnitude_g;
        float max_mag = sequence.taps[0].magnitude_g;
        
        for (uint8_t i = 1; i < sequence.count; i++) {
            min_mag = min(min_mag, sequence.taps[i].magnitude_g);
            max_mag = max(max_mag, sequence.taps[i].magnitude_g);
        }
        
        // Reject if magnitude variation is too large (likely mixed sources)
        if (max_mag / min_mag > 3.0f) {
            return false;
        }
    }
    
    return true;
}

void TapDetector::updateStatistics(TapResult result) {
    switch (result) {
        case TapResult::TAP_REJECTED:
            stats.rejected_taps++;
            break;
        case TapResult::TAP_TIMEOUT:
            stats.timeout_sequences++;
            break;
        default:
            break;
    }
}

// =============================================================================
// Public Interface Methods
// =============================================================================

bool TapDetector::isTripleTapDetected() {
    return lockout_active && (millis() - last_detection_ms < 100);
}

void TapDetector::reset() {
    resetSequence();
    last_detection_ms = 0;
    detection_armed = true;
    lockout_active = false;
    baseline_magnitude = 1.0f;
    baseline_samples = 0;
}

void TapDetector::setTapThreshold(float threshold_g) {
    tap_threshold_g = constrain(threshold_g, 0.5f, 5.0f);
    vibration_threshold = tap_threshold_g * TapTuning::VIBRATION_THRESHOLD_SCALE;
    
    if (diagnostics_enabled) {
        Serial.printf("[TAP] Threshold updated: %.2fg\n", tap_threshold_g);
    }
}

void TapDetector::setTapWindow(uint16_t window_ms) {
    tap_window_ms = constrain(window_ms, 200, 2000);
    
    if (diagnostics_enabled) {
        Serial.printf("[TAP] Window updated: %dms\n", tap_window_ms);
    }
}

void TapDetector::setMinInterval(uint16_t interval_ms) {
    min_tap_interval_ms = constrain(interval_ms, 50, 500);
}

void TapDetector::setLockoutPeriod(uint16_t period_ms) {
    lockout_period_ms = constrain(period_ms, 500, 5000);
}

float TapDetector::getTapThreshold() const {
    return tap_threshold_g;
}

uint16_t TapDetector::getTapWindow() const {
    return tap_window_ms;
}

uint16_t TapDetector::getMinInterval() const {
    return min_tap_interval_ms;
}

uint16_t TapDetector::getLockoutPeriod() const {
    return lockout_period_ms;
}

uint8_t TapDetector::getCurrentTapCount() const {
    return current_sequence.count;
}

bool TapDetector::isSequenceActive() const {
    return current_sequence.is_active;
}

bool TapDetector::isDetectionArmed() const {
    return detection_armed;
}

uint32_t TapDetector::getSequenceAge() const {
    if (current_sequence.is_active) {
        return millis() - current_sequence.sequence_start_ms;
    }
    return 0;
}

const TapStatistics& TapDetector::getStatistics() const {
    return stats;
}

void TapDetector::resetStatistics() {
    stats = TapStatistics();
}

float TapDetector::getSuccessRate() const {
    uint32_t total_attempts = stats.valid_sequences + stats.timeout_sequences + stats.false_positives;
    if (total_attempts == 0) return 0.0f;
    
    return (float)stats.valid_sequences / (float)total_attempts;
}

String TapDetector::getStatusString() const {
    String status = "Armed: " + String(detection_armed ? "YES" : "NO");
    status += ", Sequence: " + String(current_sequence.count) + "/" + String(TAP_REQUIRED_COUNT);
    if (lockout_active) {
        uint32_t remaining = lockout_period_ms - (millis() - last_detection_ms);
        status += ", Lockout: " + String(remaining) + "ms";
    }
    return status;
}

void TapDetector::enableDiagnostics(bool enable) {
    diagnostics_enabled = enable;
}

void TapDetector::printDiagnostics() const {
    Serial.println("=== TAP DETECTOR DIAGNOSTICS ===");
    Serial.printf("Status: %s\n", getStatusString().c_str());
    Serial.printf("Threshold: %.2fg, Window: %dms\n", tap_threshold_g, tap_window_ms);
    Serial.printf("Total taps: %lu\n", stats.total_taps);
    Serial.printf("Valid sequences: %lu\n", stats.valid_sequences);
    Serial.printf("Rejected taps: %lu\n", stats.rejected_taps);
    Serial.printf("Timeout sequences: %lu\n", stats.timeout_sequences);
    Serial.printf("False positives: %lu\n", stats.false_positives);
    Serial.printf("Success rate: %.1f%%\n", getSuccessRate() * 100.0f);
    Serial.printf("Avg sequence time: %.1fms\n", stats.average_sequence_time_ms);
    Serial.printf("Baseline magnitude: %.2fg\n", baseline_magnitude);
    Serial.println("================================");
}

void TapDetector::simulateTripleTap() {
    if (diagnostics_enabled) {
        Serial.println("[TAP] Simulating triple-tap sequence...");
    }
    
    uint32_t base_time = millis();
    
    TapEvent tap1(base_time, tap_threshold_g + 0.5f, 0x07);
    TapEvent tap2(base_time + 300, tap_threshold_g + 0.3f, 0x07);
    TapEvent tap3(base_time + 600, tap_threshold_g + 0.4f, 0x07);
    
    processTap(tap1);
    processTap(tap2);
    TapResult result = processTap(tap3);
    
    if (result == TapResult::TRIPLE_TAP) {
        Serial.println("[TAP] Simulation successful!");
    } else {
        Serial.println("[TAP] Simulation failed!");
    }
}

void TapDetector::logTapEvent(const TapEvent& tap, TapResult result) {
    Serial.printf("[TAP] %s: %.2fg at %lums (sequence: %d/%d)\n",
                 tapResultToString(result).c_str(),
                 tap.magnitude_g,
                 tap.timestamp_ms,
                 current_sequence.count,
                 TAP_REQUIRED_COUNT);
}

String TapDetector::tapResultToString(TapResult result) const {
    switch (result) {
        case TapResult::NO_TAP: return "NO_TAP";
        case TapResult::SINGLE_TAP: return "SINGLE";
        case TapResult::DOUBLE_TAP: return "DOUBLE";
        case TapResult::TRIPLE_TAP: return "TRIPLE";
        case TapResult::TAP_TIMEOUT: return "TIMEOUT";
        case TapResult::TAP_REJECTED: return "REJECTED";
        default: return "UNKNOWN";
    }
}

void TapDetector::printSequenceDiagnostics() {
    Serial.println("--- Triple-Tap Sequence ---");
    for (uint8_t i = 0; i < current_sequence.count; i++) {
        const TapEvent& tap = current_sequence.taps[i];
        Serial.printf("  Tap %d: %.2fg at %lums (axes: %s)\n",
                     i + 1,
                     tap.magnitude_g,
                     tap.timestamp_ms,
                     axisesToString(tap.axis_mask));
    }
    Serial.printf("Total duration: %lums\n", 
                 current_sequence.last_tap_ms - current_sequence.sequence_start_ms);
    Serial.println("---------------------------");
}

// =============================================================================
// Utility Functions
// =============================================================================

namespace TapAnalysis {
    float calculateTapMagnitude(const TapEvent& tap) {
        return tap.magnitude_g;
    }
    
    bool isReasonableTapInterval(uint32_t interval_ms) {
        return interval_ms >= MIN_TAP_INTERVAL_MS && interval_ms <= 1000;
    }
    
    bool isHumanTapPattern(const TapSequence& sequence) {
        if (sequence.count < 2) return true;
        
        // Check if intervals are consistent with human tapping
        for (uint8_t i = 1; i < sequence.count; i++) {
            uint32_t interval = sequence.taps[i].timestamp_ms - sequence.taps[i-1].timestamp_ms;
            if (!isReasonableTapInterval(interval)) {
                return false;
            }
        }
        
        return true;
    }
    
    uint8_t countRapidTaps(const TapEvent taps[], uint8_t count, uint16_t threshold_ms) {
        uint8_t rapid_count = 0;
        
        for (uint8_t i = 1; i < count; i++) {
            uint32_t interval = taps[i].timestamp_ms - taps[i-1].timestamp_ms;
            if (interval < threshold_ms) {
                rapid_count++;
            }
        }
        
        return rapid_count;
    }
}
