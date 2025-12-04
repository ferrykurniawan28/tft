#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();
#define TOUCH_CS 15   // T_CS connected to GPIO 15
XPT2046_Touchscreen ts(TOUCH_CS);
#define BACKLIGHT 4

const int textSize = 2; // Default text size
const int screenWidth = 320; // Screen width in pixels
const int screenHeight = 240; // Screen height in pixels

// NTP config
#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET     25200  // WIB (UTC+7) = 7 * 3600 seconds
#define UTC_OFFSET_DST 0

bool rtcTimeSet = false;

// Serial communication with main ESP32
HardwareSerial SerialPort(2); // Use UART2

// Display states
enum DisplayState {
  STATE_HOME,
  STATE_CONTAINERS,
  STATE_REMINDERS,
  STATE_SCHEDULE,
  STATE_ALARM,
  STATE_DISPENSING
};

DisplayState currentState = STATE_HOME;
DisplayState previousState = STATE_HOME;

// Data storage
struct Container {
  int id;
  String medicine_name;
  int current_capacity;
  int max_capacity;
  bool low_stock;
};

struct Reminder {
  int id;
  String medicine_name;
  int container_id;
  String schedule_type;
  String times[5];  // Store up to 5 reminder times
  int timeCount;    // Number of times for this reminder
  bool active;
};

struct DailySchedule {
  String time;
  String medicine_name;
  int dosage;
  String status;
};

// Data arrays
Container containers[10];
Reminder reminders[20];
DailySchedule dailySchedule[24]; // 24 hours

//Dummy data
// containers[0] = {1, "Paracetamol", 50, 100, false};

int containerCount = 0;
int reminderCount = 0;
int scheduleCount = 0;

// Device status
bool wifiConnected = false;
bool mqttConnected = false;
bool timeSynced = false;
bool alarmActive = false;
String alarmType = "";
String alarmMessage = "";
String alarmTime = "";

// Dispensing status
bool isDispensing = false;
int dispensingContainer = 0;
int dispensingDosage = 0;
bool dispensingComplete = false;

// Sensor data
float currentTemperature = 0.0;
float currentHumidity = 0.0;

// Colors
#define BACKGROUND_COLOR 0x18E3
#define TEXT_COLOR TFT_WHITE
#define HIGHLIGHT_COLOR 0x07FF
#define WARNING_COLOR TFT_YELLOW
#define ALARM_COLOR TFT_RED
#define SUCCESS_COLOR TFT_GREEN

// Button positions for navigation
const int buttonWidth = 70;
const int buttonHeight = 30;
const int buttonMargin = 10;

void showStartupScreen();
void showStatusMessage(String message);
void showErrorMessage(String error);
void showReminderAlert(String medicineName, int containerId, int dosage, String alertType, String message, String timeStr);
void showControlQueueResult(int queueId, bool success, String message);
void syncContainers(JsonArray containersArray);
void syncReminders(JsonArray remindersArray);
void syncDailySchedule(JsonArray scheduleArray);
void processIncomingData(String jsonData);
void updateDisplay();
void handleTouchInput();
void drawHomeScreen();
void drawContainersScreen();
void drawRemindersScreen();
void drawScheduleScreen();
void drawAlarmScreen();
void drawDispensingScreen();
void handleHomeTouch(int x, int y);
void handleContainersTouch(int x, int y);
void handleRemindersTouch(int x, int y);
void handleAlarmTouch(int x, int y);
int getActiveContainerCount();
void handleScheduleTouch(int x, int y);
void handleDispensingTouch(int x, int y);
void drawContainerItem(int x, int y, Container container);
void drawReminderItem(int x, int y, Reminder reminder);
void drawButton(int x, int y, int w, int h, String label, uint16_t color);
int getActiveReminderCount();
void drawScheduleItem(int x, int y, DailySchedule schedule);
void syncTimeWithNTP();
void generateDummyData();
void sendDummyDeviceInfo();
void sendDummySystemStatus();
void sendDummySensorData();
void sendDummyContainersInfo();
void sendDummyRemindersInfo();
void sendDummyDailySchedule();
void sendDummyReminderAlert();
void sendDummyGroupedReminderAlert();
void sendDummyAlarmStatus(bool active);
void sendDummyDispensingStatus(const char* status);
void sendDummyStockAlert();


void setup() {
  Serial.begin(115200);
  SerialPort.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17

  // Initialize TFT
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(BACKGROUND_COLOR);
  
  // Initialize backlight
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, HIGH);
  
  // Initialize touch
  ts.begin();
  ts.setRotation(4);
  
  // Show startup screen
  showStartupScreen();
  
  Serial.println("TFT Display Ready");
  // syncTimeWithNTP();
}

void loop() {
  // Check for incoming serial data
  if (SerialPort.available()) {
    String jsonData = SerialPort.readStringUntil('\n');
    jsonData.trim();
    
    if (jsonData.length() > 0) {
      processIncomingData(jsonData);
    }
  }
  
  // Handle touch input
  handleTouchInput();
  
  // Update display based on current state
  updateDisplay();
  
  // Debug: Send dummy data every 30 seconds for testing
  static unsigned long lastDummyTime = 0;
  if (millis() - lastDummyTime > 30000) {
    lastDummyTime = millis();
    // Uncomment one of the dummy data functions below for testing
    generateDummyData(); // Sends all dummy data
    // sendDummyDeviceInfo();
    // sendDummySensorData();
    // sendDummySystemStatus();
    // sendDummyDailySchedule();
    // sendDummyReminderAlert();
    // sendDummyDispensingStatus("started");
  }
  
  delay(100);
}

void processIncomingData(String jsonData) {
  Serial.println("Received: " + jsonData);
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  
  String type = doc["type"] | "unknown";
  
  if (type == "status") {
    // General status update
    String message = doc["message"] | "";
    showStatusMessage(message);
    
  } else if (type == "sync_all_data") {
    // Full data sync
    wifiConnected = doc["wifi_connected"] | false;
    mqttConnected = doc["mqtt_connected"] | false;
    timeSynced = doc["time_synced"] | false;
    
    // Sync containers if available
    if (doc["containers"].is<JsonArray>()) {
      syncContainers(doc["containers"]);
    }
    
    // Sync reminders if available
    if (doc["reminders"].is<JsonArray>()) {
      syncReminders(doc["reminders"]);
    }
    
    // Sync daily schedule if available
    if (doc["daily_schedule"].is<JsonArray>()) {
      syncDailySchedule(doc["daily_schedule"]);
    }
    
    Serial.println("Full data sync completed");
    
  } else if (type == "containers_info") {
    // Container data update
    if (doc["containers"].is<JsonArray>()) {
      syncContainers(doc["containers"]);
    }
    
  } else if (type == "reminders_info") {
    // Reminder data update
    if (doc["reminders"].is<JsonArray>()) {
      syncReminders(doc["reminders"]);
    }
    
  } else if (type == "daily_schedule") {
    // Daily schedule update
    if (doc["schedule"].is<JsonArray>()) {
      syncDailySchedule(doc["schedule"]);
    }
    
  } else if (type == "sensor_data") {
    // Sensor data update
    currentTemperature = doc["temperature"] | 0.0;
    currentHumidity = doc["humidity"] | 0.0;
    
  } else if (type == "system_status") {
    // System status update
    String wifiStatus = doc["wifi_status"] | "disconnected";
    String mqttStatus = doc["mqtt_status"] | "disconnected";
    wifiConnected = (wifiStatus == "connected");
    mqttConnected = (mqttStatus == "connected");
    timeSynced = doc["rtc_time_set"] | false;
    currentTemperature = doc["temperature"] | 0.0;
    currentHumidity = doc["humidity"] | 0.0;
    
  } else if (type == "device_info") {
    // Device info
    currentTemperature = doc["temperature"] | 0.0;
    currentHumidity = doc["humidity"] | 0.0;
    
  } else if (type == "alarm_status") {
    // Alarm status
    alarmActive = doc["alarm_active"] | false;
    alarmType = doc["alarm_type"] | "";
    
    if (alarmActive) {
      currentState = STATE_ALARM;
    } else if (currentState == STATE_ALARM) {
      currentState = STATE_HOME;
    }
    
  } else if (type == "reminder_alert") {
    // Single reminder alert
    String medicineName = doc["medicine_name"] | "";
    int containerId = doc["container_id"] | doc["container_number"] | 0;
    int dosage = doc["dosage"] | 1;
    String alertType = doc["schedule_type"] | "reminder";
    String message = doc["notes"] | "";
    String timeStr = doc["reminder_time"] | "";
    
    showReminderAlert(medicineName, containerId, dosage, alertType, message, timeStr);
    
  } else if (type == "grouped_reminder_alert") {
    // Multiple reminders at same time
    currentState = STATE_ALARM;
    alarmActive = true;
    alarmType = "grouped_alert";
    
  } else if (type == "dispensing_status") {
    // Dispensing status
    String dispStatus = doc["status"] | "";
    isDispensing = (dispStatus == "started");
    dispensingContainer = doc["container_number"] | 0;
    dispensingDosage = doc["dosage"] | 0;
    dispensingComplete = (dispStatus == "completed");
    
    if (isDispensing || dispensingComplete) {
      currentState = STATE_DISPENSING;
    } else if (currentState == STATE_DISPENSING) {
      currentState = STATE_HOME;
    }
    
  } else if (type == "stock_alert") {
    // Stock alert
    String medicineName = doc["medicine_name"] | "";
    int current = doc["current_stock"] | 0;
    int minimum = doc["minimum_stock"] | 0;
    Serial.printf("Stock Alert: %s - Current: %d, Minimum: %d\n", medicineName.c_str(), current, minimum);
    
  } else if (type == "error") {
    // Error message
    String errorMsg = doc["message"] | "";
    showErrorMessage(errorMsg);
    
  } else if (type == "control_queue_complete") {
    // Control queue completion
    int queueId = doc["queue_id"] | 0;
    bool success = doc["success"] | false;
    String message = doc["message"] | "";
    
    showControlQueueResult(queueId, success, message);
  }
}

void syncContainers(JsonArray containersArray) {
  containerCount = 0;
  for (JsonVariant containerVar : containersArray) {
    if (containerCount >= 10) break;
    
    JsonObject container = containerVar.as<JsonObject>();
    containers[containerCount].id = container["id"] | 0;
    containers[containerCount].medicine_name = container["medicine_name"] | "Unknown";
    containers[containerCount].current_capacity = container["current_capacity"] | 0;
    containers[containerCount].max_capacity = container["max_capacity"] | 0;
    containers[containerCount].low_stock = container["low_stock"] | false;
    
    containerCount++;
  }
  Serial.printf("Synced %d containers\n", containerCount);
}

void syncReminders(JsonArray remindersArray) {
  reminderCount = 0;
  for (JsonVariant reminderVar : remindersArray) {
    if (reminderCount >= 20) break;
    
    JsonObject reminder = reminderVar.as<JsonObject>();
    reminders[reminderCount].id = reminder["id"] | 0;
    reminders[reminderCount].medicine_name = reminder["medicine_name"] | "Unknown";
    reminders[reminderCount].container_id = reminder["container_id"] | 0;
    reminders[reminderCount].schedule_type = reminder["schedule_type"] | "daily";
    reminders[reminderCount].active = reminder["active"] | false;
    
    // Extract times from times array
    reminders[reminderCount].timeCount = 0;
    if (reminder["times"].is<JsonArray>()) {
      JsonArray timesArray = reminder["times"];
      for (JsonVariant timeVar : timesArray) {
        if (reminders[reminderCount].timeCount >= 5) break;
        JsonObject timeObj = timeVar.as<JsonObject>();
        reminders[reminderCount].times[reminders[reminderCount].timeCount] = timeObj["time"] | "";
        reminders[reminderCount].timeCount++;
      }
    }
    
    reminderCount++;
  }
  Serial.printf("Synced %d reminders\n", reminderCount);
}

void syncDailySchedule(JsonArray scheduleArray) {
  scheduleCount = 0;
  for (JsonVariant scheduleVar : scheduleArray) {
    if (scheduleCount >= 24) break;
    
    JsonObject schedule = scheduleVar.as<JsonObject>();
    dailySchedule[scheduleCount].time = schedule["time"] | "";
    dailySchedule[scheduleCount].medicine_name = schedule["medicine_name"] | "";
    dailySchedule[scheduleCount].dosage = schedule["dosage"] | 1;
    dailySchedule[scheduleCount].status = schedule["status"] | "pending";
    
    scheduleCount++;
  }
  Serial.printf("Synced %d schedule items\n", scheduleCount);
}

void handleTouchInput() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    
    // Convert touch coordinates to screen coordinates
    int x = map(p.x, 200, 3700, 0, tft.width());
    int y = map(p.y, 240, 3800, 0, tft.height());

    Serial.printf("Touch at (%d, %d)\n", x, y);
    
    // Handle touch based on current state
    switch (currentState) {
      case STATE_HOME:
        handleHomeTouch(x, y);
        break;
      case STATE_CONTAINERS:
        handleContainersTouch(x, y);
        break;
      case STATE_REMINDERS:
        handleRemindersTouch(x, y);
        break;
      case STATE_SCHEDULE:
        handleScheduleTouch(x, y);
        break;
      case STATE_ALARM:
        handleAlarmTouch(x, y);
        break;
      case STATE_DISPENSING:
        handleDispensingTouch(x, y);
        break;
    }
    
    delay(300); // Debounce delay
  }
}

void handleHomeTouch(int x, int y) {
  // View All Reminders button (pink box area)
  int reminderButtonY = 120; // Approximate Y for "View All Reminders" button
  if (x >= 10 && x <= tft.width() - 20 && y >= reminderButtonY && y <= reminderButtonY + 30) {
    currentState = STATE_REMINDERS;
  }
  
  // View All Queues button (at bottom)
  int queueButtonY = 430; // Approximate Y for "View All Queues" button
  if (x >= 10 && x <= tft.width() - 20 && y >= queueButtonY && y <= queueButtonY + 30) {
    // Handle view all queues
    Serial.println("Clicked View All Queues");
  }
}

void handleContainersTouch(int x, int y) {
  // Back button (top left)
  if (x >= buttonMargin && x <= buttonMargin + 60 && y >= buttonMargin && y <= buttonMargin + 30) {
    currentState = STATE_HOME;
  }
}

void handleRemindersTouch(int x, int y) {
  // Back button (top left)
  if (x >= buttonMargin && x <= buttonMargin + 60 && y >= buttonMargin && y <= buttonMargin + 30) {
    currentState = STATE_HOME;
  }
}

void handleScheduleTouch(int x, int y) {
  // Back button (top left)
  if (x >= buttonMargin && x <= buttonMargin + 60 && y >= buttonMargin && y <= buttonMargin + 30) {
    currentState = STATE_HOME;
  }
}

void handleAlarmTouch(int x, int y) {
  // Dismiss alarm button (center)
  int buttonX = tft.width() / 2 - 50;
  int buttonY = tft.height() - 80;
  if (x >= buttonX && x <= buttonX + 100 && y >= buttonY && y <= buttonY + 40) {
    currentState = STATE_HOME;
    alarmActive = false;
  }
}

void handleDispensingTouch(int x, int y) {
  // OK button (center bottom)
  if (dispensingComplete) {
    int buttonX = tft.width() / 2 - 40;
    int buttonY = tft.height() - 60;
    if (x >= buttonX && x <= buttonX + 80 && y >= buttonY && y <= buttonY + 30) {
      currentState = STATE_HOME;
    }
  }
}

void updateDisplay() {
  if (currentState != previousState) {
    // Clear screen and redraw for new state
    tft.fillScreen(BACKGROUND_COLOR);
    previousState = currentState;
  }
  
  switch (currentState) {
    case STATE_HOME:
      drawHomeScreen();
      break;
    case STATE_CONTAINERS:
      drawContainersScreen();
      break;
    case STATE_REMINDERS:
      drawRemindersScreen();
      break;
    case STATE_SCHEDULE:
      drawScheduleScreen();
      break;
    case STATE_ALARM:
      drawAlarmScreen();
      break;
    case STATE_DISPENSING:
      drawDispensingScreen();
      break;
  }
}

void showStartupScreen() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  
  tft.setCursor(tft.width() / 2 - 80, tft.height() / 2 - 20);
  tft.print("MINDER DEVICE");
  
  tft.setTextSize(1);
  tft.setCursor(tft.width() / 2 - 40, tft.height() / 2 + 20);
  tft.print("Starting...");
  
  delay(2000);
  tft.fillScreen(BACKGROUND_COLOR);
}

void drawHomeScreen() {
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%d.%m %A %Y", timeinfo);
  tft.print(buffer);
  
  // Connection status
  tft.setTextSize(1);
  tft.setCursor(160, 10);
  tft.setTextColor(TEXT_COLOR);
  tft.print("WiFi: ");
  tft.setTextColor(wifiConnected ? SUCCESS_COLOR : WARNING_COLOR);
  tft.print(wifiConnected ? "C" : "D");
  
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(160, 20);
  tft.print("MQTT: ");
  tft.setTextColor(mqttConnected ? SUCCESS_COLOR : WARNING_COLOR);
  tft.print(mqttConnected ? "C" : "D");
  
  // Sensor data
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(250, 10);
  tft.printf("Temp: %.1fC", currentTemperature);
  tft.setCursor(250, 20);
  tft.printf("Hum: %.1f%%", currentHumidity);
  
  // List upcoming 2 reminders
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 40);
  tft.print("Upcoming Reminders:");

  int reminderCount_shown = 0;
  int yPos = 60;
  for (int i = 0; i < reminderCount && reminderCount_shown < 2; i++) {
    if (reminders[i].active) {
      // Draw square box with pink background
      uint16_t pinkColor = 0xF81F; // Pink color
      tft.fillRect(10, yPos, tft.width() - 20, 35, pinkColor);
      tft.drawRect(10, yPos, tft.width() - 20, 35, TEXT_COLOR);
      
      // Draw text
      tft.setTextColor(TEXT_COLOR);
      tft.setTextSize(2);
      tft.setCursor(20, yPos + 10);
      
      // Format: Medicine Name: Dosage Time
      tft.printf("%s:%d %s", 
                 reminders[i].medicine_name.c_str(), 
                 1,  // dosage (adjust if you have this in reminder struct)
                 "14:00");  // time (adjust if you have this in reminder struct)
      
      yPos += 40;
      reminderCount_shown++;
    }
  }
  if (reminderCount_shown == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(10, 70);
    tft.print("No active reminders");
  } else {
    // "Reminders" button
    drawButton(10, yPos + 5, tft.width() - 20, buttonHeight, "View All Reminders", HIGHLIGHT_COLOR);
  }

  // 2x2 Grid of Containers
  int gridX1 = 5, gridX2 = 165;
  int gridY1 = 180, gridY2 = 255;
  int gridW = 150, gridH = 70;
  
  // Container 1 (Top-Left)
  if (containerCount > 0) {
    tft.drawRect(gridX1, gridY1, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX1 + 8, gridY1 + 8);
    tft.printf("Container 1");
    tft.setCursor(gridX1 + 8, gridY1 + 25);
    tft.printf("%s", containers[0].medicine_name.c_str());
    tft.setCursor(gridX1 + 8, gridY1 + 40);
    tft.printf("Stock: %d", containers[0].current_capacity);
    if (containers[0].low_stock) {
      tft.setTextColor(WARNING_COLOR);
      tft.setCursor(gridX1 + 8, gridY1 + 70);
      tft.print("LOW STOCK");
    }
  } else {
    //shows empty
    tft.drawRect(gridX1, gridY1, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX1 + 8, gridY1 + 8);
    tft.printf("Container 1");
    tft.setCursor(gridX1 + 8, gridY1 + 25);
    // tft.printf("Empty");
  }
  
  // Container 2 (Top-Right)
  if (containerCount > 1) {
    tft.drawRect(gridX2, gridY1, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX2 + 8, gridY1 + 8);
    tft.printf("Container 2");
    tft.setCursor(gridX2 + 8, gridY1 + 25);
    tft.printf("%s", containers[1].medicine_name.c_str());
    tft.setCursor(gridX2 + 8, gridY1 + 40);
    tft.printf("Stock: %d", containers[1].current_capacity);
    if (containers[1].low_stock) {
      tft.setTextColor(WARNING_COLOR);
      tft.setCursor(gridX2 + 8, gridY1 + 70);
      tft.print("LOW STOCK");
    }
  } else {
    //shows empty
    tft.drawRect(gridX2, gridY1, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX2 + 8, gridY1 + 8);
    tft.printf("Container 2");
    tft.setCursor(gridX2 + 8, gridY1 + 25);
    // tft.printf("Empty");
  }
  
  // Container 3 (Bottom-Left)
  if (containerCount > 2) {
    tft.drawRect(gridX1, gridY2, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX1 + 8, gridY2 + 8);
    tft.printf("Container 3");
    tft.setCursor(gridX1 + 8, gridY2 + 25);
    tft.printf("%s", containers[2].medicine_name.c_str());
    tft.setCursor(gridX1 + 8, gridY2 + 40);
    tft.printf("Stock: %d", containers[2].current_capacity);
    if (containers[2].low_stock) {
      tft.setTextColor(WARNING_COLOR);
      tft.setCursor(gridX1 + 8, gridY2 + 50);
      tft.print("LOW STOCK");
    }
  } else {
    //shows empty
    tft.drawRect(gridX1, gridY2, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX1 + 8, gridY2 + 8);
    tft.printf("Container 3");
    tft.setCursor(gridX1 + 8, gridY2 + 25);
    // tft.printf("Empty");
  }
  
  // Container 4 (Bottom-Right)
  if (containerCount > 3) {
    tft.drawRect(gridX2, gridY2, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX2 + 8, gridY2 + 8);
    tft.printf("Container 4");
    tft.setCursor(gridX2 + 8, gridY2 + 25);
    tft.printf("%s", containers[3].medicine_name.c_str());
    tft.setCursor(gridX2 + 8, gridY2 + 40);
    tft.printf("Stock: %d", containers[3].current_capacity);
    if (containers[3].low_stock) {
      tft.setTextColor(WARNING_COLOR);
      tft.setCursor(gridX2 + 8, gridY2 + 70);
      tft.print("LOW STOCK");
    }
  } else {
    //shows empty
    tft.drawRect(gridX2, gridY2, gridW, gridH, HIGHLIGHT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(gridX2 + 8, gridY2 + 8);
    tft.printf("Container 4");
    tft.setCursor(gridX2 + 8, gridY2 + 25);
    // tft.printf("Empty");
  }

  // TODO: add control queue data from main esp
  // List Control Queue
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 330);
  tft.print("Control Queue:");

  // List first 2 control queue
  // Control Queue display (similar to reminders style)
  int queueCount_shown = 0;
  int queueYPos = 350;
  for (int i = 0; i < 2; i++) {  // Show first 2 queue items
    // Draw square box with blue background
    uint16_t blueColor = 0x041F; // Blue color
    tft.fillRect(10, queueYPos, tft.width() - 20, 35, blueColor);
    tft.drawRect(10, queueYPos, tft.width() - 20, 35, TEXT_COLOR);
    
    // Draw text
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(20, queueYPos + 5);
    tft.printf("Queue #%d", i + 1);
    
    tft.setCursor(20, queueYPos + 17);
    tft.printf("Container %d: Paracetamol x10", i + 1);
    
    queueYPos += 40;
    queueCount_shown++;
  }
  
  // Draw button to see more queues
  if (queueCount_shown > 0) {
    drawButton(10, queueYPos + 5, tft.width() - 20, buttonHeight, "View All Queues", HIGHLIGHT_COLOR);
  }
}

void drawContainersScreen() {
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Medicine Containers");
  
  // Back button
  drawButton(buttonMargin, buttonMargin, 60, 30, "Back", HIGHLIGHT_COLOR);
  
  // Container list
  int yPos = 50;
  for (int i = 0; i < containerCount && yPos < tft.height() - 50; i++) {
    drawContainerItem(10, yPos, containers[i]);
    yPos += 45;
  }
  
  if (containerCount == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.print("No containers configured");
  }
}

void drawRemindersScreen() {
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(80, 10);
  tft.print("Medicine Reminders");
  
  // Back button
  drawButton(buttonMargin, buttonMargin, 60, 30, "Back", HIGHLIGHT_COLOR);
  
  // Reminder list
  int yPos = 50;
  for (int i = 0; i < reminderCount && yPos < tft.height() - 50; i++) {
    if (reminders[i].active) {
      drawReminderItem(10, yPos, reminders[i]);
      yPos += 40;
    }
  }
  
  if (getActiveReminderCount() == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.print("No active reminders");
  }
}

void drawScheduleScreen() {
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Daily Schedule");
  
  // Back button
  drawButton(buttonMargin, buttonMargin, 60, 30, "Back", HIGHLIGHT_COLOR);
  
  // Schedule list
  int yPos = 50;
  for (int i = 0; i < scheduleCount && yPos < tft.height() - 50; i++) {
    drawScheduleItem(10, yPos, dailySchedule[i]);
    yPos += 35;
  }
  
  if (scheduleCount == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.print("No schedule available");
  }
}

void drawAlarmScreen() {
  tft.fillScreen(ALARM_COLOR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  
  // Alarm title
  tft.setCursor(tft.width() / 2 - 60, 40);
  tft.print("ALERT!");
  
  tft.setTextSize(2);
  tft.setCursor(tft.width() / 2 - 100, 80);
  tft.print("Time for Medicine");
  
  // Medicine info
  tft.setTextSize(2);
  tft.setCursor(tft.width() / 2 - 80, 120);
  if (!alarmMessage.isEmpty()) {
    tft.print(alarmMessage);
  } else {
    tft.print("Check your medication");
  }
  
  // Dismiss button
  drawButton(tft.width() / 2 - 50, tft.height() - 80, 100, 40, "Dismiss", TFT_WHITE);
}

void drawDispensingScreen() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  
  if (isDispensing && !dispensingComplete) {
    tft.setCursor(tft.width() / 2 - 100, 60);
    tft.print("Dispensing...");
    
    tft.setCursor(tft.width() / 2 - 80, 100);
    tft.printf("Container: %d", dispensingContainer);
    
    tft.setCursor(tft.width() / 2 - 60, 130);
    tft.printf("Dosage: %d", dispensingDosage);
    
    // Loading animation
    static unsigned long lastAnim = 0;
    static int animState = 0;
    if (millis() - lastAnim > 500) {
      lastAnim = millis();
      animState = (animState + 1) % 4;
      
      tft.fillRect(tft.width() / 2 - 30, 160, 60, 10, BACKGROUND_COLOR);
      for (int i = 0; i <= animState; i++) {
        tft.fillRect(tft.width() / 2 - 30 + i * 15, 160, 10, 10, HIGHLIGHT_COLOR);
      }
    }
  } else if (dispensingComplete) {
    tft.setCursor(tft.width() / 2 - 80, 60);
    tft.print("Complete!");
    
    tft.setCursor(tft.width() / 2 - 100, 100);
    tft.printf("Dispensed %d pills", dispensingDosage);
    
    tft.setCursor(tft.width() / 2 - 80, 130);
    tft.printf("from Container %d", dispensingContainer);
    
    drawButton(tft.width() / 2 - 40, tft.height() - 60, 80, 30, "OK", SUCCESS_COLOR);
  }
}

void drawContainerItem(int x, int y, Container container) {
  // Container box
  tft.drawRect(x, y, tft.width() - 20, 40, HIGHLIGHT_COLOR);
  tft.fillRect(x + 1, y + 1, (tft.width() - 22) * container.current_capacity / container.max_capacity, 38, 
               container.low_stock ? WARNING_COLOR : SUCCESS_COLOR);
  
  // Text
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  tft.setCursor(x + 5, y + 5);
  tft.print(container.medicine_name);
  
  tft.setCursor(x + 5, y + 20);
  tft.printf("Stock: %d/%d", container.current_capacity, container.max_capacity);
  
  if (container.low_stock) {
    tft.setTextColor(WARNING_COLOR);
    tft.setCursor(x + 120, y + 20);
    tft.print("LOW STOCK");
  }
}

void drawReminderItem(int x, int y, Reminder reminder) {
  // Draw rectangle box with pink background
  uint16_t pinkColor = 0xF81F; // Pink color
  tft.fillRect(x, y, tft.width() - 20, 35, pinkColor);
  tft.drawRect(x, y, tft.width() - 20, 35, TEXT_COLOR);
  
  // Draw text
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  
  tft.setCursor(x + 8, y + 5);
  tft.print(reminder.medicine_name);
  
  // Build time string from all times
  String timeStr = "";
  for (int i = 0; i < reminder.timeCount; i++) {
    if (i > 0) timeStr += ", ";
    timeStr += reminder.times[i];
  }
  
  tft.setCursor(x + 8, y + 17);
  tft.printf("Container: %d | %s", reminder.container_id, timeStr.c_str());
}

void drawScheduleItem(int x, int y, DailySchedule schedule) {
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  
  tft.setCursor(x, y);
  tft.printf("%s - %s", schedule.time.c_str(), schedule.medicine_name.c_str());
  
  tft.setCursor(x, y + 15);
  tft.printf("Dosage: %d | Status: %s", schedule.dosage, schedule.status.c_str());
  
  // Status indicator
  uint16_t statusColor = (schedule.status == "completed") ? SUCCESS_COLOR : 
                         (schedule.status == "pending") ? WARNING_COLOR : TEXT_COLOR;
  tft.fillCircle(x + tft.width() - 30, y + 8, 4, statusColor);
}

void drawButton(int x, int y, int w, int h, String label, uint16_t color) {
  tft.drawRect(x, y, w, h, color);
  tft.setTextColor(color);
  tft.setTextSize(1);
  
  int textX = x + (w - label.length() * 6) / 2;
  int textY = y + (h - 8) / 2;
  
  tft.setCursor(textX, textY);
  tft.print(label);
}

void showStatusMessage(String message) {
  // Show temporary status message
  tft.fillRect(0, tft.height() - 20, tft.width(), 20, BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  tft.setCursor(10, tft.height() - 15);
  tft.print(message);
}

void showErrorMessage(String errorMsg) {
  tft.fillRect(0, tft.height() - 20, tft.width(), 20, ALARM_COLOR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, tft.height() - 15);
  tft.print("Error: " + errorMsg);
}

void showReminderAlert(String medicineName, int containerId, int dosage, String alertType, String message, String timeStr) {
  alarmActive = true;
  alarmType = alertType;
  alarmMessage = message;
  currentState = STATE_ALARM;
}

void showControlQueueResult(int queueId, bool success, String message) {
  String resultMsg = String("Queue #") + queueId + ": " + (success ? "Success" : "Failed");
  if (success) {
    showStatusMessage(resultMsg);
  } else {
    showErrorMessage(resultMsg);
  }
}

int getActiveContainerCount() {
  int count = 0;
  for (int i = 0; i < containerCount; i++) {
    if (containers[i].current_capacity > 0) {
      count++;
    }
  }
  return count;
}

int getActiveReminderCount() {
  int count = 0;
  for (int i = 0; i < reminderCount; i++) {
    if (reminders[i].active) {
      count++;
    }
  }
  return count;
}

void syncTimeWithNTP() {
    // Note: Caller should already hold wifiMutex or ensure thread safety
    configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
    Serial.println("NTP time sync initiated");
    
    // Wait for time sync
    int retries = 0;
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        // vTaskDelay(pdMS_TO_TICKS(500));
        delay(500);
        retries++;
    }
    
    if (getLocalTime(&timeinfo)) {
        rtcTimeSet = true;
        // xEventGroupSetBits(wifiEventGroup, TIME_SYNCED_BIT);
        Serial.println("✓ Time synchronized with NTP");
        Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900,
                      timeinfo.tm_mon + 1,
                      timeinfo.tm_mday,
                      timeinfo.tm_hour,
                      timeinfo.tm_min,
                      timeinfo.tm_sec);
    } else {
        Serial.println("⚠ NTP sync failed");
    }
}

// ====================================
// DUMMY DATA GENERATION FUNCTIONS
// ====================================

void generateDummyData() {
    Serial.println("\n>>> Sending all dummy data <<<");
    delay(500);
    sendDummyDeviceInfo();
    delay(500);
    sendDummySystemStatus();
    delay(500);
    sendDummySensorData();
    delay(500);
    sendDummyContainersInfo();
    delay(500);
    sendDummyRemindersInfo();
    delay(500);
    sendDummyDailySchedule();
    delay(500);
    Serial.println(">>> All dummy data sent <<<\n");
}

void sendDummyDeviceInfo() {
    StaticJsonDocument<512> doc;
    doc["type"] = "device_info";
    doc["id"] = 1;
    doc["uid"] = "90c666bf-c1ea-4ce5-940d-6a4b94bc9540";
    doc["device_name"] = "Minder Device";
    doc["current_state"] = "online";
    doc["temperature"] = 26.7;
    doc["humidity"] = 57.0;
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: device_info");
}

void sendDummySystemStatus() {
    StaticJsonDocument<512> doc;
    doc["type"] = "system_status";
    doc["wifi_status"] = "connected";
    doc["mqtt_status"] = "connected";
    doc["sd_card_status"] = "mounted";
    doc["temperature"] = 26.7;
    doc["humidity"] = 57.0;
    doc["rtc_time_set"] = true;
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: system_status");
}

void sendDummySensorData() {
    StaticJsonDocument<256> doc;
    doc["type"] = "sensor_data";
    doc["temperature"] = 26.7 + random(-10, 10) / 10.0;
    doc["humidity"] = 57.0 + random(-20, 20) / 10.0;
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: sensor_data");
}

void sendDummyContainersInfo() {
    StaticJsonDocument<1024> doc;
    doc["type"] = "containers_info";
    doc["timestamp"] = millis();
    
    JsonArray containersArray = doc.createNestedArray("containers");
    
    // Container 1
    JsonObject container1 = containersArray.createNestedObject();
    container1["id"] = 1;
    container1["container_id"] = 1;
    container1["container_number"] = 1;
    container1["medicine_name"] = "Paracetamol";
    container1["quantity"] = 50;
    container1["low_stock"] = false;
    
    // Container 2
    JsonObject container2 = containersArray.createNestedObject();
    container2["id"] = 2;
    container2["container_id"] = 2;
    container2["container_number"] = 2;
    container2["medicine_name"] = "Aspirin";
    container2["quantity"] = 30;
    container2["low_stock"] = false;
    
    // Container 3
    JsonObject container3 = containersArray.createNestedObject();
    container3["id"] = 3;
    container3["container_id"] = 3;
    container3["container_number"] = 3;
    container3["medicine_name"] = "Ibuprofen";
    container3["quantity"] = 5;
    container3["low_stock"] = true;
    
    // Container 4
    JsonObject container4 = containersArray.createNestedObject();
    container4["id"] = 4;
    container4["container_id"] = 4;
    container4["container_number"] = 4;
    container4["medicine_name"] = "Amoxicillin";
    container4["quantity"] = 20;
    container4["low_stock"] = false;
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: containers_info");
}

void sendDummyRemindersInfo() {
    StaticJsonDocument<2048> doc;
    doc["type"] = "reminders_info";
    doc["timestamp"] = millis();
    
    JsonArray remindersArray = doc.createNestedArray("reminders");
    
    // Reminder 1
    JsonObject reminder1 = remindersArray.createNestedObject();
    reminder1["id"] = 8;
    reminder1["medicine_name"] = "Paracetamol";
    reminder1["container_id"] = 1;
    reminder1["container_number"] = 1;
    reminder1["active"] = true;
    reminder1["schedule_type"] = "Once Daily";
    reminder1["notes"] = "Take with water";
    JsonArray times1 = reminder1.createNestedArray("times");
    JsonObject time1 = times1.createNestedObject();
    time1["time"] = "08:00";
    time1["dosage"] = 1;
    
    // Reminder 2
    JsonObject reminder2 = remindersArray.createNestedObject();
    reminder2["id"] = 9;
    reminder2["medicine_name"] = "Aspirin";
    reminder2["container_id"] = 2;
    reminder2["container_number"] = 2;
    reminder2["active"] = true;
    reminder2["schedule_type"] = "Twice Daily";
    reminder2["notes"] = "After meals";
    JsonArray times2 = reminder2.createNestedArray("times");
    JsonObject time2a = times2.createNestedObject();
    time2a["time"] = "08:00";
    time2a["dosage"] = 1;
    JsonObject time2b = times2.createNestedObject();
    time2b["time"] = "14:00";
    time2b["dosage"] = 1;
    
    // Reminder 3
    JsonObject reminder3 = remindersArray.createNestedObject();
    reminder3["id"] = 10;
    reminder3["medicine_name"] = "Ibuprofen";
    reminder3["container_id"] = 3;
    reminder3["container_number"] = 3;
    reminder3["active"] = false;
    reminder3["schedule_type"] = "As needed";
    reminder3["notes"] = "Only if fever > 38C";
    JsonArray times3 = reminder3.createNestedArray("times");
    JsonObject time3 = times3.createNestedObject();
    time3["time"] = "12:00";
    time3["dosage"] = 1;
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: reminders_info");
}

void sendDummyDailySchedule() {
    StaticJsonDocument<2048> doc;
    doc["type"] = "daily_schedule";
    doc["current_time"] = "17:25";
    doc["timestamp"] = millis();
    
    JsonArray scheduleArray = doc.createNestedArray("schedule");
    
    // Schedule 1 - Paracetamol at 08:00
    JsonObject schedule1 = scheduleArray.createNestedObject();
    schedule1["medicine_name"] = "Paracetamol";
    schedule1["container_id"] = 1;
    schedule1["container_number"] = 1;
    schedule1["time"] = "08:00";
    schedule1["dosage"] = 1;
    schedule1["schedule_type"] = "Once Daily";
    schedule1["notes"] = "After breakfast";
    schedule1["reminder_id"] = 8;
    schedule1["status"] = "pending";
    
    // Schedule 2 - Aspirin at 08:00
    JsonObject schedule2 = scheduleArray.createNestedObject();
    schedule2["medicine_name"] = "Aspirin";
    schedule2["container_id"] = 2;
    schedule2["container_number"] = 2;
    schedule2["time"] = "08:00";
    schedule2["dosage"] = 1;
    schedule2["schedule_type"] = "Twice Daily";
    schedule2["notes"] = "After breakfast";
    schedule2["reminder_id"] = 9;
    schedule2["status"] = "completed";
    
    // Schedule 3 - Aspirin at 14:00
    JsonObject schedule3 = scheduleArray.createNestedObject();
    schedule3["medicine_name"] = "Aspirin";
    schedule3["container_id"] = 2;
    schedule3["container_number"] = 2;
    schedule3["time"] = "14:00";
    schedule3["dosage"] = 1;
    schedule3["schedule_type"] = "Twice Daily";
    schedule3["notes"] = "After lunch";
    schedule3["reminder_id"] = 9;
    schedule3["status"] = "pending";
    
    // Schedule 4 - Paracetamol at 20:00
    JsonObject schedule4 = scheduleArray.createNestedObject();
    schedule4["medicine_name"] = "Paracetamol";
    schedule4["container_id"] = 1;
    schedule4["container_number"] = 1;
    schedule4["time"] = "20:00";
    schedule4["dosage"] = 1;
    schedule4["schedule_type"] = "Once Daily";
    schedule4["notes"] = "Before sleep";
    schedule4["reminder_id"] = 8;
    schedule4["status"] = "pending";
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: daily_schedule");
}

void sendDummyReminderAlert() {
    StaticJsonDocument<512> doc;
    doc["type"] = "reminder_alert";
    doc["medicine_name"] = "Paracetamol";
    doc["container_number"] = 1;
    doc["container_id"] = 1;
    doc["dosage"] = 1;
    doc["schedule_type"] = "Once Daily";
    doc["notes"] = "Take with water";
    doc["reminder_time"] = "17:25";
    doc["timestamp"] = millis();
    doc["alert_count"] = 1;
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: reminder_alert");
}

void sendDummyGroupedReminderAlert() {
    StaticJsonDocument<2048> doc;
    doc["type"] = "grouped_reminder_alert";
    doc["timestamp"] = millis();
    doc["alert_count"] = 2;
    doc["reminder_time"] = "14:00";
    
    JsonArray alertsArray = doc.createNestedArray("alerts");
    
    // Alert 1
    JsonObject alert1 = alertsArray.createNestedObject();
    alert1["medicine_name"] = "Paracetamol";
    alert1["container_id"] = 1;
    alert1["container_number"] = 1;
    alert1["dosage"] = 1;
    alert1["schedule_type"] = "Once Daily";
    alert1["notes"] = "After meal";
    alert1["reminder_id"] = 8;
    
    // Alert 2
    JsonObject alert2 = alertsArray.createNestedObject();
    alert2["medicine_name"] = "Aspirin";
    alert2["container_id"] = 2;
    alert2["container_number"] = 2;
    alert2["dosage"] = 1;
    alert2["schedule_type"] = "Twice Daily";
    alert2["notes"] = "With food";
    alert2["reminder_id"] = 9;
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: grouped_reminder_alert");
}

void sendDummyAlarmStatus(bool active) {
    StaticJsonDocument<256> doc;
    doc["type"] = "alarm_status";
    doc["alarm_active"] = active;
    doc["alarm_type"] = active ? "daily_log" : "";
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.printf("Sent dummy: alarm_status (active=%s)\n", active ? "true" : "false");
}

void sendDummyDispensingStatus(const char* status) {
    StaticJsonDocument<512> doc;
    doc["type"] = "dispensing_status";
    doc["container_number"] = 1;
    doc["container_id"] = 1;
    doc["dosage"] = 2;
    doc["medicine_name"] = "Paracetamol";
    doc["status"] = status;  // "started" or "completed"
    doc["pills_remaining"] = (strcmp(status, "started") == 0) ? 28 : 28;
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.printf("Sent dummy: dispensing_status (status=%s)\n", status);
}

void sendDummyStockAlert() {
    StaticJsonDocument<512> doc;
    doc["type"] = "stock_alert";
    doc["medicine_name"] = "Ibuprofen";
    doc["container_number"] = 3;
    doc["container_id"] = 3;
    doc["current_stock"] = 5;
    doc["minimum_stock"] = 10;
    doc["alert_level"] = "low";
    doc["recommendation"] = "Please refill soon";
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    processIncomingData(json);
    Serial.println("Sent dummy: stock_alert");
}