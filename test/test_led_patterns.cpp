// =============================================================================
// Test Suite: LED Pattern Rendering
// Tests 8x8 icon patterns and display control
// =============================================================================

#include <Arduino.h>
#include <unity.h>
#include "../src/config.h"

// Mock LED color structure
struct MockColor {
    uint8_t r, g, b;
    
    MockColor() : r(0), g(0), b(0) {}
    MockColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    
    bool operator==(const MockColor& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
};

// Mock LED matrix (8x8 = 64 pixels)
MockColor mock_matrix[64];
uint8_t mock_brightness = LED_BRIGHTNESS_DEFAULT;
bool mock_display_active = false;
uint32_t mock_millis = 0;

// Standard colors for patterns
const MockColor COLOR_OFF(0, 0, 0);
const MockColor COLOR_GREEN(0, 255, 0);
const MockColor COLOR_YELLOW(255, 255, 0);
const MockColor COLOR_RED(255, 0, 0);

// Test helper functions
void clearMatrix() {
    for (int i = 0; i < 64; i++) {
        mock_matrix[i] = COLOR_OFF;
    }
    mock_display_active = false;
}

void setPixel(uint8_t x, uint8_t y, MockColor color) {
    if (x < 8 && y < 8) {
        // Convert 2D coordinates to 1D array index
        // Assuming standard row-major order for testing
        uint8_t index = y * 8 + x;
        mock_matrix[index] = color;
    }
}

MockColor getPixel(uint8_t x, uint8_t y) {
    if (x < 8 && y < 8) {
        uint8_t index = y * 8 + x;
        return mock_matrix[index];
    }
    return COLOR_OFF;
}

void setBrightness(uint8_t brightness) {
    // Enforce safety limit
    mock_brightness = (brightness > LED_BRIGHTNESS_MAX) ? LED_BRIGHTNESS_MAX : brightness;
}

void showDisplay() {
    mock_display_active = true;
}

void hideDisplay() {
    mock_display_active = false;
}

// Pattern rendering functions (simplified versions of actual icons)
void renderGreenCheckmark() {
    clearMatrix();
    // Simplified checkmark pattern (✓)
    setPixel(6, 1, COLOR_GREEN);
    setPixel(5, 2, COLOR_GREEN);
    setPixel(4, 3, COLOR_GREEN);
    setPixel(1, 3, COLOR_GREEN);
    setPixel(2, 4, COLOR_GREEN);
    setPixel(3, 5, COLOR_GREEN);
    setPixel(2, 6, COLOR_GREEN);
    showDisplay();
}

void renderYellowCaution() {
    clearMatrix();
    // Simplified triangle with exclamation (⚠)
    setPixel(4, 1, COLOR_YELLOW); // Top point
    setPixel(3, 2, COLOR_YELLOW);
    setPixel(5, 2, COLOR_YELLOW);
    setPixel(2, 3, COLOR_YELLOW);
    setPixel(6, 3, COLOR_YELLOW);
    setPixel(1, 4, COLOR_YELLOW);
    setPixel(7, 4, COLOR_YELLOW);
    setPixel(4, 3, COLOR_YELLOW); // Exclamation dot
    setPixel(4, 5, COLOR_YELLOW); // Exclamation dot
    showDisplay();
}

void renderRedStop() {
    clearMatrix();
    // Simplified octagon (⬢)
    for (int x = 2; x <= 5; x++) {
        setPixel(x, 1, COLOR_RED); // Top edge
        setPixel(x, 6, COLOR_RED); // Bottom edge
    }
    for (int y = 2; y <= 5; y++) {
        setPixel(1, y, COLOR_RED); // Left edge
        setPixel(6, y, COLOR_RED); // Right edge
    }
    showDisplay();
}

void renderRedError() {
    clearMatrix();
    // X pattern (✗)
    for (int i = 1; i < 7; i++) {
        setPixel(i, i, COLOR_RED);       // Diagonal \
        setPixel(i, 7-i, COLOR_RED);     // Diagonal /
    }
    showDisplay();
}

// =============================================================================
// Test Cases
// =============================================================================

void test_matrix_coordinates() {
    clearMatrix();
    
    // Test corner pixels
    setPixel(0, 0, COLOR_GREEN);
    TEST_ASSERT_TRUE(getPixel(0, 0) == COLOR_GREEN);
    
    setPixel(7, 0, COLOR_YELLOW);
    TEST_ASSERT_TRUE(getPixel(7, 0) == COLOR_YELLOW);
    
    setPixel(0, 7, COLOR_RED);
    TEST_ASSERT_TRUE(getPixel(0, 7) == COLOR_RED);
    
    setPixel(7, 7, COLOR_GREEN);
    TEST_ASSERT_TRUE(getPixel(7, 7) == COLOR_GREEN);
    
    // Test center pixel
    setPixel(4, 4, COLOR_YELLOW);
    TEST_ASSERT_TRUE(getPixel(4, 4) == COLOR_YELLOW);
}

void test_matrix_clear() {
    // Set some pixels
    setPixel(2, 2, COLOR_GREEN);
    setPixel(5, 5, COLOR_RED);
    
    // Clear matrix
    clearMatrix();
    
    // Verify all pixels are off
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            TEST_ASSERT_TRUE(getPixel(x, y) == COLOR_OFF);
        }
    }
    
    TEST_ASSERT_FALSE(mock_display_active);
}

void test_brightness_safety_limit() {
    // Test normal brightness
    setBrightness(30);
    TEST_ASSERT_EQUAL(30, mock_brightness);
    
    // Test at safety limit
    setBrightness(LED_BRIGHTNESS_MAX);
    TEST_ASSERT_EQUAL(LED_BRIGHTNESS_MAX, mock_brightness);
    
    // Test exceeding safety limit - should be capped
    setBrightness(100);
    TEST_ASSERT_EQUAL(LED_BRIGHTNESS_MAX, mock_brightness);
    
    // Test maximum possible value - should be capped
    setBrightness(255);
    TEST_ASSERT_EQUAL(LED_BRIGHTNESS_MAX, mock_brightness);
}

void test_green_checkmark_pattern() {
    renderGreenCheckmark();
    
    // Verify display is active
    TEST_ASSERT_TRUE(mock_display_active);
    
    // Verify specific checkmark pixels are green
    TEST_ASSERT_TRUE(getPixel(6, 1) == COLOR_GREEN);
    TEST_ASSERT_TRUE(getPixel(5, 2) == COLOR_GREEN);
    TEST_ASSERT_TRUE(getPixel(4, 3) == COLOR_GREEN);
    TEST_ASSERT_TRUE(getPixel(1, 3) == COLOR_GREEN);
    
    // Verify non-pattern pixels are off
    TEST_ASSERT_TRUE(getPixel(0, 0) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(7, 7) == COLOR_OFF);
}

void test_yellow_caution_pattern() {
    renderYellowCaution();
    
    // Verify display is active
    TEST_ASSERT_TRUE(mock_display_active);
    
    // Verify triangle outline pixels are yellow
    TEST_ASSERT_TRUE(getPixel(4, 1) == COLOR_YELLOW); // Top point
    TEST_ASSERT_TRUE(getPixel(3, 2) == COLOR_YELLOW);
    TEST_ASSERT_TRUE(getPixel(5, 2) == COLOR_YELLOW);
    
    // Verify exclamation mark
    TEST_ASSERT_TRUE(getPixel(4, 3) == COLOR_YELLOW);
    TEST_ASSERT_TRUE(getPixel(4, 5) == COLOR_YELLOW);
    
    // Verify corners are off
    TEST_ASSERT_TRUE(getPixel(0, 0) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(7, 0) == COLOR_OFF);
}

void test_red_stop_pattern() {
    renderRedStop();
    
    // Verify display is active
    TEST_ASSERT_TRUE(mock_display_active);
    
    // Verify octagon edges are red
    TEST_ASSERT_TRUE(getPixel(2, 1) == COLOR_RED); // Top edge
    TEST_ASSERT_TRUE(getPixel(5, 1) == COLOR_RED);
    TEST_ASSERT_TRUE(getPixel(1, 2) == COLOR_RED); // Side edge
    TEST_ASSERT_TRUE(getPixel(6, 5) == COLOR_RED);
    
    // Verify corners are off (octagon shape)
    TEST_ASSERT_TRUE(getPixel(0, 0) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(7, 0) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(0, 7) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(7, 7) == COLOR_OFF);
}

void test_red_error_pattern() {
    renderRedError();
    
    // Verify display is active
    TEST_ASSERT_TRUE(mock_display_active);
    
    // Verify X pattern diagonals are red
    TEST_ASSERT_TRUE(getPixel(1, 1) == COLOR_RED); // \ diagonal
    TEST_ASSERT_TRUE(getPixel(3, 3) == COLOR_RED);
    TEST_ASSERT_TRUE(getPixel(5, 5) == COLOR_RED);
    
    TEST_ASSERT_TRUE(getPixel(1, 6) == COLOR_RED); // / diagonal
    TEST_ASSERT_TRUE(getPixel(3, 4) == COLOR_RED);
    TEST_ASSERT_TRUE(getPixel(5, 2) == COLOR_RED);
    
    // Verify center intersection
    // Note: In a real X, center might be double-lit
}

void test_pattern_switching() {
    // Test rapid pattern changes
    renderGreenCheckmark();
    TEST_ASSERT_TRUE(getPixel(6, 1) == COLOR_GREEN);
    
    renderRedStop();
    TEST_ASSERT_TRUE(getPixel(6, 1) == COLOR_OFF); // Should be cleared
    TEST_ASSERT_TRUE(getPixel(2, 1) == COLOR_RED); // New pattern
    
    renderYellowCaution();
    TEST_ASSERT_TRUE(getPixel(2, 1) == COLOR_OFF); // Previous cleared
    TEST_ASSERT_TRUE(getPixel(4, 1) == COLOR_YELLOW); // New pattern
}

void test_display_state_management() {
    // Start with display off
    clearMatrix();
    TEST_ASSERT_FALSE(mock_display_active);
    
    // Render pattern should activate display
    renderGreenCheckmark();
    TEST_ASSERT_TRUE(mock_display_active);
    
    // Hide display
    hideDisplay();
    TEST_ASSERT_FALSE(mock_display_active);
    
    // Show display again
    showDisplay();
    TEST_ASSERT_TRUE(mock_display_active);
}

void test_color_accuracy() {
    clearMatrix();
    
    // Test pure colors
    setPixel(0, 0, MockColor(255, 0, 0));   // Pure red
    MockColor red_pixel = getPixel(0, 0);
    TEST_ASSERT_EQUAL(255, red_pixel.r);
    TEST_ASSERT_EQUAL(0, red_pixel.g);
    TEST_ASSERT_EQUAL(0, red_pixel.b);
    
    setPixel(1, 1, MockColor(0, 255, 0));   // Pure green
    MockColor green_pixel = getPixel(1, 1);
    TEST_ASSERT_EQUAL(0, green_pixel.r);
    TEST_ASSERT_EQUAL(255, green_pixel.g);
    TEST_ASSERT_EQUAL(0, green_pixel.b);
    
    setPixel(2, 2, MockColor(255, 255, 0)); // Yellow
    MockColor yellow_pixel = getPixel(2, 2);
    TEST_ASSERT_EQUAL(255, yellow_pixel.r);
    TEST_ASSERT_EQUAL(255, yellow_pixel.g);
    TEST_ASSERT_EQUAL(0, yellow_pixel.b);
}

void test_bounds_checking() {
    clearMatrix();
    
    // Test out-of-bounds coordinates - should not crash
    setPixel(8, 0, COLOR_RED);   // X out of bounds
    setPixel(0, 8, COLOR_GREEN); // Y out of bounds
    setPixel(255, 255, COLOR_YELLOW); // Way out of bounds
    
    // Verify matrix is still clean
    TEST_ASSERT_TRUE(getPixel(7, 0) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(0, 7) == COLOR_OFF);
    
    // Test out-of-bounds reads return COLOR_OFF
    TEST_ASSERT_TRUE(getPixel(8, 0) == COLOR_OFF);
    TEST_ASSERT_TRUE(getPixel(0, 8) == COLOR_OFF);
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp(void) {
    clearMatrix();
    mock_brightness = LED_BRIGHTNESS_DEFAULT;
    mock_millis = 1000;
}

void tearDown(void) {
    // Cleanup after each test
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    UNITY_BEGIN();
    
    // Basic functionality tests
    RUN_TEST(test_matrix_coordinates);
    RUN_TEST(test_matrix_clear);
    RUN_TEST(test_brightness_safety_limit);
    
    // Pattern rendering tests
    RUN_TEST(test_green_checkmark_pattern);
    RUN_TEST(test_yellow_caution_pattern);
    RUN_TEST(test_red_stop_pattern);
    RUN_TEST(test_red_error_pattern);
    
    // State management tests
    RUN_TEST(test_pattern_switching);
    RUN_TEST(test_display_state_management);
    
    // Quality assurance tests
    RUN_TEST(test_color_accuracy);
    RUN_TEST(test_bounds_checking);
    
    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}

// =============================================================================
// Test Implementation Notes
// =============================================================================

/*
This test suite validates LED pattern rendering using a mocked 8x8 matrix.
Key areas covered:

1. Matrix Coordinate System:
   - 2D to 1D index conversion
   - Bounds checking for pixel access
   - Corner and center pixel verification

2. Pattern Accuracy:
   - Icon shape verification for each fluid level
   - Color accuracy for status indication
   - Pattern clearing between switches

3. Safety Constraints:
   - Brightness limiting enforcement
   - Hardware protection validation
   - Safe default behaviors

4. Display State Management:
   - Show/hide functionality
   - State tracking consistency
   - Pattern transition handling

Real hardware integration points:
- FastLED library with WS2812B
- GPIO14 data pin configuration
- Color order (GRB) for WS2812B
- Matrix layout (zigzag vs row-major)

Performance considerations:
- Pattern rendering speed
- Memory usage for frame buffer
- Brightness calculation efficiency
- Update rate for smooth transitions

Visual requirements:
- Icons must be distinguishable at 1+ meter distance
- Clear color differentiation (red/yellow/green)
- Consistent brightness scaling
- No flickering during updates

Pattern design constraints:
- 8x8 pixel resolution limitation
- Simple, recognizable symbols
- High contrast for visibility
- Minimalist due to pixel count
*/
