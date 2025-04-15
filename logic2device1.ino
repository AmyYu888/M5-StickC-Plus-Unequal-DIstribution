#include <M5StickCPlus.h>
#include "Unit_Encoder.h"
#include <esp_now.h>
#include <WiFi.h>

Unit_Encoder encoder;
#define BUTTON_PIN 37

// MAC address of Device2
uint8_t broadcastAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x8B, 0x70};

// 用户配置（4人）
const int userCount = 4;
const uint16_t userColors[userCount] = {
  TFT_RED, 0xFDA0, TFT_YELLOW, TFT_GREEN
};

int selectedUser = 0;
bool colorLocked = false;
bool systemStarted = false;

// 数据结构
typedef struct struct_message {
  int userIndex;
  int taskCount;
} struct_message;

struct_message messageToSend;
int taskCounts[userCount] = {0};

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void showStartScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Rotate to select");
  M5.Lcd.setCursor(10, 30);
  M5.Lcd.println("your color/user");
  M5.Lcd.setCursor(10, 80);
  M5.Lcd.println("PRESS TO START");
}

void showUserScreen(int index) {
  M5.Lcd.fillScreen(userColors[index]);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("User %d", index + 1);
}

void setup() {
  M5.begin();
  encoder.begin();
  pinMode(BUTTON_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

 esp_now_peer_info_t peerInfo = {};
memcpy(peerInfo.peer_addr, broadcastAddress, 6);
peerInfo.channel = 0;
peerInfo.encrypt = false;
peerInfo.ifidx = WIFI_IF_STA;  // ✅ 加上这句

  esp_now_add_peer(&peerInfo);

  M5.Lcd.setRotation(3);
  showStartScreen();
}

void loop() {
  static int lastEncoder = encoder.getEncoderValue();
  static bool lastBtn = false;
  static int stepCount = 0;
  bool btn = encoder.getButtonStatus();

  if (!systemStarted) {
    if (btn && !lastBtn) {
      systemStarted = true;
      showUserScreen(selectedUser);
    }
    lastBtn = btn;
    return;
  }

  int enc = encoder.getEncoderValue();
  int delta = enc - lastEncoder;

  if (!colorLocked && abs(delta) >= 2) {
    stepCount += delta / 2;
    selectedUser = ((stepCount % userCount) + userCount) % userCount;
    showUserScreen(selectedUser);
    lastEncoder = enc;
  }

  if (!colorLocked && btn && !lastBtn) {
    colorLocked = true;
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("User Locked!");
    delay(500);
  }

  if (colorLocked && btn && !lastBtn) {
    taskCounts[selectedUser]++;
    messageToSend.userIndex = selectedUser;
    messageToSend.taskCount = taskCounts[selectedUser];
    esp_now_send(broadcastAddress, (uint8_t *)&messageToSend, sizeof(messageToSend));

    M5.Lcd.fillScreen(userColors[selectedUser]);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Successful!");
    delay(500);
    colorLocked = false;
    systemStarted = false;
    showStartScreen();
  }
  lastBtn = btn;
}