#include <M5StickCPlus.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SH110X.h>

#define LED_PIN 26
#define LED_COUNT 4
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// OLED SH1107
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

const uint32_t userColors[4] = {
  strip.Color(255, 0, 0),      // Red
  strip.Color(255, 165, 0),    // Orange
  strip.Color(255, 255, 0),    // Yellow
  strip.Color(0, 255, 0)       // Green
};

int userTaskCounts[4] = {0};

struct struct_message {
  int userIndex;
  int taskCount;
};

// 排名信息结构体
struct RankEntry {
  int users[4];
  int userCount;
};

RankEntry ranks[4]; // 每个灯位可有多个用户交替显示

unsigned long lastSwapTime = 0;
int colorIndex[4] = {0, 0, 0, 0}; // 每个LED位当前正在显示的颜色索引

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("User Task Counts:");
  for (int i = 0; i < 4; i++) {
    display.setCursor(0, 12 + i * 12);
    display.printf("User %d: %d", i + 1, userTaskCounts[i]);
  }
  display.display();
}

void buildRank() {
  int sorted[4] = {0, 1, 2, 3};
  for (int i = 0; i < 3; i++) {
    for (int j = i + 1; j < 4; j++) {
      if (userTaskCounts[sorted[j]] > userTaskCounts[sorted[i]]) {
        int temp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = temp;
      }
    }
  }

  int pos = 0;
  int currentCount = -1;
  for (int i = 0; i < 4; i++) {
    int user = sorted[i];
    if (i == 0 || userTaskCounts[user] != currentCount) {
      ranks[pos].users[0] = user;
      ranks[pos].userCount = 1;
      currentCount = userTaskCounts[user];
      pos++;
    } else {
      ranks[pos - 1].users[ranks[pos - 1].userCount] = user;
      ranks[pos - 1].userCount++;
    }
  }

  // 清空剩余位置
  for (int i = pos; i < 4; i++) {
    ranks[i].userCount = 0;
  }
}

void updateStrip() {
  unsigned long now = millis();
  if (now - lastSwapTime < 1000) return; // 每 1 秒刷新一次
  lastSwapTime = now;

  for (int i = 0; i < 4; i++) {
    if (ranks[i].userCount == 0) {
      strip.setPixelColor(i, 0); // 关闭
    } else {
      int uidx = ranks[i].users[colorIndex[i] % ranks[i].userCount];
      strip.setPixelColor(i, userColors[uidx]);
      colorIndex[i]++;
    }
  }
  strip.show();
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  struct_message msg;
  memcpy(&msg, incomingData, sizeof(msg));
  if (msg.userIndex >= 0 && msg.userIndex < 4) {
    userTaskCounts[msg.userIndex] = msg.taskCount;
    updateDisplay();
    buildRank();  // 重新计算排名与并列
    updateStrip(); // 立即更新一次（否则需等1秒）
  }
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  Wire.begin(32, 33); // OLED 默认引脚
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED not found");
    return;
  }
  display.setRotation(1);
  display.clearDisplay();
  display.display();

  strip.begin();
  strip.setBrightness(80);
  strip.clear();
  strip.show();

  updateDisplay();
  buildRank();
}

void loop() {
  updateStrip();  // 持续轮换显示
  delay(50);
}