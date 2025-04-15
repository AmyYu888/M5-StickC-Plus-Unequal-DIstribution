#include <M5StickCPlus.h>
#include "Unit_Encoder.h"
#include <esp_now.h>
#include <WiFi.h>

#define IR_PIN 0
#define BUZZER_FREQ 3000

Unit_Encoder sensor;

const uint16_t colors[10] = {
  TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN,
  TFT_MAGENTA, TFT_ORANGE, TFT_PURPLE, TFT_PINK, 0x996600
};

struct User {
  String name;
  int colorIndex;
  bool isLocked;
  int taskCount;
};
User users[10];
bool colorsUsed[10] = {false};
int currentUserIndex = 0;

enum SystemState { SYS_IDLE, SYS_SELECTING, SYS_WAIT_START, SYS_COUNTING };
SystemState systemState = SYS_IDLE;

uint8_t broadcastAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x8B, 0x70};

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

void sendMessage(int userIdx, int colorIdx, bool finished) {
  Message msg = {userIdx, colorIdx, finished};
  esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(msg));
}

void sendTaskCount(int userIdx, int taskCount) {
  TaskMessage task = {userIdx, taskCount};
  esp_now_send(broadcastAddress, (uint8_t*)&task, sizeof(task));
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
  M5.Lcd.fillScreen(colors[users[index].colorIndex]);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("User %d", index + 1);
}

void showSuccessScreen(int index) {
  M5.Lcd.fillScreen(colors[users[index].colorIndex]);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Successful!");
}

void updateDisplay() {
  if (systemState == SYS_IDLE) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Waiting for");
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("IR detection...");
  } else if (systemState == SYS_WAIT_START) {
    showStartScreen();
  } else if (systemState == SYS_SELECTING) {
    User& u = users[currentUserIndex];
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.printf("%s", u.name.c_str());
    if (u.colorIndex != -1) {
      M5.Lcd.fillRect(30, 50, 80, 30, colors[u.colorIndex]);
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

void handleEncoder() {
  static int lastEncoder = 0;
  static int stepCount = 0;
  int encoder = sensor.getEncoderValue();
  int delta = encoder - lastEncoder;
  lastEncoder = encoder;
  if (delta == 0) return;

  if (systemState == SYS_SELECTING) {
    User& u = users[currentUserIndex];
    if (!u.isLocked) {
      int newColor = getNextAvailableColor(u.colorIndex, delta > 0 ? 1 : -1);
      if (newColor != -1) {
        u.colorIndex = newColor;
        updateDisplay();
      }
    }
  }

  else if (systemState == SYS_COUNTING) {
    stepCount += delta;
    if (abs(stepCount) >= 2) {
      int dir = (stepCount > 0) ? 1 : -1;
      for (int i = 1; i <= 10; i++) {
        int next = (currentUserIndex + dir * i + 10) % 10;
        if (users[next].isLocked) {
          currentUserIndex = next;
          showUserScreen(currentUserIndex);
          stepCount = 0;
          break;
        }
      }
    }
  }
}

void handleButton() {
  static bool lastBtn = false;
  bool btn = sensor.getButtonStatus();
  bool pressed = btn && !lastBtn;
  lastBtn = btn;
  if (!pressed) return;

  static unsigned long lastPress = 0;
  unsigned long now = millis();

  if (systemState == SYS_SELECTING) {
    if (now - lastPress < 500) {
      sendMessage(-1, -1, true);
      M5.Beep.mute();
      systemState = SYS_WAIT_START;
      updateDisplay();
    } else {
      User &u = users[currentUserIndex];
      if (u.colorIndex != -1 && !u.isLocked) {
        u.isLocked = true;
        colorsUsed[u.colorIndex] = true;
        sendMessage(currentUserIndex, u.colorIndex, false);
        for (int i = 1; i <= 10; i++) {
          int next = (currentUserIndex + i) % 10;
          if (!users[next].isLocked) {
            currentUserIndex = next;
            updateDisplay();
            return;
          }
        }
        sendMessage(-1, -1, true);
        M5.Beep.mute();
        systemState = SYS_WAIT_START;
        updateDisplay();
      }
    }
    lastPress = now;
  }

  else if (systemState == SYS_WAIT_START) {
    systemState = SYS_COUNTING;
    currentUserIndex = 0;
    while (!users[currentUserIndex].isLocked) {
      currentUserIndex = (currentUserIndex + 1) % 10;
    }
    showUserScreen(currentUserIndex);
  }

  else if (systemState == SYS_COUNTING) {
    User &u = users[currentUserIndex];
    if (u.isLocked) {
      u.taskCount++;
      sendTaskCount(currentUserIndex, u.taskCount);
      showSuccessScreen(currentUserIndex);
      delay(500);
      showStartScreen();
      systemState = SYS_WAIT_START;
    }
  }
}

void handleIR() {
  static bool lastState = true;
  bool detected = digitalRead(IR_PIN) == LOW;
  if (detected && !lastState && systemState == SYS_IDLE) {
    Serial.println("✅ IR Triggered! Entering selection mode.");
    systemState = SYS_SELECTING;
    M5.Beep.tone(BUZZER_FREQ);
    updateDisplay();
  }
  lastState = detected;
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

  for (int i = 0; i < 10; i++) {
    users[i] = {"User" + String(i + 1), -1, false, 0};
  }

  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_send_cb([](const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✅ Message Sent!" : "❌ Send Failed!");
  });

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  updateDisplay();
}

void loop() {
  handleIR();
  handleEncoder();
  handleButton();
  if (systemState == SYS_SELECTING) {
    M5.Beep.tone(BUZZER_FREQ);
  } else {
    M5.Beep.mute();
  }
  delay(20);
}