#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "../config.h"
#include "icons.h"

// =============================================================================
// LED Matrix Controller
// FastLED-based 8x8 WS2812B matrix control with safety features
// =============================================================================

// Display state enumeration (matches data model)
enum class DisplayState : uint8_t {
    OFF = 0,              // Matrix powered off
    SHOWING_GREEN = 1,    // Green checkmark displayed
    SHOWING_YELLOW = 2,   // Yellow caution displayed
    SHOWING_RED = 3,      // Red stop sign displayed
    SHOWING_ERROR = 4,    // Blinking red X pattern
    SELF_TEST = 5         // Startup test pattern
};

// Display management structure
struct DisplayStatus {
    DisplayState current_state;
    uint32_t state_start_time;
    uint32_t last_update_time;
    bool is_active;
    bool is_blinking;
    uint8_t animation_frame;
    float brightness_scale;
    
    DisplayStatus() : current_state(DisplayState::OFF), state_start_time(0), 
                     last_update_time(0), is_active(false), is_blinking(false),
                     animation_frame(0), brightness_scale(1.0f) {}
};

// LED matrix controller class
class LEDController {
private:
    // FastLED array and configuration
    CRGB leds[LED_COUNT];
    uint8_t current_brightness;
    bool initialized;
    
    // Display state management
    DisplayStatus display_status;
    IconPattern current_pattern;
    uint32_t timeout_start_time;
    bool timeout_active;
    
    // Animation and effects
    uint32_t last_animation_time;
    uint32_t blink_start_time;
    bool blink_state;
    
    // Safety and diagnostics
    bool safety_override;
    uint32_t last_thermal_check;
    bool diagnostics_enabled;
    uint32_t total_on_time;
    uint32_t activation_count;
    
    // Internal methods
    void updateMatrix();
    void applyPattern(const IconPattern& pattern, float brightness_scale = 1.0f);
    void clearMatrix();
    bool checkSafetyLimits();
    void handleTimeout();
    void updateAnimation();
    void updateBlinking();
    uint8_t xyToIndex(uint8_t x, uint8_t y);
    CRGB rgbColorToCRGB(const RGBColor& color, float brightness_scale = 1.0f);

public:
    // Constructor and initialization
    LEDController();
    bool begin();
    void end();
    
    // Main interface
    void update();                              // Call regularly to update display
    bool showPattern(DisplayState state);       // Display specific status pattern
    void turnOff();                             // Turn off display immediately
    bool isActive() const;                      // Check if display is currently on
    
    // Status display methods
    bool showGoodLevel();                       // Green checkmark
    bool showCautionLevel();                    // Yellow triangle
    bool showLowLevel();                        // Red octagon
    bool showErrorState();                      // Blinking red X
    bool showSelfTest();                        // Self-test sequence
    
    // Configuration
    void setBrightness(uint8_t brightness);     // Set brightness (capped at safety limit)
    uint8_t getBrightness() const;              // Get current brightness
    void setTimeout(uint16_t timeout_ms);       // Set auto-off timeout
    uint16_t getTimeout() const;                // Get current timeout
    
    // Advanced control
    void setBlinkRate(uint16_t period_ms);      // Error blink rate
    void enableSafetyOverride(bool enable);     // For testing only
    void forceUpdate();                         // Immediate matrix update
    bool startSelfTest();                       // Begin self-test sequence
    
    // Diagnostics and status
    DisplayState getCurrentState() const;       // Current display state
    uint32_t getTimeActive() const;             // Time in current state
    uint32_t getTotalOnTime() const;            // Total operating time
    uint32_t getActivationCount() const;        // Number of activations
    String getStateString() const;              // Human-readable state
    void printDiagnostics() const;              // Debug output
    void enableDiagnostics(bool enable);        // Control debug output
    
    // Testing and calibration
    void testAllPixels();                       // Light all pixels for testing
    void testColor(uint8_t r, uint8_t g, uint8_t b); // Test specific color
    void setCustomPattern(const IconPattern& pattern); // Custom pattern display
    bool verifyDisplay();                       // Hardware verification
    
    // Safety monitoring
    bool isThermalSafe() const;                 // Temperature check (if available)
    bool isPowerSafe() const;                   // Power consumption check
    void emergencyShutdown();                   // Immediate safety shutdown

private:
    // Configuration parameters
    uint16_t display_timeout_ms;
    uint16_t blink_period_ms;
    uint16_t animation_speed_ms;
    
    // Safety monitoring
    uint32_t max_on_time_continuous;
    uint8_t thermal_warning_count;
    bool emergency_shutdown_active;
    
    // Pattern buffer for effects
    IconPattern transition_buffer;
    
    // Animation timing
    uint32_t animation_frame_time;
    uint8_t animation_total_frames;
    bool animation_in_progress;
    
    // Helper methods for safety
    void enforceHardwareLimits();
    void logActivation();
    void checkThermalCondition();
    void validateBrightnessSettings();
};

// Utility functions for pattern manipulation
namespace PatternUtils {
    void rotatePattern(const IconPattern& src, IconPattern& dest, uint8_t rotation);
    void mirrorPattern(const IconPattern& src, IconPattern& dest, bool horizontal);
    void fadePattern(const IconPattern& src, IconPattern& dest, float fade_factor);
    void overlayPatterns(const IconPattern& base, const IconPattern& overlay, IconPattern& result);
    bool comparePatterns(const IconPattern& pattern1, const IconPattern& pattern2);
    void generateTestPattern(IconPattern& pattern, uint8_t test_type);
}

// Display timing constants
namespace DisplayTiming {
    constexpr uint16_t FAST_BLINK_MS = 250;     // Fast error blink
    constexpr uint16_t SLOW_BLINK_MS = 1000;    // Slow warning blink
    constexpr uint16_t FADE_TRANSITION_MS = 100; // Fade in/out time
    constexpr uint16_t SELF_TEST_FRAME_MS = 500; // Self-test frame duration
    constexpr uint32_t MAX_CONTINUOUS_ON_MS = 60000; // 1 minute safety limit
}

// Display brightness levels
namespace BrightnessLevels {
    constexpr uint8_t OFF = 0;
    constexpr uint8_t DIM = 10;
    constexpr uint8_t LOW = 20;
    constexpr uint8_t NORMAL = LED_BRIGHTNESS_DEFAULT;
    constexpr uint8_t HIGH = LED_BRIGHTNESS_MAX;
}

#endif // LED_CONTROLLER_H
