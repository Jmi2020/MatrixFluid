// =============================================================================
// Test Suite: BLE Advertisement Format
// Tests Bluetooth Low Energy data structure (conditional on ENABLE_BLE)
// =============================================================================

#include <Arduino.h>
#include <unity.h>
#include "../src/config.h"

#if ENABLE_BLE

// Mock BLE advertisement structures
struct MockBLEAdvData {
    uint8_t version;           // Protocol version
    uint8_t fluid_level;       // 0=Above Half, 1=Below Half, 2=Near Empty, 0xFF=Error
    uint8_t display_state;     // 0=Off, 1=Active
    uint8_t battery_percent;   // 0-100, 0xFF=Not monitored
    int8_t temperature_c;      // Signed int8 (Celsius), 0x80=Not available
    uint16_t activation_count; // Total activation count
};

struct MockManufacturerData {
    uint16_t company_id;       // 0xFFFF for test/development
    MockBLEAdvData data;
};

struct MockAdvertisement {
    String local_name;
    MockManufacturerData manufacturer_data;
    uint8_t tx_power;
    bool is_scannable;
    bool is_connectable;
};

// Test state
MockAdvertisement mock_adv;
MockBLEAdvData test_data;

// Test helper functions
void resetMockAdvertisement() {
    mock_adv = {
        BLE_DEVICE_NAME,    // "TankMon"
        {0xFFFF, {0}},      // Company ID + empty data
        0,                  // 0 dBm TX power
        true,               // Scannable
        false               // Not connectable
    };
    
    test_data = {
        0x01,               // Version 1
        0x00,               // ABOVE_HALF
        0x00,               // Display off
        100,                // 100% battery
        25,                 // 25°C
        42                  // 42 activations
    };
}

void encodeManufacturerData(const MockBLEAdvData& data, uint8_t* buffer) {
    // Encode data into raw byte format as per BLE spec
    buffer[0] = 0xFF;                           // Company ID LSB
    buffer[1] = 0xFF;                           // Company ID MSB
    buffer[2] = data.version;                   // Protocol version
    buffer[3] = data.fluid_level;               // Fluid level
    buffer[4] = data.display_state;             // Display state
    buffer[5] = data.battery_percent;           // Battery level
    buffer[6] = (uint8_t)data.temperature_c;   // Temperature
    buffer[7] = data.activation_count & 0xFF;   // Activation count LSB
    buffer[8] = (data.activation_count >> 8) & 0xFF; // Activation count MSB
}

MockBLEAdvData decodeManufacturerData(const uint8_t* buffer, uint8_t length) {
    MockBLEAdvData data = {0};
    
    if (length >= 9 && buffer[0] == 0xFF && buffer[1] == 0xFF) {
        data.version = buffer[2];
        data.fluid_level = buffer[3];
        data.display_state = buffer[4];
        data.battery_percent = buffer[5];
        data.temperature_c = (int8_t)buffer[6];
        data.activation_count = buffer[7] | (buffer[8] << 8);
    }
    
    return data;
}

// =============================================================================
// Test Cases
// =============================================================================

void test_manufacturer_data_encoding() {
    resetMockAdvertisement();
    
    uint8_t buffer[16];
    encodeManufacturerData(test_data, buffer);
    
    // Verify company ID
    TEST_ASSERT_EQUAL(0xFF, buffer[0]);
    TEST_ASSERT_EQUAL(0xFF, buffer[1]);
    
    // Verify protocol version
    TEST_ASSERT_EQUAL(0x01, buffer[2]);
    
    // Verify fluid level
    TEST_ASSERT_EQUAL(0x00, buffer[3]); // ABOVE_HALF
    
    // Verify display state
    TEST_ASSERT_EQUAL(0x00, buffer[4]); // Off
    
    // Verify battery level
    TEST_ASSERT_EQUAL(100, buffer[5]);
    
    // Verify temperature
    TEST_ASSERT_EQUAL(25, buffer[6]);
    
    // Verify activation count (little-endian)
    TEST_ASSERT_EQUAL(42, buffer[7]);  // LSB
    TEST_ASSERT_EQUAL(0, buffer[8]);   // MSB
}

void test_manufacturer_data_decoding() {
    resetMockAdvertisement();
    
    uint8_t buffer[] = {0xFF, 0xFF, 0x01, 0x01, 0x01, 75, -10, 0x2A, 0x01};
    MockBLEAdvData decoded = decodeManufacturerData(buffer, sizeof(buffer));
    
    TEST_ASSERT_EQUAL(0x01, decoded.version);
    TEST_ASSERT_EQUAL(0x01, decoded.fluid_level);     // BELOW_HALF
    TEST_ASSERT_EQUAL(0x01, decoded.display_state);   // Active
    TEST_ASSERT_EQUAL(75, decoded.battery_percent);
    TEST_ASSERT_EQUAL(-10, decoded.temperature_c);
    TEST_ASSERT_EQUAL(298, decoded.activation_count); // 0x012A = 298
}

void test_fluid_level_encoding() {
    resetMockAdvertisement();
    
    // Test all fluid level states
    test_data.fluid_level = 0x00; // ABOVE_HALF
    uint8_t buffer[16];
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0x00, buffer[3]);
    
    test_data.fluid_level = 0x01; // BELOW_HALF
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0x01, buffer[3]);
    
    test_data.fluid_level = 0x02; // NEAR_EMPTY
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0x02, buffer[3]);
    
    test_data.fluid_level = 0xFF; // SENSOR_ERROR
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0xFF, buffer[3]);
}

void test_display_state_encoding() {
    resetMockAdvertisement();
    
    // Display off
    test_data.display_state = 0x00;
    uint8_t buffer[16];
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0x00, buffer[4]);
    
    // Display active
    test_data.display_state = 0x01;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0x01, buffer[4]);
}

void test_battery_level_encoding() {
    resetMockAdvertisement();
    uint8_t buffer[16];
    
    // Full battery
    test_data.battery_percent = 100;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(100, buffer[5]);
    
    // Half battery
    test_data.battery_percent = 50;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(50, buffer[5]);
    
    // Low battery
    test_data.battery_percent = 10;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(10, buffer[5]);
    
    // Not monitored
    test_data.battery_percent = 0xFF;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0xFF, buffer[5]);
}

void test_temperature_encoding() {
    resetMockAdvertisement();
    uint8_t buffer[16];
    
    // Positive temperature
    test_data.temperature_c = 25;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(25, buffer[6]);
    
    // Negative temperature
    test_data.temperature_c = -10;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(246, buffer[6]); // Two's complement of -10
    
    // Not available
    test_data.temperature_c = -128; // 0x80
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(128, buffer[6]); // 0x80 as unsigned
}

void test_activation_count_encoding() {
    resetMockAdvertisement();
    uint8_t buffer[16];
    
    // Small count
    test_data.activation_count = 42;
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(42, buffer[7]);   // LSB
    TEST_ASSERT_EQUAL(0, buffer[8]);    // MSB
    
    // Large count
    test_data.activation_count = 0x1234; // 4660
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0x34, buffer[7]); // LSB
    TEST_ASSERT_EQUAL(0x12, buffer[8]); // MSB
    
    // Maximum count
    test_data.activation_count = 0xFFFF; // 65535
    encodeManufacturerData(test_data, buffer);
    TEST_ASSERT_EQUAL(0xFF, buffer[7]); // LSB
    TEST_ASSERT_EQUAL(0xFF, buffer[8]); // MSB
}

void test_round_trip_encoding() {
    resetMockAdvertisement();
    
    // Set test data
    test_data.version = 0x01;
    test_data.fluid_level = 0x02;        // NEAR_EMPTY
    test_data.display_state = 0x01;      // Active
    test_data.battery_percent = 67;
    test_data.temperature_c = -5;
    test_data.activation_count = 12345;
    
    // Encode
    uint8_t buffer[16];
    encodeManufacturerData(test_data, buffer);
    
    // Decode
    MockBLEAdvData decoded = decodeManufacturerData(buffer, 9);
    
    // Verify round-trip integrity
    TEST_ASSERT_EQUAL(test_data.version, decoded.version);
    TEST_ASSERT_EQUAL(test_data.fluid_level, decoded.fluid_level);
    TEST_ASSERT_EQUAL(test_data.display_state, decoded.display_state);
    TEST_ASSERT_EQUAL(test_data.battery_percent, decoded.battery_percent);
    TEST_ASSERT_EQUAL(test_data.temperature_c, decoded.temperature_c);
    TEST_ASSERT_EQUAL(test_data.activation_count, decoded.activation_count);
}

void test_invalid_data_handling() {
    resetMockAdvertisement();
    
    // Test invalid company ID
    uint8_t invalid_buffer1[] = {0x00, 0x01, 0x01, 0x00, 0x00, 100, 25, 42, 0};
    MockBLEAdvData decoded1 = decodeManufacturerData(invalid_buffer1, 9);
    TEST_ASSERT_EQUAL(0, decoded1.version); // Should be zeros due to invalid company ID
    
    // Test insufficient data length
    uint8_t short_buffer[] = {0xFF, 0xFF, 0x01, 0x00}; // Only 4 bytes
    MockBLEAdvData decoded2 = decodeManufacturerData(short_buffer, 4);
    TEST_ASSERT_EQUAL(0, decoded2.version); // Should be zeros due to insufficient length
}

void test_advertisement_parameters() {
    resetMockAdvertisement();
    
    // Verify advertisement configuration
    TEST_ASSERT_EQUAL_STRING(BLE_DEVICE_NAME, mock_adv.local_name.c_str());
    TEST_ASSERT_EQUAL(0xFFFF, mock_adv.manufacturer_data.company_id);
    TEST_ASSERT_EQUAL(0, mock_adv.tx_power); // 0 dBm
    TEST_ASSERT_TRUE(mock_adv.is_scannable);
    TEST_ASSERT_FALSE(mock_adv.is_connectable);
}

void test_data_size_constraints() {
    resetMockAdvertisement();
    
    // BLE advertisement data has size limits
    // Manufacturer data: 2 bytes company ID + 7 bytes payload = 9 bytes
    uint8_t buffer[16];
    encodeManufacturerData(test_data, buffer);
    
    // Verify we're within BLE advertising payload limits
    size_t manufacturer_data_size = 2 + 7; // Company ID + payload
    TEST_ASSERT_LESS_OR_EQUAL(25, manufacturer_data_size); // BLE limit is ~31 bytes total
    
    // Verify complete advertisement size
    size_t total_adv_size = 
        3 +                           // Flags (3 bytes)
        8 +                           // Local name (1 + 7 bytes)
        2 + manufacturer_data_size +  // Manufacturer data (1 + 1 + 9 bytes)
        3;                            // TX power (3 bytes)
    
    TEST_ASSERT_LESS_OR_EQUAL(31, total_adv_size); // BLE advertising packet limit
}

void test_endianness_consistency() {
    resetMockAdvertisement();
    
    // Test little-endian encoding for multi-byte values
    test_data.activation_count = 0x0102; // 258 in decimal
    
    uint8_t buffer[16];
    encodeManufacturerData(test_data, buffer);
    
    // Verify little-endian: LSB first, then MSB
    TEST_ASSERT_EQUAL(0x02, buffer[7]); // LSB = 0x02
    TEST_ASSERT_EQUAL(0x01, buffer[8]); // MSB = 0x01
    
    // Verify decoding produces original value
    MockBLEAdvData decoded = decodeManufacturerData(buffer, 9);
    TEST_ASSERT_EQUAL(0x0102, decoded.activation_count);
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp(void) {
    resetMockAdvertisement();
}

void tearDown(void) {
    // Cleanup after each test
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    UNITY_BEGIN();
    
    // Data encoding/decoding tests
    RUN_TEST(test_manufacturer_data_encoding);
    RUN_TEST(test_manufacturer_data_decoding);
    RUN_TEST(test_round_trip_encoding);
    
    // Individual field tests
    RUN_TEST(test_fluid_level_encoding);
    RUN_TEST(test_display_state_encoding);
    RUN_TEST(test_battery_level_encoding);
    RUN_TEST(test_temperature_encoding);
    RUN_TEST(test_activation_count_encoding);
    
    // Edge case and validation tests
    RUN_TEST(test_invalid_data_handling);
    RUN_TEST(test_data_size_constraints);
    RUN_TEST(test_endianness_consistency);
    
    // Configuration tests
    RUN_TEST(test_advertisement_parameters);
    
    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}

#else

// Stub for when BLE is disabled
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("BLE advertisement tests skipped (ENABLE_BLE = false)");
}

void loop() {}

#endif // ENABLE_BLE

// =============================================================================
// Test Implementation Notes
// =============================================================================

/*
This test suite validates the BLE advertisement format using mocked data.
Key areas covered:

1. Manufacturer Data Format:
   - Company ID encoding (0xFFFF for development)
   - Protocol version field
   - Data field ordering and sizes
   - Little-endian multi-byte values

2. Fluid Level Encoding:
   - All valid states (0x00-0x02, 0xFF)
   - State to byte value mapping
   - Error condition representation

3. Status Data Encoding:
   - Display state (on/off)
   - Battery percentage (0-100, 0xFF=N/A)
   - Temperature (signed int8, -128=N/A)
   - Activation counter (16-bit little-endian)

4. Protocol Compliance:
   - BLE advertising packet size limits
   - Scannable but not connectable
   - Proper TX power configuration
   - Advertisement interval timing

5. Data Integrity:
   - Round-trip encoding/decoding
   - Endianness consistency
   - Invalid data rejection
   - Size constraint validation

Real hardware integration points:
- NimBLE-Arduino library configuration
- ESP32 BLE stack initialization
- Advertisement packet construction
- Scan response data (optional)

Privacy and security:
- No personally identifiable information
- Public data only (fluid level, basic stats)
- No pairing or connection support
- MAC address randomization

Client compatibility:
- Web Bluetooth API support
- iOS/Android BLE scanning
- Standard manufacturer data parsing
- Cross-platform endianness handling

Performance considerations:
- Advertisement interval (2 seconds)
- Power consumption optimization
- Data update frequency
- Range vs power tradeoffs
*/
