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
uint32_t userColors[maxUsers];      // Color for each user
int userTaskCounts[maxUsers] = {0};   // Number of tasks completed by each user
bool userActive[maxUsers] = {false};  // Whether each user is active

bool systemReady = false;
bool waitingToStart = false;
bool countdownRunning = false;
bool countdownFinished = false;
bool playingImperialMarch = false;    // Currently playing Imperial March
bool imperialMarchStarted = false;    // Imperial March finished; entered settlement stage
bool taskStarted = false;

bool selectionFinished = false;
unsigned long selectionFinishedTime = 0;

unsigned long countdownStartTime = 0;
const int countdownSeconds = 60;
unsigned long lastToggleTime = 0;
bool showCountdownScreen = true;

const uint32_t colorMap[10] = {
  strip.Color(255,0,0), strip.Color(0,255,0), strip.Color(0,0,255),
  strip.Color(255,255,0), strip.Color(0,255,255), strip.Color(255,0,255),
  strip.Color(255,165,0), strip.Color(128,0,128), strip.Color(255,192,203), strip.Color(153,102,0)
};

uint8_t device1Address[] = {0xE8,0x9F,0x6D,0x0A,0x49,0x9C};

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

struct RankEntry {
  int taskCount;
  std::vector<int> userIndices;
};
std::vector<RankEntry> rankList;

unsigned long imperialMarchFinishTime = 0;

int impNoteIndex = 0;
unsigned long impNoteStart = 0;
const int impTotalNotes = 18;
int imperialMelody[impTotalNotes] = {
  440, 440, 440, 349, 523, 440, 349, 523, 440,
  659, 659, 659, 698, 523, 415, 349, 523, 440
};
int imperialDurations[impTotalNotes] = {
  500, 500, 500, 350, 150, 500, 350, 150, 1000,
  500, 500, 500, 350, 150, 500, 350, 150, 1000
};

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
    if (!found)
      rankList.push_back({count, {i}});
  }
  std::sort(rankList.begin(), rankList.end(), [](RankEntry &a, RankEntry &b) {
    return a.taskCount > b.taskCount;
  });
}

void updateOLEDActiveUsers() {
  int activeCount = 0;
  for (int i = 0; i < maxUsers; i++) {
    if (userActive[i])
      activeCount++;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(5, 30);
  display.printf("%d/10 Active", activeCount);
  display.display();
}

void updateDisplayCountdown() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  int remaining = countdownSeconds - (millis() - countdownStartTime) / 1000;
  display.setCursor(10, 30);
  display.printf("%02d s left", remaining);
  display.display();
}

void updateDisplayTasks() {
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

void updateStripFlashing() {
  if (playingImperialMarch || imperialMarchStarted) return;
  if (!systemReady || countdownFinished || waitingToStart || !taskStarted)
    return;
  
  buildRankList();
  strip.clear();
  int pos = 0;
  for (const auto &entry : rankList) {
    if (pos >= LED_COUNT) break;
    int groupSize = entry.userIndices.size();
    int selectedIndex = (groupSize > 1) ? ((millis() / 500) % groupSize) : 0;
    uint32_t color = userColors[entry.userIndices[selectedIndex]];
    strip.setPixelColor(pos, color);
    pos++;
  }
  for (; pos < LED_COUNT; pos++) {
    strip.setPixelColor(pos, 0);
  }
  strip.show();
}

void showLastPlaceFlashing() {
  buildRankList();
  if (rankList.empty()) return;
  const auto &last = rankList.back();
  if (last.userIndices.empty()) return;
  int groupSize = last.userIndices.size();
  int selectedIndex = (groupSize > 1) ? ((millis() / 500) % groupSize) : 0;
  
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.setPixelColor(LED_COUNT / 2, userColors[last.userIndices[selectedIndex]]);
  strip.show();
}

void showWhiteStrip() {
  if (playingImperialMarch || imperialMarchStarted) return;
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(255, 255, 255));
  }
  strip.show();
}

void showFirstPlaceLED() {
  buildRankList();
  strip.clear();
  if (rankList.empty()) return;
  const auto &first = rankList[0];
  int userCount = first.userIndices.size();
  int ledsPerUser = LED_COUNT / userCount;
  int extra = LED_COUNT % userCount;
  int index = 0;
  for (int i = 0; i < userCount; i++) {
    int count = ledsPerUser + (i < extra ? 1 : 0);
    uint32_t color = userColors[first.userIndices[i]];
    for (int j = 0; j < count && index < LED_COUNT; j++) {
      strip.setPixelColor(index++, color);
    }
  }
  strip.show();
}

void playTwinkleMelody() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SH110X_WHITE);
  String text = "WINNER";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((display.width() - w) / 2, (display.height() - h) / 2);
  display.println(text);
  display.display();
  delay(500);
  
  int melody[] = {262, 262, 392, 392, 440, 440, 392, 349, 349, 330, 330, 294, 294, 262};
  int noteDurations[] = {300, 300, 300, 300, 300, 300, 600, 300, 300, 300, 300, 300, 300, 600};
  for (int i = 0; i < 14; i++){
    ledcWriteTone(0, melody[i]);
    ledcWrite(0, 255);
    delay(noteDurations[i]);
    ledcWriteTone(0, 0);
    delay(50);
  }
}  // End of playTwinkleMelody function

// Non-blocking playback of the Imperial March: display "LOSER" and then start playing the buzzer tone
void startImperialMarch() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SH110X_WHITE);
  String text = "LOSER";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((display.width() - w) / 2, (display.height() - h) / 2);
  display.println(text);
  display.display();
  delay(500);
  
  impNoteIndex = 0;
  impNoteStart = millis();
  playingImperialMarch = true;
}

void updateImperialMarch() {
  if (!playingImperialMarch)
    return;
  
  if (impNoteIndex < impTotalNotes) {
    if (millis() - impNoteStart >= (unsigned long) imperialDurations[impNoteIndex]) {
      impNoteIndex++;
      impNoteStart = millis();
    }
    else {
      ledcWriteTone(0, imperialMelody[impNoteIndex]);
      ledcWrite(0, 255);
    }
  }
  else {
    ledcWriteTone(0, 0);
    playingImperialMarch = false;
    imperialMarchStarted = true;  // Enter settlement stage
    imperialMarchFinishTime = millis(); // Record finish time
  }
}

void handleCountdownFinish() {
  countdownRunning = false;
  countdownFinished = true;
  updateDisplayTasks();
  showFirstPlaceLED();
  playTwinkleMelody();
  delay(1000);
  startImperialMarch();
  Message endSignal = {-1, -1, true};
  esp_now_send(device1Address, (uint8_t*)&endSignal, sizeof(endSignal));
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(Message)) {
    Message msg;
    memcpy(&msg, incomingData, sizeof(msg));
    if (msg.systemFinished) {
      systemReady = true;
      waitingToStart = true;
      countdownRunning = true;
      countdownFinished = false;
      countdownStartTime = millis();
      lastToggleTime = millis();
      showCountdownScreen = true;
      taskStarted = false;
      selectionFinished = true;
      selectionFinishedTime = millis();
      buildRankList();
    }
    if (msg.userIndex >= 0 && msg.userIndex < maxUsers &&
        msg.colorIndex >= 0 && msg.colorIndex < 10) {
      userColors[msg.userIndex] = colorMap[msg.colorIndex];
      userActive[msg.userIndex] = true;
      updateOLEDActiveUsers();
      if (!playingImperialMarch && !imperialMarchStarted) {
        strip.setPixelColor(msg.userIndex, userColors[msg.userIndex]);
        strip.show();
      }
      buildRankList();
    }
  } else if (len == sizeof(TaskMessage)) {
    TaskMessage msg;
    memcpy(&msg, incomingData, sizeof(msg));
    if (msg.userIndex >= 0 && msg.userIndex < maxUsers) {
      userTaskCounts[msg.userIndex] = msg.taskCount;
      taskStarted = true;
      waitingToStart = false;
      buildRankList();
    }
  }
}

void resetDevice2() {
  // Reset all variables and display/LED state
  systemReady = false;
  waitingToStart = false;
  countdownRunning = false;
  countdownFinished = false;
  imperialMarchStarted = false;
  taskStarted = false;
  
  strip.clear();
  strip.show();
  display.clearDisplay();
  display.display();
  
  for (int i = 0; i < maxUsers; i++) {
    userTaskCounts[i] = 0;
    userActive[i] = false;
    userColors[i] = 0;
  }
  rankList.clear();
  Serial.println("Device2 reset to initial state.");
}

void setup(){
  Serial.begin(115200);
  M5.begin();
  Wire.begin(32, 33);
  WiFi.mode(WIFI_STA);
  display.begin(0x3C, true);
  display.setRotation(1);
  display.clearDisplay();
  display.display();
  strip.begin();
  strip.setBrightness(80);
  strip.clear();
  strip.show();
  ledcAttachPin(0, 0);
  ledcSetup(0, 1000, 8);
  esp_now_init();
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, device1Address, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop(){
  unsigned long now = millis();
  
  if (playingImperialMarch) {
    updateImperialMarch();
    showLastPlaceFlashing();
  }
  else if (countdownRunning) {
    if (now - countdownStartTime >= countdownSeconds * 1000) {
      handleCountdownFinish();
    }
    else {
      if (now - lastToggleTime >= 2000) {
        showCountdownScreen = !showCountdownScreen;
        lastToggleTime = now;
      }
      if (showCountdownScreen) {
        updateDisplayCountdown();
      }
      else {
        updateDisplayTasks();
      }
      
      if (selectionFinished && (now - selectionFinishedTime < 2000)) {
        showWhiteStrip();
      }
      else if (taskStarted) {
        updateStripFlashing();
      }
    }
  }
  else if (!systemReady) {
    updateOLEDActiveUsers();
  }
  else if (imperialMarchStarted) {
    showLastPlaceFlashing();
    if (millis() - imperialMarchFinishTime >= 3000) {
      resetDevice2();
    }
  }
  else {
    updateStripFlashing();
  }
  
  delay(10);
}
