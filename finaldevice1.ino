#include <M5StickCPlus.h>
#include "Unit_Encoder.h"
#include <esp_now.h>
#include <WiFi.h>

#define IR_PIN 0
#define BUZZER_FREQ 3000
#define COUNTDOWN_DURATION 30000  // Task countdown 30 seconds

Unit_Encoder sensor;

// Array of available colors
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
bool colorsUsed[10] = { false };
int currentUserIndex = 0;

// System state definitions
enum SystemState {
  SYS_IDLE,           // Waiting for IR trigger
  SYS_SELECTING,      // Selecting color
  SYS_USER_CONFIRMED, // User confirmed (display for 1 second)
  SYS_FINISH_PROMPT,  // Finish sign-in prompt (double press)
  SYS_WAIT_START,     // Waiting for button to start task
  SYS_COUNTING,       // Task data entry phase
  SYS_FINISHED        // Task finished (settlement state)
};
SystemState systemState = SYS_IDLE;

// ESP-NOW target broadcast address
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

// Time variables for state transitions
unsigned long countdownStartTime = 0;
bool countdownStarted = false;
unsigned long userConfirmedTime = 0;     // Start time for SYS_USER_CONFIRMED
unsigned long finishPromptStartTime = 0;   // Start time for SYS_FINISH_PROMPT

// ---------------- Data sending functions ----------------
void sendMessage(int userIdx, int colorIdx, bool finished) {
  Message msg = {userIdx, colorIdx, finished};
  esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(msg));
}
void sendTaskCount(int userIdx, int taskCount) {
  TaskMessage task = {userIdx, taskCount};
  esp_now_send(broadcastAddress, (uint8_t*)&task, sizeof(task));
}

// ---------------- ESP-NOW receive callback ----------------
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if(len == sizeof(Message)) {
    Message msg;
    memcpy(&msg, incomingData, sizeof(msg));
    if(msg.systemFinished) {
      systemState = SYS_FINISHED;
      updateDisplay();
    }
  }
}

// ---------------- Display functions ----------------
void showFinishedScreen() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.println("Time's Up");
  M5.Lcd.setCursor(10, 70);
  M5.Lcd.println("FINISHED!");
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

// ---------------- Main display update function ----------------
void updateDisplay() {
  if(systemState == SYS_IDLE) {
    // Waiting for IR trigger
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 30);
    M5.Lcd.println("Waiting for");
    M5.Lcd.setCursor(10, 60);
    M5.Lcd.println("New Task...");
  }
  else if(systemState == SYS_WAIT_START) {
    // Display "PRESS TO START" and "Rotate to select your colour"
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("PRESS TO START");
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("Rotate to select");
    M5.Lcd.setCursor(10, 60);
    M5.Lcd.println("your colour");
  }
  else if(systemState == SYS_SELECTING) {
    // Color selection phase
    User &u = users[currentUserIndex];
    if(u.colorIndex != -1)
      M5.Lcd.fillScreen(colors[u.colorIndex]);
    else
      M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(0, 10);
    M5.Lcd.println("Rotate to select");
    M5.Lcd.setCursor(0, 35);
    M5.Lcd.printf("%s", users[currentUserIndex].name.c_str());
    M5.Lcd.setCursor(0, 70);
    M5.Lcd.println("PRESS ONCE:");
    M5.Lcd.setCursor(0, 90);
    M5.Lcd.println("USER CONFIRM");
  }
  else if(systemState == SYS_USER_CONFIRMED) {
    // After button press, display "User Confirmed" for 1 second
    User &u = users[currentUserIndex];
    M5.Lcd.fillScreen(colors[u.colorIndex]);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(0, 10);
    M5.Lcd.printf("%s", u.name.c_str());
    M5.Lcd.setCursor(0, 60);
    M5.Lcd.println("User Confirmed");
  }
  else if(systemState == SYS_FINISH_PROMPT) {
    // Double press prompt with 3-second countdown
    static int lastRemaining = -1;
    unsigned long elapsed = millis() - finishPromptStartTime;
    int remaining = 3 - (elapsed / 1000);
    if (remaining < 0)
      remaining = 0;
    if (remaining != lastRemaining) {
      User &u = users[currentUserIndex];
      M5.Lcd.fillScreen(colors[u.colorIndex]);
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.println("DOUBLE PRESS:");
      M5.Lcd.setCursor(0, 60);
      M5.Lcd.println("FINISH ALL SIGN IN");
      M5.Lcd.setCursor(0, 90);
      M5.Lcd.printf("Time: %d", remaining);
      lastRemaining = remaining;
    }
  }
  else if(systemState == SYS_COUNTING) {
    // Data entry phase
    showUserScreen(currentUserIndex);
  }
  else if(systemState == SYS_FINISHED) {
    showFinishedScreen();
  }
}

// ---------------- Encoder handling function ----------------
int getNextAvailableColor(int current, int dir) {
  if(current == -1) {
    for(int i = 0; i < 10; i++) {
      int check = (dir == 1) ? i : 9 - i;
      if(!colorsUsed[check])
        return check;
    }
    return -1;
  }
  for(int i = 1; i < 10; i++) {
    int next = (current + dir * i + 10) % 10;
    if(!colorsUsed[next])
      return next;
  }
  return -1;
}

void handleEncoder() {
  static int lastEncoder = 0;
  int encoder = sensor.getEncoderValue();
  int delta = encoder - lastEncoder;
  lastEncoder = encoder;
  if(delta == 0)
    return;
  
  if(systemState == SYS_SELECTING) {
    static int selectAccum = 0;
    selectAccum += delta;
    if(abs(selectAccum) >= 2) {
      int steps = selectAccum / 2;
      selectAccum -= steps * 2;
      User &u = users[currentUserIndex];
      if(!u.isLocked) {
        for(int i = 0; i < abs(steps); i++) {
          int newColor = getNextAvailableColor(u.colorIndex, (steps > 0) ? 1 : -1);
          if(newColor != -1) {
            u.colorIndex = newColor;
          }
        }
        updateDisplay();
      }
    }
  }
  else if(systemState == SYS_COUNTING) {
    static int stepCount = 0;
    stepCount += delta;
    if(abs(stepCount) >= 2) {
      int dir = (stepCount > 0) ? 1 : -1;
      for(int i = 1; i <= 10; i++) {
        int next = (currentUserIndex + dir * i + 10) % 10;
        if(users[next].isLocked) {
          currentUserIndex = next;
          showUserScreen(currentUserIndex);
          stepCount = 0;
          break;
        }
      }
    }
  }
}

// ---------------- Button handling function ----------------
void handleButton() {
  static bool lastBtn = false;
  bool btn = sensor.getButtonStatus();
  bool pressed = btn && !lastBtn;
  lastBtn = btn;
  if(!pressed)
    return;
  
  static unsigned long lastPress = 0;
  unsigned long now = millis();
  
  if(systemState == SYS_SELECTING) {
    User &u = users[currentUserIndex];
    if(u.colorIndex != -1 && !u.isLocked) {
      u.isLocked = true;
      colorsUsed[u.colorIndex] = true;
      sendMessage(currentUserIndex, u.colorIndex, false);
      systemState = SYS_USER_CONFIRMED;
      userConfirmedTime = now;
      updateDisplay();
    }
    lastPress = now;
  }
  else if(systemState == SYS_FINISH_PROMPT) {
    if(now - lastPress < 500) {
      sendMessage(-1, -1, true);  // Send finish sign-in signal
      systemState = SYS_WAIT_START;
      updateDisplay();
    }
    lastPress = now;
  }
  else if(systemState == SYS_WAIT_START) {
    systemState = SYS_COUNTING;
    currentUserIndex = 0;
    countdownStartTime = now;
    countdownStarted = true;
    showUserScreen(currentUserIndex);
    updateDisplay();
    lastPress = now;
  }
  else if(systemState == SYS_COUNTING) {
    User &u = users[currentUserIndex];
    if(u.isLocked) {
      u.taskCount++;
      sendTaskCount(currentUserIndex, u.taskCount);
      showSuccessScreen(currentUserIndex);
      delay(500);
      systemState = SYS_WAIT_START;
      updateDisplay();
    }
    lastPress = now;
  }
}

// ---------------- IR detection function ----------------
void handleIR() {
  static bool lastState = true;
  static bool objectRemoved = false;  // Flag indicating whether object has been removed
  bool detected = (digitalRead(IR_PIN) == LOW);
  
  if(systemState == SYS_IDLE) {
    // In initial state, when a new object is detected, start color selection
    if(detected && !lastState) {
      Serial.println("IR Triggered! Entering selection mode.");
      systemState = SYS_SELECTING;
      updateDisplay();
    }
  }
  else if(systemState == SYS_FINISHED) {
    // In finished state, if object is removed, then when a new object is detected, reset device
    if(!detected) {
      objectRemoved = true;
    }
    if(detected && objectRemoved) {
      Serial.println("New object detected after removal. Resetting device.");
      // Reset user data and system state
      for (int i = 0; i < 10; i++) {
        users[i].isLocked = false;
        users[i].taskCount = 0;
        users[i].colorIndex = -1;
        users[i].name = "User" + String(i+1);
      }
      for (int i = 0; i < 10; i++) {
        colorsUsed[i] = false;
      }
      currentUserIndex = 0;
      systemState = SYS_IDLE;
      updateDisplay();
      objectRemoved = false;
    }
  }
  
  lastState = detected;
}

// ---------------- Find next user ----------------
void gotoNextUser() {
  bool existsUnlocked = false;
  for (int i = 0; i < 10; i++) {
    if (!users[i].isLocked) {
      existsUnlocked = true;
      break;
    }
  }
  if (existsUnlocked) {
    for (int i = 1; i <= 10; i++) {
      int next = (currentUserIndex + i) % 10;
      if (!users[next].isLocked) {
        currentUserIndex = next;
        systemState = SYS_SELECTING;
        updateDisplay();
        return;
      }
    }
  }
  else {
    systemState = SYS_COUNTING;
    updateDisplay();
  }
}

// ---------------- Setup ----------------
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
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Message Sent" : "Send Failed");
  });
  esp_now_register_recv_cb(OnDataRecv);
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  
  updateDisplay();
}

// ---------------- Loop ----------------
void loop() {
  handleIR();
  handleEncoder();
  handleButton();
  
  unsigned long now = millis();
  
  if(systemState == SYS_USER_CONFIRMED) {
    if(now - userConfirmedTime >= 1000) {
      systemState = SYS_FINISH_PROMPT;
      finishPromptStartTime = now;
      updateDisplay();
    }
  }
  else if(systemState == SYS_FINISH_PROMPT) {
    if(now - finishPromptStartTime >= 3000) {
      gotoNextUser();
    }
    else {
      updateDisplay();
    }
  }
  
  if(countdownStarted && systemState == SYS_COUNTING) {
    unsigned long elapsed = now - countdownStartTime;
    if(elapsed >= COUNTDOWN_DURATION) {
      systemState = SYS_FINISHED;
      countdownStarted = false;
      showFinishedScreen();
    }
  }
  
  if(systemState == SYS_SELECTING ||
     systemState == SYS_USER_CONFIRMED ||
     systemState == SYS_FINISH_PROMPT) {
    M5.Beep.tone(BUZZER_FREQ);
  }
  else {
    M5.Beep.mute();
  }
  
  delay(20);
}
