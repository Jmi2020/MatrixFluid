#include "accelerometer.h"

// =============================================================================
// QMI8658 Accelerometer Implementation
// Triple-tap detection with vibration filtering
// =============================================================================

// Static instance for ISR access
Accelerometer* Accelerometer::instance = nullptr;

Accelerometer::Accelerometer() :
    initialized(false),
    interrupt_enabled(false),
    device_address(ACCEL_I2C_ADDR),
    baseline_magnitude(1.0f), // 1g baseline (gravity)
    last_update_time(0),
    offset_x(0), offset_y(0), offset_z(0),
    scale_factor(1.0f),
    calibration_complete(false),
    interrupt_triggered(false),
    last_interrupt_time(0),
    diagnostics_enabled(DEBUG_ENABLED),
    last_diagnostic_time(0),
    tap_threshold_g(TAP_THRESHOLD_G),
    tap_window_ms(TAP_WINDOW_MS),
    vibration_filter_alpha(0.1f),
    filtered_magnitude(1.0f),
    vibration_reject_count(0)
{
    tap_detector = TapDetector();
    last_reading = AccelData();
    instance = this; // Set static instance for ISR
}

bool Accelerometer::begin() {
    // Initialize I2C
    Wire.begin(ACCEL_I2C_SDA, ACCEL_I2C_SCL);
    Wire.setClock(400000); // 400kHz I2C
    
    delay(10); // Allow device to power up
    
    // Initialize QMI8658 device
    if (!initializeDevice()) {
        if (diagnostics_enabled) {
            Serial.println("[ACCEL] ERROR: Failed to initialize QMI8658");
        }
        return false;
    }
    
    // Setup interrupt pin
    pinMode(ACCEL_INT_PIN, INPUT_PULLUP);
    
    // Attach interrupt
    attachInterrupt();
    
    // Initial reading to establish baseline
    AccelData initial_data;
    if (readRawData(initial_data)) {
        convertToG(initial_data);
        baseline_magnitude = initial_data.magnitude;
        filtered_magnitude = baseline_magnitude;
        last_reading = initial_data;
    }
    
    initialized = true;
    
    if (diagnostics_enabled) {
        Serial.println("[ACCEL] QMI8658 accelerometer initialized");
        Serial.printf("[ACCEL] Baseline magnitude: %.2fg\n", baseline_magnitude);
        printDiagnostics();
    }
    
    return true;
}

void Accelerometer::end() {
    detachInterrupt();
    initialized = false;
    instance = nullptr;
}

bool Accelerometer::initializeDevice() {
    // Check device ID
    uint8_t who_am_i = readRegister(QMI8658_REG::WHO_AM_I);
    if (who_am_i != QMI8658_REG::WHO_AM_I_VALUE) {
        if (diagnostics_enabled) {
            Serial.printf("[ACCEL] ERROR: Wrong device ID. Expected 0x%02X, got 0x%02X\n", 
                         QMI8658_REG::WHO_AM_I_VALUE, who_am_i);
        }
        return false;
    }
    
    // Reset device
    writeRegister(QMI8658_REG::CTRL1, 0x80); // Software reset
    delay(10);
    
    // Configure accelerometer
    // CTRL2: Enable accelerometer, 1000Hz ODR, ±8g range
    writeRegister(QMI8658_REG::CTRL2, 0x95);
    
    // CTRL3: Disable gyroscope to save power
    writeRegister(QMI8658_REG::CTRL3, 0x00);
    
    // CTRL1: Enable sensors
    writeRegister(QMI8658_REG::CTRL1, 0x01);
    
    delay(10); // Allow settings to take effect
    
    return true;
}

bool Accelerometer::update() {
    if (!initialized) return false;
    
    uint32_t now = millis();
    
    // Rate limiting
    if (now - last_update_time < 10) { // 100Hz max update rate
        return false;
    }
    
    last_update_time = now;
    
    // Check for interrupt-driven updates
    if (interrupt_triggered) {
        interrupt_triggered = false;
        
        AccelData data;
        if (readRawData(data)) {
            convertToG(data);
            
            if (validateReading(data)) {
                last_reading = data;
                updateBaseline(data.magnitude);
                
                // Check for tap detection
                if (detectTap(data)) {
                    if (diagnostics_enabled) {
                        Serial.printf("[ACCEL] Tap detected: %.2fg on axes %s\n", 
                                     data.magnitude, axisesToString(getAxisMask(data.x_g, data.y_g, data.z_g, tap_threshold_g)));
                    }
                }
                
                return true;
            }
        }
    }
    
    // Periodic diagnostics
    if (diagnostics_enabled && now - last_diagnostic_time > 30000) { // Every 30 seconds
        printDiagnostics();
        last_diagnostic_time = now;
    }
    
    return false;
}

bool Accelerometer::readRawData(AccelData& data) {
    uint8_t raw_data[6];
    
    if (!readMultipleRegisters(QMI8658_REG::AX_L, raw_data, 6)) {
        data.is_valid = false;
        return false;
    }
    
    // Combine LSB and MSB for each axis
    data.x_raw = (int16_t)((raw_data[1] << 8) | raw_data[0]);
    data.y_raw = (int16_t)((raw_data[3] << 8) | raw_data[2]);
    data.z_raw = (int16_t)((raw_data[5] << 8) | raw_data[4]);
    data.timestamp_ms = millis();
    data.is_valid = true;
    
    return true;
}

void Accelerometer::convertToG(AccelData& data) {
    // Convert raw data to g (±8g range, 16-bit resolution)
    const float lsb_per_g = 4096.0f; // 32768 / 8g
    
    data.x_g = (float)data.x_raw / lsb_per_g;
    data.y_g = (float)data.y_raw / lsb_per_g;
    data.z_g = (float)data.z_raw / lsb_per_g;
    
    // Apply calibration offsets
    data.x_g -= offset_x;
    data.y_g -= offset_y;
    data.z_g -= offset_z;
    
    // Calculate magnitude
    data.magnitude = calculateMagnitude(data.x_g, data.y_g, data.z_g);
}

bool Accelerometer::detectTap(const AccelData& data) {
    // Check if magnitude exceeds threshold
    float delta_magnitude = abs(data.magnitude - baseline_magnitude);
    
    if (delta_magnitude < tap_threshold_g) {
        return false; // Below threshold
    }
    
    uint32_t now = data.timestamp_ms;
    
    // Check for vibration rejection (too rapid taps)
    if (tap_detector.last_tap_ms > 0 && 
        now - tap_detector.last_tap_ms < MIN_TAP_INTERVAL_MS) {
        vibration_reject_count++;
        
        if (diagnostics_enabled && vibration_reject_count % 10 == 0) {
            Serial.printf("[ACCEL] Vibration rejected: %lu rapid taps\n", vibration_reject_count);
        }
        return false;
    }
    
    // Check lockout period
    if (!tap_detector.detection_armed && 
        now - tap_detector.last_detection_ms < TAP_LOCKOUT_MS) {
        return false; // Still in lockout period
    }
    
    // Valid tap detected
    TapEvent tap_event(now, delta_magnitude, getAxisMask(data.x_g, data.y_g, data.z_g, tap_threshold_g));
    
    // Add to tap buffer
    tap_detector.tap_buffer[tap_detector.tap_count % TAP_REQUIRED_COUNT] = tap_event;
    tap_detector.last_tap_ms = now;
    
    if (tap_detector.tap_count == 0) {
        // First tap in sequence
        tap_detector.window_start_ms = now;
        tap_detector.detection_armed = true;
    }
    
    tap_detector.tap_count++;
    
    // Check for triple-tap completion
    if (tap_detector.tap_count >= TAP_REQUIRED_COUNT) {
        uint32_t window_duration = now - tap_detector.window_start_ms;
        
        if (window_duration <= tap_window_ms) {
            // Valid triple-tap sequence detected!
            tap_detector.last_detection_ms = now;
            tap_detector.detection_armed = false; // Enter lockout
            tap_detector.tap_count = 0; // Reset for next sequence
            
            if (diagnostics_enabled) {
                Serial.printf("[ACCEL] TRIPLE-TAP DETECTED! Window: %lums\n", window_duration);
            }
            
            return true;
        } else {
            // Window expired - start new sequence with this tap
            tap_detector.tap_count = 1;
            tap_detector.window_start_ms = now;
            tap_detector.tap_buffer[0] = tap_event;
        }
    }
    
    return false;
}

void Accelerometer::updateBaseline(float magnitude) {
    // Low-pass filter for baseline tracking
    filtered_magnitude = (vibration_filter_alpha * magnitude) + 
                        ((1.0f - vibration_filter_alpha) * filtered_magnitude);
    
    // Update baseline with heavily filtered value
    if (abs(magnitude - baseline_magnitude) < 0.5f) { // Only update during stable periods
        baseline_magnitude = (baseline_magnitude * 0.99f) + (filtered_magnitude * 0.01f);
    }
}

bool Accelerometer::validateReading(const AccelData& data) {
    // Sanity checks for accelerometer data
    if (data.magnitude > 20.0f) return false;  // >20g is likely noise
    if (data.magnitude < 0.1f) return false;   // <0.1g is likely disconnected
    
    return true;
}

// =============================================================================
// Public Interface Methods
// =============================================================================

bool Accelerometer::checkForTap() {
    // Check if triple-tap was detected in recent update
    return (tap_detector.last_detection_ms > 0 && 
            millis() - tap_detector.last_detection_ms < 100);
}

void Accelerometer::resetTapDetector() {
    tap_detector = TapDetector();
    
    if (diagnostics_enabled) {
        Serial.println("[ACCEL] Tap detector reset");
    }
}

AccelData Accelerometer::getLastReading() const {
    return last_reading;
}

TapEvent Accelerometer::getLastTap() const {
    if (tap_detector.tap_count > 0) {
        uint8_t last_index = (tap_detector.tap_count - 1) % TAP_REQUIRED_COUNT;
        return tap_detector.tap_buffer[last_index];
    }
    return TapEvent();
}

uint8_t Accelerometer::getTapCount() const {
    return tap_detector.tap_count;
}

bool Accelerometer::isDetectionArmed() const {
    return tap_detector.detection_armed;
}

void Accelerometer::setTapThreshold(float threshold_g) {
    tap_threshold_g = constrain(threshold_g, 0.5f, 5.0f);
}

void Accelerometer::setTapWindow(uint16_t window_ms) {
    tap_window_ms = constrain(window_ms, 100, 2000);
}

float Accelerometer::getTapThreshold() const {
    return tap_threshold_g;
}

uint16_t Accelerometer::getTapWindow() const {
    return tap_window_ms;
}

void Accelerometer::attachInterrupt() {
    if (!interrupt_enabled) {
        attachInterrupt(digitalPinToInterrupt(ACCEL_INT_PIN), accel_interrupt_isr, RISING);
        interrupt_enabled = true;
    }
}

void Accelerometer::detachInterrupt() {
    if (interrupt_enabled) {
        detachInterrupt(digitalPinToInterrupt(ACCEL_INT_PIN));
        interrupt_enabled = false;
    }
}

void Accelerometer::enableDiagnostics(bool enable) {
    diagnostics_enabled = enable;
}

void Accelerometer::printDiagnostics() const {
    Serial.println("=== ACCELEROMETER DIAGNOSTICS ===");
    Serial.printf("Initialized: %s\n", initialized ? "YES" : "NO");
    Serial.printf("Baseline: %.2fg (filtered: %.2fg)\n", baseline_magnitude, filtered_magnitude);
    Serial.printf("Last reading: X=%.2f Y=%.2f Z=%.2f |M|=%.2f\n", 
                 last_reading.x_g, last_reading.y_g, last_reading.z_g, last_reading.magnitude);
    Serial.printf("Tap threshold: %.2fg, Window: %dms\n", tap_threshold_g, tap_window_ms);
    Serial.printf("Tap count: %d/%d, Armed: %s\n", 
                 tap_detector.tap_count, TAP_REQUIRED_COUNT, 
                 tap_detector.detection_armed ? "YES" : "NO");
    Serial.printf("Vibration rejects: %lu\n", vibration_reject_count);
    Serial.printf("Last detection: %lums ago\n", 
                 tap_detector.last_detection_ms > 0 ? millis() - tap_detector.last_detection_ms : 0);
    Serial.println("=================================");
}

// =============================================================================
// I2C Communication
// =============================================================================

bool Accelerometer::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(device_address);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}

uint8_t Accelerometer::readRegister(uint8_t reg) {
    Wire.beginTransmission(device_address);
    Wire.write(reg);
    Wire.endTransmission(false);
    
    Wire.requestFrom(device_address, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

bool Accelerometer::readMultipleRegisters(uint8_t start_reg, uint8_t* buffer, uint8_t count) {
    Wire.beginTransmission(device_address);
    Wire.write(start_reg);
    Wire.endTransmission(false);
    
    Wire.requestFrom(device_address, count);
    
    for (uint8_t i = 0; i < count; i++) {
        if (Wire.available()) {
            buffer[i] = Wire.read();
        } else {
            return false;
        }
    }
    
    return true;
}

// =============================================================================
// Interrupt Handling
// =============================================================================

void IRAM_ATTR Accelerometer::interruptHandler() {
    if (instance) {
        instance->interrupt_triggered = true;
        instance->last_interrupt_time = millis();
    }
}

void IRAM_ATTR accel_interrupt_isr() {
    if (Accelerometer::instance) {
        Accelerometer::instance->interruptHandler();
    }
}

// =============================================================================
// Utility Functions
// =============================================================================

float calculateMagnitude(float x, float y, float z) {
    return sqrt(x*x + y*y + z*z);
}

bool isWithinTapWindow(uint32_t first_tap, uint32_t current_tap, uint16_t window_ms) {
    return (current_tap - first_tap) <= window_ms;
}

uint8_t getAxisMask(float x, float y, float z, float threshold) {
    uint8_t mask = 0;
    if (abs(x) >= threshold) mask |= 0x01; // X axis
    if (abs(y) >= threshold) mask |= 0x02; // Y axis  
    if (abs(z) >= threshold) mask |= 0x04; // Z axis
    return mask;
}

const char* axisesToString(uint8_t axis_mask) {
    static char buffer[8];
    buffer[0] = '\0';
    
    if (axis_mask & 0x01) strcat(buffer, "X");
    if (axis_mask & 0x02) strcat(buffer, "Y");
    if (axis_mask & 0x04) strcat(buffer, "Z");
    
    return buffer[0] ? buffer : "None";
}
