#include "led_controller.h"

// =============================================================================
// LED Matrix Controller Implementation
// FastLED-based 8x8 WS2812B matrix control with safety features
// =============================================================================

LEDController::LEDController() :
    current_brightness(LED_BRIGHTNESS_DEFAULT),
    initialized(false),
    timeout_active(false),
    last_animation_time(0),
    blink_start_time(0),
    blink_state(false),
    safety_override(false),
    last_thermal_check(0),
    diagnostics_enabled(DEBUG_ENABLED),
    total_on_time(0),
    activation_count(0),
    display_timeout_ms(DISPLAY_TIMEOUT_MS),
    blink_period_ms(ERROR_BLINK_PERIOD_MS),
    animation_speed_ms(100),
    max_on_time_continuous(DisplayTiming::MAX_CONTINUOUS_ON_MS),
    thermal_warning_count(0),
    emergency_shutdown_active(false),
    animation_frame_time(0),
    animation_total_frames(0),
    animation_in_progress(false)
{
    display_status = DisplayStatus();
    memset(current_pattern, 0, sizeof(current_pattern));
}

bool LEDController::begin() {
    // Initialize FastLED
    FastLED.addLeds<WS2812B, LED_PIN, LED_COLOR_ORDER>(leds, LED_COUNT);
    
    // Set initial brightness with safety enforcement
    validateBrightnessSettings();
    FastLED.setBrightness(current_brightness);
    
    // Clear all LEDs
    clearMatrix();
    FastLED.show();
    
    // Verify hardware functionality
    if (!verifyDisplay()) {
        if (diagnostics_enabled) {
            Serial.println("[LED] ERROR: Display verification failed");
        }
        return false;
    }
    
    initialized = true;
    
    if (diagnostics_enabled) {
        Serial.println("[LED] LED matrix controller initialized");
        Serial.printf("[LED] Brightness: %d/%d (%.1f%%)\n", 
                     current_brightness, LED_BRIGHTNESS_MAX, 
                     (float)current_brightness / LED_BRIGHTNESS_MAX * 100.0f);
    }
    
    return true;
}

void LEDController::end() {
    turnOff();
    initialized = false;
}

void LEDController::update() {
    if (!initialized || emergency_shutdown_active) return;
    
    uint32_t now = millis();
    
    // Safety checks
    if (!checkSafetyLimits()) {
        emergencyShutdown();
        return;
    }
    
    // Handle timeout
    handleTimeout();
    
    // Update animations and effects
    if (display_status.is_active) {
        updateAnimation();
        updateBlinking();
        updateMatrix();
        
        // Track on-time
        total_on_time += now - display_status.last_update_time;
    }
    
    display_status.last_update_time = now;
    
    // Periodic thermal monitoring
    if (now - last_thermal_check > 5000) { // Every 5 seconds
        checkThermalCondition();
        last_thermal_check = now;
    }
}

bool LEDController::showPattern(DisplayState state) {
    if (!initialized || emergency_shutdown_active) return false;
    
    // Get the appropriate icon pattern
    const IconPattern* pattern = nullptr;
    bool should_blink = false;
    
    switch (state) {
        case DisplayState::SHOWING_GREEN:
            pattern = &Icons::getGreenCheckmark();
            break;
        case DisplayState::SHOWING_YELLOW:
            pattern = &Icons::getYellowCaution();
            break;
        case DisplayState::SHOWING_RED:
            pattern = &Icons::getRedStop();
            break;
        case DisplayState::SHOWING_ERROR:
            pattern = &Icons::getRedError();
            should_blink = true;
            break;
        case DisplayState::SELF_TEST:
            return startSelfTest();
        case DisplayState::OFF:
        default:
            turnOff();
            return true;
    }
    
    if (!pattern) return false;
    
    // Update display state
    display_status.current_state = state;
    display_status.state_start_time = millis();
    display_status.is_active = true;
    display_status.is_blinking = should_blink;
    display_status.animation_frame = 0;
    
    // Start timeout timer
    timeout_start_time = millis();
    timeout_active = true;
    
    // Set up blinking if needed
    if (should_blink) {
        blink_start_time = millis();
        blink_state = true;
    }
    
    // Apply the pattern
    Icons::copyPattern(*pattern, current_pattern);
    applyPattern(current_pattern);
    
    // Log activation
    logActivation();
    
    if (diagnostics_enabled) {
        Serial.printf("[LED] Showing pattern: %s%s\n", 
                     getStateString().c_str(),
                     should_blink ? " (blinking)" : "");
    }
    
    return true;
}

void LEDController::turnOff() {
    clearMatrix();
    FastLED.show();
    
    display_status.is_active = false;
    display_status.current_state = DisplayState::OFF;
    timeout_active = false;
    
    if (diagnostics_enabled) {
        Serial.println("[LED] Display turned off");
    }
}

void LEDController::updateMatrix() {
    if (!display_status.is_active) return;
    
    // Apply current pattern with brightness scaling
    float brightness_scale = display_status.brightness_scale;
    
    // Handle blinking effect
    if (display_status.is_blinking && !blink_state) {
        brightness_scale = 0.0f; // Off phase of blink
    }
    
    applyPattern(current_pattern, brightness_scale);
    FastLED.show();
}

void LEDController::applyPattern(const IconPattern& pattern, float brightness_scale) {
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            uint8_t index = xyToIndex(x, y);
            const RGBColor& pixel_color = pattern[y][x];
            
            // Convert to CRGB with brightness scaling
            leds[index] = rgbColorToCRGB(pixel_color, brightness_scale);
        }
    }
}

void LEDController::clearMatrix() {
    fill_solid(leds, LED_COUNT, CRGB::Black);
}

bool LEDController::checkSafetyLimits() {
    uint32_t now = millis();
    
    // Check continuous operation time
    if (display_status.is_active && 
        now - display_status.state_start_time > max_on_time_continuous) {
        if (diagnostics_enabled) {
            Serial.println("[LED] WARNING: Maximum continuous operation time exceeded");
        }
        return false;
    }
    
    // Check brightness limits
    if (current_brightness > LED_BRIGHTNESS_MAX && !safety_override) {
        if (diagnostics_enabled) {
            Serial.printf("[LED] ERROR: Brightness %d exceeds safety limit %d\n", 
                         current_brightness, LED_BRIGHTNESS_MAX);
        }
        return false;
    }
    
    return true;
}

void LEDController::handleTimeout() {
    if (!timeout_active || !display_status.is_active) return;
    
    uint32_t now = millis();
    if (now - timeout_start_time >= display_timeout_ms) {
        if (diagnostics_enabled) {
            Serial.printf("[LED] Display timeout after %dms\n", display_timeout_ms);
        }
        turnOff();
    }
}

void LEDController::updateAnimation() {
    // Animation updates would go here for complex sequences
    // Currently used for self-test sequence
    if (!animation_in_progress) return;
    
    uint32_t now = millis();
    if (now - animation_frame_time >= animation_speed_ms) {
        display_status.animation_frame++;
        animation_frame_time = now;
        
        if (display_status.animation_frame >= animation_total_frames) {
            animation_in_progress = false;
            display_status.animation_frame = 0;
        }
    }
}

void LEDController::updateBlinking() {
    if (!display_status.is_blinking) return;
    
    uint32_t now = millis();
    if (now - blink_start_time >= blink_period_ms / 2) {
        blink_state = !blink_state;
        blink_start_time = now;
    }
}

uint8_t LEDController::xyToIndex(uint8_t x, uint8_t y) {
    // Convert 2D coordinates to 1D LED array index
    // Assuming row-major order for simplicity
    // Real hardware might use zigzag pattern for WS2812B strips
    return y * 8 + x;
}

CRGB LEDController::rgbColorToCRGB(const RGBColor& color, float brightness_scale) {
    uint8_t r = (uint8_t)((float)color.r * brightness_scale);
    uint8_t g = (uint8_t)((float)color.g * brightness_scale);
    uint8_t b = (uint8_t)((float)color.b * brightness_scale);
    
    return CRGB(r, g, b);
}

// =============================================================================
// Public Interface Methods
// =============================================================================

bool LEDController::isActive() const {
    return display_status.is_active;
}

bool LEDController::showGoodLevel() {
    return showPattern(DisplayState::SHOWING_GREEN);
}

bool LEDController::showCautionLevel() {
    return showPattern(DisplayState::SHOWING_YELLOW);
}

bool LEDController::showLowLevel() {
    return showPattern(DisplayState::SHOWING_RED);
}

bool LEDController::showErrorState() {
    return showPattern(DisplayState::SHOWING_ERROR);
}

bool LEDController::showSelfTest() {
    if (!initialized) return false;
    
    if (diagnostics_enabled) {
        Serial.println("[LED] Starting self-test sequence...");
    }
    
    // Self-test sequence: show each pattern briefly
    const IconPattern* test_patterns[] = {
        &Icons::getStartupPattern(),
        &Icons::getGreenCheckmark(),
        &Icons::getYellowCaution(),
        &Icons::getRedStop(),
        &Icons::getRedError()
    };
    
    const uint8_t pattern_count = sizeof(test_patterns) / sizeof(test_patterns[0]);
    
    for (uint8_t i = 0; i < pattern_count; i++) {
        Icons::copyPattern(*test_patterns[i], current_pattern);
        applyPattern(current_pattern, 0.7f); // Reduced brightness for test
        FastLED.show();
        delay(SELF_TEST_DURATION_MS);
    }
    
    // End with off state
    turnOff();
    
    if (diagnostics_enabled) {
        Serial.println("[LED] Self-test sequence complete");
    }
    
    return true;
}

void LEDController::setBrightness(uint8_t brightness) {
    // Enforce safety limit
    current_brightness = min(brightness, LED_BRIGHTNESS_MAX);
    
    if (brightness > LED_BRIGHTNESS_MAX && diagnostics_enabled) {
        Serial.printf("[LED] WARNING: Brightness capped at safety limit (%d -> %d)\n", 
                     brightness, current_brightness);
    }
    
    FastLED.setBrightness(current_brightness);
    
    if (diagnostics_enabled) {
        Serial.printf("[LED] Brightness set to %d (%.1f%%)\n", 
                     current_brightness, 
                     (float)current_brightness / 255.0f * 100.0f);
    }
}

uint8_t LEDController::getBrightness() const {
    return current_brightness;
}

void LEDController::setTimeout(uint16_t timeout_ms) {
    display_timeout_ms = constrain(timeout_ms, DISPLAY_MIN_TIMEOUT_MS, 30000);
    
    if (diagnostics_enabled) {
        Serial.printf("[LED] Timeout set to %dms\n", display_timeout_ms);
    }
}

uint16_t LEDController::getTimeout() const {
    return display_timeout_ms;
}

DisplayState LEDController::getCurrentState() const {
    return display_status.current_state;
}

uint32_t LEDController::getTimeActive() const {
    if (display_status.is_active) {
        return millis() - display_status.state_start_time;
    }
    return 0;
}

uint32_t LEDController::getTotalOnTime() const {
    return total_on_time;
}

uint32_t LEDController::getActivationCount() const {
    return activation_count;
}

String LEDController::getStateString() const {
    switch (display_status.current_state) {
        case DisplayState::OFF: return "OFF";
        case DisplayState::SHOWING_GREEN: return "GREEN";
        case DisplayState::SHOWING_YELLOW: return "YELLOW";
        case DisplayState::SHOWING_RED: return "RED";
        case DisplayState::SHOWING_ERROR: return "ERROR";
        case DisplayState::SELF_TEST: return "SELF_TEST";
        default: return "UNKNOWN";
    }
}

void LEDController::enableDiagnostics(bool enable) {
    diagnostics_enabled = enable;
}

void LEDController::printDiagnostics() const {
    Serial.println("=== LED CONTROLLER DIAGNOSTICS ===");
    Serial.printf("State: %s (active: %s)\n", getStateString().c_str(), 
                 display_status.is_active ? "YES" : "NO");
    Serial.printf("Brightness: %d/%d (%.1f%%)\n", current_brightness, LED_BRIGHTNESS_MAX,
                 (float)current_brightness / LED_BRIGHTNESS_MAX * 100.0f);
    Serial.printf("Timeout: %dms (active: %s)\n", display_timeout_ms,
                 timeout_active ? "YES" : "NO");
    Serial.printf("Time active: %lums\n", getTimeActive());
    Serial.printf("Total on-time: %lums\n", total_on_time);
    Serial.printf("Activations: %lu\n", activation_count);
    Serial.printf("Blinking: %s\n", display_status.is_blinking ? "YES" : "NO");
    Serial.printf("Emergency shutdown: %s\n", emergency_shutdown_active ? "YES" : "NO");
    Serial.printf("Thermal warnings: %d\n", thermal_warning_count);
    Serial.println("===================================");
}

void LEDController::testAllPixels() {
    if (!initialized) return;
    
    if (diagnostics_enabled) {
        Serial.println("[LED] Testing all pixels at low brightness...");
    }
    
    // Test red
    fill_solid(leds, LED_COUNT, CRGB(20, 0, 0));
    FastLED.show();
    delay(500);
    
    // Test green
    fill_solid(leds, LED_COUNT, CRGB(0, 20, 0));
    FastLED.show();
    delay(500);
    
    // Test blue
    fill_solid(leds, LED_COUNT, CRGB(0, 0, 20));
    FastLED.show();
    delay(500);
    
    // Test white
    fill_solid(leds, LED_COUNT, CRGB(10, 10, 10));
    FastLED.show();
    delay(500);
    
    // Turn off
    clearMatrix();
    FastLED.show();
    
    if (diagnostics_enabled) {
        Serial.println("[LED] Pixel test complete");
    }
}

bool LEDController::verifyDisplay() {
    // Basic hardware verification
    clearMatrix();
    FastLED.show();
    delay(10);
    
    // Test a few pixels at low brightness
    leds[0] = CRGB(5, 0, 0);    // Top-left red
    leds[7] = CRGB(0, 5, 0);    // Top-right green
    leds[56] = CRGB(0, 0, 5);   // Bottom-left blue
    leds[63] = CRGB(5, 5, 5);   // Bottom-right white
    
    FastLED.show();
    delay(100);
    
    clearMatrix();
    FastLED.show();
    
    return true; // Assume success for now
}

void LEDController::emergencyShutdown() {
    emergency_shutdown_active = true;
    clearMatrix();
    FastLED.show();
    
    if (diagnostics_enabled) {
        Serial.println("[LED] EMERGENCY SHUTDOWN ACTIVATED");
    }
}

void LEDController::enforceHardwareLimits() {
    if (current_brightness > LED_BRIGHTNESS_MAX && !safety_override) {
        current_brightness = LED_BRIGHTNESS_MAX;
        FastLED.setBrightness(current_brightness);
    }
}

void LEDController::logActivation() {
    activation_count++;
}

void LEDController::checkThermalCondition() {
    // Placeholder for thermal monitoring
    // In real hardware, would read temperature sensor
    // For now, just monitor continuous operation time
    
    if (display_status.is_active && 
        millis() - display_status.state_start_time > 30000) { // 30 seconds
        thermal_warning_count++;
        
        if (thermal_warning_count > 3 && diagnostics_enabled) {
            Serial.println("[LED] WARNING: Extended operation detected");
        }
    } else {
        thermal_warning_count = 0; // Reset when not continuously on
    }
}

void LEDController::validateBrightnessSettings() {
    if (current_brightness > LED_BRIGHTNESS_MAX) {
        current_brightness = LED_BRIGHTNESS_MAX;
    }
}

void LEDController::setBlinkRate(uint16_t period_ms) {
    blink_period_ms = constrain(period_ms, 100, 2000);
}

// =============================================================================
// Pattern Utility Functions
// =============================================================================

namespace PatternUtils {
    void rotatePattern(const IconPattern& src, IconPattern& dest, uint8_t rotation) {
        // Rotate pattern 90 degrees clockwise for each rotation count
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 8; x++) {
                switch (rotation % 4) {
                    case 0: // 0 degrees
                        dest[y][x] = src[y][x];
                        break;
                    case 1: // 90 degrees
                        dest[x][7-y] = src[y][x];
                        break;
                    case 2: // 180 degrees
                        dest[7-y][7-x] = src[y][x];
                        break;
                    case 3: // 270 degrees
                        dest[7-x][y] = src[y][x];
                        break;
                }
            }
        }
    }
    
    void fadePattern(const IconPattern& src, IconPattern& dest, float fade_factor) {
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 8; x++) {
                dest[y][x] = src[y][x].scaled(fade_factor);
            }
        }
    }
    
    bool comparePatterns(const IconPattern& pattern1, const IconPattern& pattern2) {
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 8; x++) {
                if (!(pattern1[y][x] == pattern2[y][x])) {
                    return false;
                }
            }
        }
        return true;
    }
}
