#include <M5StickCPlus.h>
#include "Unit_Encoder.h"
#include <esp_now.h>
#include <WiFi.h>

// Hardware Definitions
#define IR_PIN 0
#define BUZZER_FREQ 3000
#define DEBOUNCE_DELAY 50
#define DOUBLE_CLICK_TIME 500

Unit_Encoder sensor;

// Color Definitions (matches Device2)
const uint16_t colors[10] = {
  TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN,
  TFT_MAGENTA, TFT_ORANGE, TFT_PURPLE, TFT_PINK, 0x996600 // Brown
};
const char* colorNames[10] = {
  "Red", "Green", "Blue", "Yellow", "Cyan",
  "Magenta", "Orange", "Purple", "Pink", "Brown"
};

// User Management
struct User {
  String name;
  int colorIndex;
  bool isLocked;
};
User users[10];
bool colorsUsed[10] = {false};
int currentUserIndex = 0;

// System States
enum SystemState { SYS_IDLE, SYS_SELECTING, SYS_FINISHED };
SystemState systemState = SYS_IDLE;

// Button States
enum ButtonState { BTN_IDLE, BTN_FIRST_PRESS };
ButtonState btnState = BTN_IDLE;

// Device2 MAC Address
uint8_t broadcastAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x8B, 0x70};

// Message Structure for ESP-NOW
#pragma pack(push, 1)
struct Message {
  int userIndex;
  int colorIndex;
  bool systemFinished;
};
#pragma pack(pop)

// Timing and State Variables
unsigned long firstPressTime = 0;
bool irActive = false;

// ESP-NOW Send Callback
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

void sendMessage(int userIdx, int colorIdx, bool finished) {
  Message msg = {userIdx, colorIdx, finished};
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(msg));
  
  if (result == ESP_OK) {
    Serial.println("Message sent successfully");
  } else {
    Serial.println("Error sending message");
  }
}

void updateDisplay() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);

  if (systemState == SYS_FINISHED) {
    M5.Lcd.println("All users set");
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("Ready to start!");
    return;
  }

  if (systemState == SYS_IDLE) {
    M5.Lcd.println("Waiting for");
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("IR detection...");
    return;
  }

  User& u = users[currentUserIndex];
  M5.Lcd.printf("%s", u.name.c_str());
  
  if (u.colorIndex != -1) {
    M5.Lcd.fillRect(30, 50, 80, 30, colors[u.colorIndex]);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 90);
    M5.Lcd.printf("Color: %s", colorNames[u.colorIndex]);
  } else {
    M5.Lcd.setCursor(10, 50);
    M5.Lcd.println("Rotate to select");
  }

  M5.Lcd.setCursor(10, 110);
  M5.Lcd.setTextSize(1);
  M5.Lcd.println("Press:Confirm");
  M5.Lcd.setCursor(10, 125);
  M5.Lcd.println("Double:Finish");
}

void handleIR() {
  static bool lastState = false;
  bool detected = digitalRead(IR_PIN) == LOW;

  if (detected && !lastState) {
    irActive = true;
    M5.Beep.tone(BUZZER_FREQ);
    if (systemState == SYS_IDLE) {
      systemState = SYS_SELECTING;
      updateDisplay();
    }
  }

  if (!detected && lastState && systemState != SYS_FINISHED) {
    irActive = false;
    M5.Beep.mute();
  }

  lastState = detected;
}

void handleEncoder() {
  static int lastEncoder = 0;
  int encoder = sensor.getEncoderValue();
  int delta = encoder - lastEncoder;
  lastEncoder = encoder;

  if (systemState != SYS_SELECTING) return;
  
  User& u = users[currentUserIndex];
  if (u.isLocked || delta == 0) return;

  int newColor = getNextAvailableColor(u.colorIndex, delta > 0 ? 1 : -1);
  if (newColor != -1) {
    u.colorIndex = newColor;
    updateDisplay();
    // Removed sendMessage from here - only send after confirmation
  }
}

int getNextAvailableColor(int current, int dir) {
  if (current == -1) {
    for (int i = 0; i < 10; i++) {
      int check = (dir == 1) ? i : 9 - i;
      if (!colorsUsed[check]) return check;
    }
    return -1;
  }

  for (int i = 1; i < 10; i++) {
    int next = (current + dir * i + 10) % 10;
    if (!colorsUsed[next]) return next;
  }
  return -1;
}

void handleButton() {
  bool btn = sensor.getButtonStatus();
  static bool lastBtn = false;
  bool pressed = btn && !lastBtn;
  lastBtn = btn;

  if (!pressed || systemState == SYS_FINISHED) return;

  unsigned long now = millis();
  
  if (btnState == BTN_IDLE) {
    btnState = BTN_FIRST_PRESS;
    firstPressTime = now;
  } 
  else if (btnState == BTN_FIRST_PRESS && (now - firstPressTime < DOUBLE_CLICK_TIME)) {
    // Double click: finish setup
    systemState = SYS_FINISHED;
    M5.Beep.mute();
    updateDisplay();
    btnState = BTN_IDLE;
    sendMessage(-1, -1, true); // Send finish signal
    return;
  }

  // Single click: confirm selection
  if (btnState == BTN_FIRST_PRESS && (now - firstPressTime >= DOUBLE_CLICK_TIME)) {
    User &u = users[currentUserIndex];
    if (u.colorIndex != -1 && !u.isLocked) {
      u.isLocked = true;
      colorsUsed[u.colorIndex] = true;
      
      // Send confirmation to Device2 only after user presses button
      sendMessage(currentUserIndex, u.colorIndex, false);

      // Find next available user
      bool foundNext = false;
      for (int i = 1; i <= 10; i++) {
        int next = (currentUserIndex + i) % 10;
        if (!users[next].isLocked) {
          currentUserIndex = next;
          foundNext = true;
          break;
        }
      }

      // If no more users, finish
      if (!foundNext) {
        systemState = SYS_FINISHED;
        sendMessage(-1, -1, true);
      }

      updateDisplay();
    }
    btnState = BTN_IDLE;
  }
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  M5.Lcd.begin();
  M5.Beep.begin();
  sensor.begin();
  pinMode(IR_PIN, INPUT);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(TFT_BLACK);

  // Initialize users
  for (int i = 0; i < 10; i++) {
    users[i] = {"User" + String(i + 1), -1, false};
  }

  // Initialize WiFi and ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  // Test beep
  M5.Beep.tone(2000, 100);
  delay(200);
  M5.Beep.tone(3000, 100);
  delay(200);
  M5.Beep.mute();

  updateDisplay();
}

void loop() {
  handleIR();
  handleEncoder();
  handleButton();
  
  // Maintain buzzer state while IR is active and not finished
  if (irActive && systemState != SYS_FINISHED) {
    M5.Beep.tone(BUZZER_FREQ);
  } else if (!irActive || systemState == SYS_FINISHED) {
    M5.Beep.mute();
  }
  
  delay(20);
}