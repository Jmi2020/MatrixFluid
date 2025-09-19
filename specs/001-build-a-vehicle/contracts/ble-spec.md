# BLE Advertisement Specification

**Feature**: 001-build-a-vehicle | **Date**: 2025-01-18

## Overview
Bluetooth Low Energy advertisement format for broadcasting fluid level status without requiring connection.

## Advertisement Data Structure

### Local Name
- **Type**: Complete Local Name (0x09)
- **Value**: "TankMon"
- **Length**: 7 bytes

### Manufacturer Specific Data
- **Type**: Manufacturer Specific Data (0xFF)
- **Company ID**: 0xFFFF (test/development ID)
- **Data Format**:

```
Byte | Description           | Values
-----|----------------------|--------
0-1  | Company ID           | 0xFF, 0xFF
2    | Protocol Version     | 0x01
3    | Fluid Level          | 0x00=Above Half, 0x01=Below Half, 0x02=Near Empty, 0xFF=Error
4    | Display State        | 0x00=Off, 0x01=Active
5    | Battery Level        | 0-100 (percentage), 0xFF=Not monitored
6    | Temperature          | Signed int8 (Celsius), 0x80=Not available
7    | Activation Count LSB | Lower byte of total activation count
8    | Activation Count MSB | Upper byte of total activation count
```

### Service UUIDs (Optional)
- **Environmental Sensing Service**: 0x181A (if exposing sensor data)
- **Battery Service**: 0x180F (if monitoring battery)

## Advertisement Parameters

### Timing
- **Interval**: 2000ms (2 seconds)
- **Duration**: Continuous when enabled
- **Type**: ADV_NONCONN_IND (non-connectable)

### Power
- **TX Power**: 0 dBm (approximately 10m range)
- **Included in advertisement**: Yes (1 byte)

## Example Advertisement Packet

```
02 01 06                    # Flags: LE General Discoverable, BR/EDR Not Supported
08 09 54 61 6E 6B 4D 6F 6E # Complete Local Name: "TankMon"
0B FF FF FF 01 00 00 64 19 00 2A # Manufacturer Data
02 0A 00                    # TX Power Level: 0 dBm
```

### Decoded Example
- Flags: BLE only, general discoverable
- Name: "TankMon"
- Manufacturer Data:
  - Company: 0xFFFF (test)
  - Version: 0x01
  - Fluid Level: 0x00 (Above Half)
  - Display: 0x00 (Off)
  - Battery: 100%
  - Temperature: 25°C
  - Activations: 42

## Scan Response Data (Optional)

If scan response is implemented:

```
0F 08 54 61 6E 6B 20 4D 6F 6E 69 74 6F 72 # Complete Name: "Tank Monitor"
11 07 UUID_128_BIT_CUSTOM_SERVICE         # Custom service UUID
```

## Client Implementation

### Scanning
```javascript
// Example Web Bluetooth scan filter
const scanFilter = {
  name: 'TankMon',
  manufacturerData: [{
    companyIdentifier: 0xFFFF
  }]
};
```

### Parsing
```cpp
struct BLEAdvData {
    uint8_t version;
    uint8_t fluid_level;
    uint8_t display_state;
    uint8_t battery_percent;
    int8_t temperature_c;
    uint16_t activation_count;
};

void parseManufacturerData(uint8_t* data, uint8_t length) {
    if (length >= 9 && data[0] == 0xFF && data[1] == 0xFF) {
        BLEAdvData adv;
        adv.version = data[2];
        adv.fluid_level = data[3];
        adv.display_state = data[4];
        adv.battery_percent = data[5];
        adv.temperature_c = (int8_t)data[6];
        adv.activation_count = data[7] | (data[8] << 8);
    }
}
```

## Privacy Considerations

- No personally identifiable information transmitted
- No persistent connections allowed
- No pairing or bonding supported
- MAC address randomization recommended

## Power Optimization

- Use lowest viable TX power
- Increase interval if battery critical
- Disable when not needed (compile flag)
- Consider duty cycling (advertise for 30s after activation)

## Testing Requirements

1. Verify advertisement visible to standard BLE scanners
2. Confirm data updates match actual sensor state
3. Validate 10m range in typical environment
4. Test battery impact (measure current during advertising)
5. Ensure no interference with tap detection timing