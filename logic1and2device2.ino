#include <M5StickCPlus.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>

#define LED_PIN 26
#define LED_COUNT 10
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

const int maxUsers = 10;
uint32_t userColors[maxUsers];
int userTaskCounts[maxUsers] = {0};
bool userActive[maxUsers] = {false};

bool systemReady = false;
bool waitingToStart = false;

unsigned long lastFlashTime = 0;
int flashState = 0;

#pragma pack(push, 1)
struct Message {
  int userIndex;
  int colorIndex;
  bool systemFinished;
};
struct TaskMessage {
  int userIndex;
  int taskCount;
};
#pragma pack(pop)

const uint32_t colorMap[10] = {
  strip.Color(255, 0, 0), strip.Color(0, 255, 0), strip.Color(0, 0, 255),
  strip.Color(255, 255, 0), strip.Color(0, 255, 255), strip.Color(255, 0, 255),
  strip.Color(255, 165, 0), strip.Color(128, 0, 128), strip.Color(255, 192, 203), strip.Color(153, 102, 0)
};

// 排名结构
struct RankEntry {
  int taskCount;
  std::vector<int> userIndices;
};
std::vector<RankEntry> rankList;

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("Task Count:");
  for (int i = 0; i < maxUsers; i++) {
    if (userActive[i]) {
      display.setCursor(0, 12 + i * 10);
      display.printf("User %d: %d", i + 1, userTaskCounts[i]);
    }
  }
  display.display();
}

void buildRankList() {
  rankList.clear();
  for (int i = 0; i < maxUsers; i++) {
    if (!userActive[i]) continue;
    int count = userTaskCounts[i];
    bool found = false;
    for (auto &entry : rankList) {
      if (entry.taskCount == count) {
        entry.userIndices.push_back(i);
        found = true;
        break;
      }
    }
    if (!found) {
      RankEntry entry;
      entry.taskCount = count;
      entry.userIndices.push_back(i);
      rankList.push_back(entry);
    }
  }
  std::sort(rankList.begin(), rankList.end(), [](RankEntry &a, RankEntry &b) {
    return a.taskCount > b.taskCount;
  });
}

void updateStrip() {
  if (waitingToStart || !systemReady) return;  // 停止更新，保持白色
  if (millis() - lastFlashTime < 1000) return;
  lastFlashTime = millis();

  strip.clear();
  int ledPos = 0;
  for (const auto &entry : rankList) {
    if (ledPos >= LED_COUNT) break;
    int indexToShow = flashState % entry.userIndices.size();
    int userIdx = entry.userIndices[indexToShow];
    strip.setPixelColor(ledPos, userColors[userIdx]);
    ledPos++;
  }
  strip.show();
  flashState++;
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(Message)) {
    Message msg;
    memcpy(&msg, incomingData, sizeof(msg));
    if (msg.systemFinished) {
      systemReady = true;
      waitingToStart = true;  // ⬅️ 进入白灯等待状态
      for (int i = 0; i < LED_COUNT; i++) {
        strip.setPixelColor(i, strip.Color(255, 255, 255));
      }
      strip.show();
      updateDisplay();
      return;
    }
    if (msg.userIndex >= 0 && msg.userIndex < maxUsers && msg.colorIndex >= 0 && msg.colorIndex < 10) {
      userColors[msg.userIndex] = colorMap[msg.colorIndex];
      userActive[msg.userIndex] = true;
      strip.setPixelColor(msg.userIndex, userColors[msg.userIndex]);
      strip.show();
      updateDisplay();
    }
  } else if (len == sizeof(TaskMessage)) {
    TaskMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    if (msg.userIndex >= 0 && msg.userIndex < maxUsers) {
      userTaskCounts[msg.userIndex] = msg.taskCount;
      waitingToStart = false;  // ⬅️ 收到第一个任务后退出白灯状态
      buildRankList();
      updateDisplay();
      updateStrip();
    }
  }
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  Wire.begin(32, 33);
  WiFi.mode(WIFI_STA);

  if (!display.begin(0x3C, true)) {
    Serial.println("❌ OLED init failed");
    return;
  }
  display.setRotation(1);
  display.clearDisplay();
  display.display();

  strip.begin();
  strip.setBrightness(80);
  strip.clear();
  strip.show();

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("✅ Device2 Ready");
  updateDisplay();
}

void loop() {
  updateStrip();  // 闪烁控制（如果不在等待中）
  delay(10);
}