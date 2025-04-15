#include <M5StickCPlus.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include <esp_now.h>
#include <WiFi.h>

#define LED_PIN 26
#define LED_COUNT 10
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128
#define OLED_RESET -1
Adafruit_SH1107 display = Adafruit_SH1107(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Color definitions (matches Device1)
const uint32_t colors[10] = {
  strip.Color(255, 0, 0),    // RED
  strip.Color(0, 255, 0),    // GREEN
  strip.Color(0, 0, 255),    // BLUE
  strip.Color(255, 255, 0),  // YELLOW
  strip.Color(0, 255, 255),  // CYAN
  strip.Color(255, 0, 255),  // MAGENTA
  strip.Color(255, 165, 0),  // ORANGE
  strip.Color(128, 0, 128),  // PURPLE
  strip.Color(255, 192, 203),// PINK
  strip.Color(153, 102, 0)   // BROWN
};

// ESP-NOW data structure (matches Device1)
struct Message {
  int userIndex;
  int colorIndex;
  bool systemFinished;
};

int currentUserCount = 0;
bool systemFinished = false;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  Message msg;
  memcpy(&msg, incomingData, sizeof(msg));
  
  Serial.print("Received data for user: ");
  Serial.print(msg.userIndex);
  Serial.print(", color: ");
  Serial.println(msg.colorIndex);
  
  if (msg.systemFinished) {
    systemFinished = true;
    updateDisplay();
    return;
  }
  
  if (msg.colorIndex >= 0 && msg.colorIndex < 10) {
    if (msg.userIndex >= currentUserCount) {
      currentUserCount = msg.userIndex + 1;
    }
    
    // Update the corresponding LED
    strip.setPixelColor(msg.userIndex, colors[msg.colorIndex]);
    strip.show();
    
    updateDisplay();
  }
}

void updateDisplay() {
  display.clearDisplay();
  
  if (systemFinished) {
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(10, 50);
    display.println("Work Started");
    display.display();
    return;
  }
  
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  // Display title
  display.setCursor(10, 10);
  display.println("User Colors:");
  
  // Display current user count
  display.setCursor(10, 30);
  display.print("Users: ");
  display.print(currentUserCount);
  display.print("/10");
  
  // Display instructions
  display.setCursor(10, 100);
  if (currentUserCount == 0) {
    display.println("Waiting for users...");
  } else {
    display.println("Ready for more users");
  }
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  
  // Initialize OLED
  if(!display.begin(0x3C, true)) {
    Serial.println("OLED initialization failed");
    while(1);
  }
  display.clearDisplay();
  display.display();
  
  // Initialize LED strip
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  
  // Initialize WiFi and ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  
  // Print MAC address for reference
  Serial.print("Device2 MAC: ");
  Serial.println(WiFi.macAddress());
  
  // Initial display
  updateDisplay();
  
  // Test LEDs
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(50, 50, 50));
    strip.show();
    delay(100);
  }
  delay(500);
  strip.clear();
  strip.show();
}

void loop() {
  M5.update();
  // Main loop doesn't need to do much - everything is event-driven
  delay(100);
}