#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

// WiFi credentials
const char* ssid = "REPLACEWITHSSID";
const char* password = "REPLACEWITHPASS";
const char* hostname = "HowdyLED";

// Matrix setup - using exact same settings as Waveshare demo
#define RGB_Control_PIN 14
#define BRIGHTNESS 5

// Web server
WebServer server(80);

// Initialize the matrix using Waveshare's working configuration
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(8, 8, RGB_Control_PIN, 
  NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +                     
  NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,                
  NEO_GRB            + NEO_KHZ800);

// Scrolling text variables
int x = matrix.width(); // Current position of text
String currentMessage = "Say 'Hey Howdy' to start a conversation.";
String currentState = "waiting";
unsigned long stateChangeTime = 0; // Time when state was last changed
const uint16_t colors[] = {
  matrix.Color(0, 255, 0),   // Green for "waiting"
  matrix.Color(0, 0, 255),   // Blue for "listening"
  matrix.Color(255, 165, 0), // Orange for "thinking"
  matrix.Color(255, 0, 0),   // Red for "ending"
  matrix.Color(255, 255, 0)  // Yellow for "speaking"
};
uint16_t currentColor = colors[0];

// IP display control variables
bool showingIP = false;
int ipScrollCount = 0;
const int MAX_IP_SCROLLS = 2; // Number of times to scroll IP

// URL Decoding function to convert %xx escape sequences
String urlDecode(String input) {
  String decoded = "";
  char temp[] = "00"; // Holds the two hex characters
  unsigned int len = input.length();
  unsigned int i = 0;
  
  while (i < len) {
    char c = input[i];
    if (c == '%' && i + 2 < len) {
      // Get the two hex chars
      temp[0] = input[i + 1];
      temp[1] = input[i + 2];
      // Convert hex to decimal
      decoded += (char)strtol(temp, NULL, 16);
      // Skip the next two characters
      i += 3;
    } 
    else if (c == '+') {
      // Convert '+' to space
      decoded += ' ';
      i++;
    }
    else {
      // Regular character
      decoded += c;
      i++;
    }
  }
  
  return decoded;
}

// Helper function to calculate text width from Waveshare demo
int getCharWidth(char c) {
  if (c == 'i' || c == 'l' || c == '!' || c == '.') {
    return 3;
  } else {
    return 5;
  }
}

int getStringWidth(const String& str) {
  int width = 0;
  int length = str.length();
  
  for (int i = 0; i < length; i++) {
    width += getCharWidth(str.charAt(i));
    width += 1;      
  }
  return width;
}

void setup() {
  // Delay for stability
  delay(1000);
  
  Serial.begin(115200);
  Serial.println("\nHowdyTTS Matrix Controller (Waveshare Version)");
  
  // Initialize the matrix with EXACT Waveshare settings
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(BRIGHTNESS);
  matrix.setTextColor(currentColor);
  
  // Show a test pattern 
  for (int i = 0; i < 8; i++) {
    matrix.drawPixel(i, i, matrix.Color(255, 0, 0));
    matrix.show();
    delay(50);
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Setup web server endpoints
  server.on("/state", HTTP_POST, handleStateUpdate);
  server.on("/speak", HTTP_POST, handleSpeakUpdate); // New endpoint for speaking state
  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  
  Serial.println("HTTP server started");
  
  // Display initial message
  updateMessage("waiting");
  stateChangeTime = millis();
}

void loop() {
  server.handleClient();
  
  // Check if we need to auto-switch from "ending" to "waiting" after 10 seconds
  if (currentState == "ending" && (millis() - stateChangeTime >= 10000)) {
    updateMessage("waiting");
    stateChangeTime = millis();
  }
  
  scrollText();
  delay(50); // Controls scroll speed
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  
  // Set hostname before connecting
  WiFi.setHostname(hostname);
  WiFi.begin(ssid, password);
  
  // Try to connect for up to 20 seconds
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    
    // Display a dot pattern during connection attempt
    matrix.fillScreen(0);
    matrix.drawPixel(attempts % 8, attempts / 8, matrix.Color(0, 0, 255));
    matrix.show();
    
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Setup mDNS responder
    if (MDNS.begin(hostname)) {
      Serial.println("mDNS responder started");
      Serial.print("Device can be reached at: http://");
      Serial.print(hostname);
      Serial.println(".local");
      
      // Add service to mDNS
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("Error setting up mDNS responder!");
    }
    
    // Show IP separately for exactly two scrolls
    String ipMsg = "IP: " + WiFi.localIP().toString();
    displayScrollingText(ipMsg, matrix.Color(0, 255, 255), 2);
    
    // Show hostname
    displayStaticText(hostname, matrix.Color(0, 255, 255));
    delay(2000);
  } else {
    Serial.println("\nWiFi connection failed!");
    
    // Display failure message
    displayStaticText("WiFi Fail", matrix.Color(255, 0, 0));
    delay(3000);
  }
}

void displayStaticText(String text, uint16_t color) {
  matrix.fillScreen(0);
  matrix.setCursor(0, 0);
  matrix.setTextColor(color);
  matrix.print(text);
  matrix.show();
}

// New function to display scrolling text a specific number of times
void displayScrollingText(String text, uint16_t color, int scrollCount) {
  int textWidth = getStringWidth(text);
  int scrollPosition = matrix.width();
  int completedScrolls = 0;
  
  matrix.setTextColor(color);
  
  // Continue scrolling until we've completed the requested number of scrolls
  while (completedScrolls < scrollCount) {
    matrix.fillScreen(0);
    matrix.setCursor(scrollPosition, 0);
    matrix.print(text);
    matrix.show();
    
    scrollPosition--;
    
    // If text has scrolled off left completely, reset to right side and count a completed scroll
    if (scrollPosition < -textWidth) {
      scrollPosition = matrix.width();
      completedScrolls++;
    }
    
    delay(50); // Controls scroll speed
  }
}

void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>HowdyTTS LED Matrix</title>";
  html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;background-color:#f0f0f0;}";
  html += "button{background-color:#4CAF50;color:white;padding:10px 24px;border:none;border-radius:4px;margin:5px;cursor:pointer;}";
  html += "button:hover{background-color:#45a049;}</style></head><body>";
  html += "<h1>HowdyTTS LED Matrix Control</h1>";
  html += "<h2>Current state: " + currentState + "</h2>";
  html += "<h3>Message: \"" + currentMessage + "\"</h3>";
  html += "<h3>IP: " + WiFi.localIP().toString() + " (" + hostname + ".local)</h3>";
  html += "<div><button onclick=\"fetch('/state',{method:'POST',body:new URLSearchParams({state:'waiting'})}).then(()=>location.reload())\">Set Waiting</button>";
  html += "<button onclick=\"fetch('/state',{method:'POST',body:new URLSearchParams({state:'listening'})}).then(()=>location.reload())\">Set Listening</button>";
  html += "<button onclick=\"fetch('/state',{method:'POST',body:new URLSearchParams({state:'thinking'})}).then(()=>location.reload())\">Set Thinking</button>";
  html += "<button onclick=\"fetch('/state',{method:'POST',body:new URLSearchParams({state:'ending'})}).then(()=>location.reload())\">Set Ending</button>";
  html += "<button onclick=\"fetch('/speak',{method:'POST',body:new URLSearchParams({text:'Test speaking text'})}).then(()=>location.reload())\">Test Speaking</button></div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleStateUpdate() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    
    if (state == "listening" || state == "thinking" || 
        state == "ending" || state == "waiting") {
      updateMessage(state);
      stateChangeTime = millis(); // Update state change time
      server.send(200, "text/plain", "State updated to: " + state);
    } else {
      server.send(400, "text/plain", "Invalid state parameter");
    }
  } else {
    server.send(400, "text/plain", "Missing state parameter");
  }
}

// Updated handler for speaking state with URL decoding
void handleSpeakUpdate() {
  if (server.hasArg("text")) {
    String speakText = server.arg("text");
    
    // Decode URL-encoded text
    speakText = urlDecode(speakText);
    
    // Update to speaking state with provided text
    currentState = "speaking";
    currentMessage = speakText;
    currentColor = colors[4]; // Yellow color for speaking
    
    // Update text color
    matrix.setTextColor(currentColor);
    
    // Reset position to start scrolling from the right
    x = matrix.width();
    
    stateChangeTime = millis(); // Update state change time
    
    Serial.println("State changed to: speaking");
    Serial.println("Message: " + currentMessage);
    
    server.send(200, "text/plain", "Speaking text set to: " + speakText);
  } else {
    server.send(400, "text/plain", "Missing text parameter");
  }
}

void updateMessage(String state) {
  currentState = state;
  
  // Set color and message based on state - no IP address in messages
  if (state == "waiting") {
    currentColor = colors[0];  // Green
    currentMessage = "Say 'Hey Howdy' to start a conversation.";
  } else if (state == "listening") {
    currentColor = colors[1];  // Blue
    currentMessage = "Listening";
  } else if (state == "thinking") {
    currentColor = colors[2];  // Orange
    currentMessage = "Thinking";
  } else if (state == "ending") {
    currentColor = colors[3];  // Red
    currentMessage = "Later Space Cowboy";
  }
  
  // Update text color
  matrix.setTextColor(currentColor);
  
  // Reset position to start scrolling from the right
  x = matrix.width();
  
  Serial.println("State changed to: " + state);
  Serial.println("Message: " + currentMessage);
}

void scrollText() {
  // Using Waveshare's approach, which is working in their demo
  matrix.fillScreen(0);
  matrix.setCursor(x, 0);
  matrix.print(currentMessage);
  
  // Move position for scrolling
  x--;
  
  // If text has scrolled off left completely, reset to right side
  if (x < -getStringWidth(currentMessage)) {
    x = matrix.width();
  }
  
  // Show the updated display
  matrix.show();
}