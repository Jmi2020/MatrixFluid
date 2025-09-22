# ESP32 LED Matrix for HowdyTTS

This directory contains the Arduino sketch for an ESP32-S3 based LED matrix display that shows the current state of the HowdyTTS voice assistant.

## Hardware Requirements

- ESP32-S3 development board
- MAX7219 LED matrix display (8x32 or similar)
- Jumper wires for connections
- Micro USB cable for programming

## Wiring

Connect the LED matrix to the ESP32-S3 as follows:

| LED Matrix | ESP32-S3 |
|------------|----------|
| VCC        | 5V       |
| GND        | GND      |
| DIN        | GPIO23   |
| CS         | GPIO5    |
| CLK        | GPIO18   |

## Software Requirements

You'll need the following software:

1. [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 support
2. Libraries:
   - [MD_Parola](https://github.com/MajicDesigns/MD_Parola)
   - [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX)

## Setup Instructions

1. Install the Arduino IDE and add ESP32 support through the Board Manager
2. Install the required libraries via the Library Manager
3. Open the `HowdyTTS_LED_Matrix.ino` sketch
4. Edit the WiFi credentials:
   ```cpp
   const char* ssid = "Your_WiFi_SSID";      // Your WiFi network name
   const char* password = "Your_WiFi_Pass";   // Your WiFi password
   ```
5. Adjust the LED matrix configuration if needed:
   ```cpp
   #define HARDWARE_TYPE MD_MAX72XX::FC16_HW
   #define MAX_DEVICES 4  // Number of 8x8 modules in your matrix
   #define CS_PIN 5       // CS pin for SPI
   ```
6. Upload the sketch to your ESP32-S3
7. Open the Serial Monitor to see the IP address assigned to the ESP32-S3
8. Add this IP address to your `.env` file:
   ```
   ESP32_IP=192.168.1.xxx  # Replace with your ESP32's IP address
   ```

## API Endpoints

The ESP32 provides two HTTP endpoints:

1. `/state` - POST request with a `state` parameter to update the state:
   - `waiting` - Displayed when waiting for wake word
   - `listening` - Shown when listening for your command
   - `thinking` - Displayed when generating a response
   - `speaking` - Shows when speaking (requires text parameter)
   - `ending` - Appears when ending a conversation

2. `/speak` - POST request with a `text` parameter to display scrolling text

## Troubleshooting

- **LED Matrix not working**: Check the wiring and make sure you've set the correct HARDWARE_TYPE
- **WiFi not connecting**: Verify your WiFi credentials
- **HowdyTTS not connecting to ESP32**: Make sure the IP address in your `.env` file matches the ESP32's IP address
- **Text not scrolling**: Ensure the text is being properly URL-encoded before sending

## Customization

You can customize the animations and text effects in the `updateMatrixDisplay` function of the Arduino sketch.
