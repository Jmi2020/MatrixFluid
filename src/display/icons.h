#ifndef ICONS_H
#define ICONS_H

#include <Arduino.h>
#include "../config.h"

// =============================================================================
// 8x8 Pixel Icon Patterns for LED Matrix Display
// Optimized patterns for fluid level status indication
// =============================================================================

// Color definitions (24-bit RGB)
struct RGBColor {
    uint8_t r, g, b;
    
    RGBColor() : r(0), g(0), b(0) {}
    RGBColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    
    // Brightness scaling
    RGBColor scaled(float factor) const {
        return RGBColor(
            (uint8_t)(r * factor),
            (uint8_t)(g * factor),
            (uint8_t)(b * factor)
        );
    }
    
    bool operator==(const RGBColor& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
};

// Standard status colors
namespace StatusColors {
    constexpr RGBColor OFF(0, 0, 0);           // Black (LEDs off)
    constexpr RGBColor GREEN(0, 255, 0);       // Green for good level
    constexpr RGBColor YELLOW(255, 255, 0);    // Yellow for caution
    constexpr RGBColor RED(255, 0, 0);         // Red for low/error
    constexpr RGBColor WHITE(255, 255, 255);   // White for self-test
    constexpr RGBColor BLUE(0, 0, 255);        // Blue for diagnostics
}

// Icon pattern type (8x8 matrix)
typedef RGBColor IconPattern[8][8];

// Icon pattern storage in program memory
class Icons {
public:
    // Status icon patterns
    static const IconPattern& getGreenCheckmark();    // ✓ - Good level (above half)
    static const IconPattern& getYellowCaution();     // ⚠ - Caution (below half)
    static const IconPattern& getRedStop();           // ⬢ - Low level (near empty)
    static const IconPattern& getRedError();          // ✗ - Sensor error
    
    // Self-test patterns
    static const IconPattern& getAllOn();             // Full brightness test
    static const IconPattern& getAllOff();            // All LEDs off
    static const IconPattern& getColorTest();         // RGB color wheel test
    
    // Utility patterns
    static const IconPattern& getBlinkPattern();      // For error blinking
    static const IconPattern& getStartupPattern();    // Boot sequence
    
    // Pattern manipulation
    static void copyPattern(const IconPattern& src, IconPattern& dest);
    static void scalePattern(const IconPattern& src, IconPattern& dest, float brightness);
    static void blendPatterns(const IconPattern& base, const IconPattern& overlay, IconPattern& result, float alpha);
    static bool isPatternEmpty(const IconPattern& pattern);
    
    // Color utilities
    static RGBColor adjustBrightness(const RGBColor& color, uint8_t brightness);
    static RGBColor blendColors(const RGBColor& color1, const RGBColor& color2, float alpha);

private:
    // Internal pattern definitions (stored in PROGMEM)
    static const IconPattern green_checkmark PROGMEM;
    static const IconPattern yellow_caution PROGMEM;
    static const IconPattern red_stop PROGMEM;
    static const IconPattern red_error PROGMEM;
    static const IconPattern all_on PROGMEM;
    static const IconPattern all_off PROGMEM;
    static const IconPattern color_test PROGMEM;
    static const IconPattern blink_pattern PROGMEM;
    static const IconPattern startup_pattern PROGMEM;
    
    // Helper functions
    static void loadPatternFromProgmem(const IconPattern& src, IconPattern& dest);
};

// =============================================================================
// Pattern Definitions (designed for 8x8 matrix visibility)
// =============================================================================

// Green Checkmark Pattern (✓)
// Optimized for visibility at distance
const IconPattern Icons::green_checkmark PROGMEM = {{
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::GREEN, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::GREEN, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::GREEN, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::GREEN, StatusColors::OFF, StatusColors::OFF, StatusColors::GREEN, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::GREEN, StatusColors::GREEN, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::GREEN, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF}
}};

// Yellow Caution Triangle (⚠)
// Triangle with exclamation mark
const IconPattern Icons::yellow_caution PROGMEM = {{
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::YELLOW, StatusColors::OFF, StatusColors::YELLOW, StatusColors::OFF, StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF, StatusColors::YELLOW, StatusColors::OFF, StatusColors::OFF, StatusColors::YELLOW, StatusColors::OFF},
    {StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::YELLOW, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF}
}};

// Red Stop Octagon (⬢)
// Simplified octagon shape
const IconPattern Icons::red_stop PROGMEM = {{
    {StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::RED, StatusColors::RED, StatusColors::RED, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED},
    {StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED},
    {StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED},
    {StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::RED, StatusColors::RED, StatusColors::RED, StatusColors::OFF, StatusColors::OFF}
}};

// Red Error X Pattern (✗)
// Diagonal cross for error indication
const IconPattern Icons::red_error PROGMEM = {{
    {StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::RED, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::RED, StatusColors::OFF}
}};

// All LEDs On (brightness test)
const IconPattern Icons::all_on PROGMEM = {{
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::WHITE}
}};

// All LEDs Off
const IconPattern Icons::all_off PROGMEM = {{
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF}
}};

// Color Test Pattern (RGB stripes)
const IconPattern Icons::color_test PROGMEM = {{
    {StatusColors::RED, StatusColors::RED, StatusColors::GREEN, StatusColors::GREEN, StatusColors::BLUE, StatusColors::BLUE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::RED, StatusColors::RED, StatusColors::GREEN, StatusColors::GREEN, StatusColors::BLUE, StatusColors::BLUE, StatusColors::WHITE, StatusColors::WHITE},
    {StatusColors::GREEN, StatusColors::GREEN, StatusColors::BLUE, StatusColors::BLUE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::RED, StatusColors::RED},
    {StatusColors::GREEN, StatusColors::GREEN, StatusColors::BLUE, StatusColors::BLUE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::RED, StatusColors::RED},
    {StatusColors::BLUE, StatusColors::BLUE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::RED, StatusColors::RED, StatusColors::GREEN, StatusColors::GREEN},
    {StatusColors::BLUE, StatusColors::BLUE, StatusColors::WHITE, StatusColors::WHITE, StatusColors::RED, StatusColors::RED, StatusColors::GREEN, StatusColors::GREEN},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::RED, StatusColors::RED, StatusColors::GREEN, StatusColors::GREEN, StatusColors::BLUE, StatusColors::BLUE},
    {StatusColors::WHITE, StatusColors::WHITE, StatusColors::RED, StatusColors::RED, StatusColors::GREEN, StatusColors::GREEN, StatusColors::BLUE, StatusColors::BLUE}
}};

// Blink Pattern (alternating for error indication)
const IconPattern Icons::blink_pattern PROGMEM = {{
    {StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED},
    {StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED},
    {StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED},
    {StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED, StatusColors::OFF, StatusColors::RED}
}};

// Startup Pattern (centered dot expanding)
const IconPattern Icons::startup_pattern PROGMEM = {{
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::BLUE, StatusColors::BLUE, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::BLUE, StatusColors::BLUE, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF},
    {StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF, StatusColors::OFF}
}};

// =============================================================================
// Icon Animation Sequences
// =============================================================================

// Animation frame structure
struct AnimationFrame {
    const IconPattern* pattern;
    uint16_t duration_ms;
    float brightness_scale;
};

// Self-test animation sequence
const AnimationFrame SELF_TEST_SEQUENCE[] = {
    {&Icons::startup_pattern, 200, 0.5f},
    {&Icons::green_checkmark, 500, 1.0f},
    {&Icons::yellow_caution, 500, 1.0f},
    {&Icons::red_stop, 500, 1.0f},
    {&Icons::all_off, 200, 0.0f}
};

const uint8_t SELF_TEST_FRAME_COUNT = sizeof(SELF_TEST_SEQUENCE) / sizeof(AnimationFrame);

// Error blink animation
const AnimationFrame ERROR_BLINK_SEQUENCE[] = {
    {&Icons::red_error, 250, 1.0f},
    {&Icons::all_off, 250, 0.0f}
};

const uint8_t ERROR_BLINK_FRAME_COUNT = sizeof(ERROR_BLINK_SEQUENCE) / sizeof(AnimationFrame);

#endif // ICONS_H
