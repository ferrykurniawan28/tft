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
  STATE_ON_REMINDERS,
  STATE_TAKE_MEDICINE,
  STATE_DISPENSING,
  STATE_QUANTITY_CONFIRMATION,
  STATE_JAM_ALERT,
  STATE_WIFI_ERROR,
  STATE_CONTROL_QUEUE_LIST,
  STATE_CONTROL_QUEUE_CONFIRMATION
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
  int dosage;
};

// Reminder item for confirmation
struct ReminderItem {
  int id;
  String medicine_name;
  int container_id;
  int dosage;
};

// Control action for confirmation
struct ControlAction {
  int control_id;
  String action;
  String medicine_name;
  int container_id;
  int quantity;
  String message;
};

// Pending confirmation state
struct PendingConfirmation {
  ReminderItem reminders[10];
  int reminder_count;
  ControlAction control;
  int type; // 0=medication, 1=device_control
  int timeout_seconds;
  unsigned long sent_at;
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
String dispensingMedicineName = "";

// Sensor data
float currentTemperature = 0.0;
float currentHumidity = 0.0;

// Confirmation state
PendingConfirmation pendingConfirmation;
bool hasPendingConfirmation = false;
unsigned long confirmationStartTime = 0;
const unsigned long CONFIRMATION_TIMEOUT = 60000; // 60 seconds

// Jam alert state
int jamAlertContainer = 0;
String jamAlertMedicine = "";
int jamAlertPillsRemaining = 0;

// WiFi error state
String wifiErrorMessage = "";
String wifiErrorInstruction = "";

// AP Mode state
bool isInAPMode = false;
String apModeMessage = "";

// Clock display
String currentTimeString = "--:--";

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
void drawTakeMedicineConfirmation();
void drawQuantityConfirmation();
void drawJamAlert();
void drawWiFiError();
void drawControlConfirmation();
void handleTakeMedicineTouch(int x, int y);
void handleQuantityConfirmationTouch(int x, int y);
void handleJamAlertTouch(int x, int y);
void handleWiFiErrorTouch(int x, int y);
void handleControlQueueConfirmationTouch(int x, int y);


void setup() {
  Serial.begin(115200);
  SerialPort.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17

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

  // go to control queue confirmation for testing
  // currentState = STATE_CONTROL_QUEUE_CONFIRMATION;

  // go to reminder alert for testing
  // showReminderAlert("Paracetamol", 1, 2, "reminder", "Time to take your medicine", "08:00");
  // currentState = STATE_TAKE_MEDICINE;
}

void loop() {
  // Frame protocol receiver
  static enum { SYNC1, SYNC2, LENGTH_HIGH, LENGTH_LOW, DATA, CHECKSUM, END } rxState = SYNC1;
  static uint16_t rxLength = 0;
  static uint16_t rxCount = 0;
  static uint8_t rxChecksum = 0;
  static char rxBuffer[1024];
  
  while (SerialPort.available()) {
    uint8_t b = SerialPort.read();
    
    switch (rxState) {
      case SYNC1:
        if (b == 0x7E) {
          rxState = SYNC2;
        }
        break;
        
      case SYNC2:
        if (b == 0x7E) {
          rxState = LENGTH_HIGH;
        } else {
          rxState = SYNC1;
        }
        break;
        
      case LENGTH_HIGH:
        rxLength = b << 8;
        rxState = LENGTH_LOW;
        break;
        
      case LENGTH_LOW:
        rxLength |= b;
        if (rxLength > 0 && rxLength < sizeof(rxBuffer)) {
          rxCount = 0;
          rxChecksum = 0;
          rxState = DATA;
        } else {
          rxState = SYNC1;
        }
        break;
        
      case DATA:
        rxBuffer[rxCount++] = b;
        rxChecksum ^= b;
        if (rxCount >= rxLength) {
          rxState = CHECKSUM;
        }
        break;
        
      case CHECKSUM:
        if (b == rxChecksum) {
          rxState = END;
        } else {
          Serial.println("Checksum error");
          rxState = SYNC1;
        }
        break;
        
      case END:
        if (b == 0x00) {
          rxBuffer[rxCount] = '\0';
          processIncomingData(String(rxBuffer));
        }
        rxState = SYNC1;
        break;
    }
  }
  
  // Handle touch input
  handleTouchInput();
  
  // Check confirmation timeout (60 seconds)
  if (hasPendingConfirmation) {
    unsigned long elapsed = (millis() - confirmationStartTime) / 1000;
    if (elapsed >= pendingConfirmation.timeout_seconds) {
      // Timeout - auto cancel confirmation
      hasPendingConfirmation = false;
      currentState = STATE_HOME;
      
      // Send timeout response to minder
      StaticJsonDocument<256> doc;
      doc["type"] = "confirmation_response";
      doc["confirmed"] = false;
      doc["timeout"] = true;
      doc["confirmation_type"] = pendingConfirmation.type;
      
      String jsonStr;
      serializeJson(doc, jsonStr);
      SerialPort.println(jsonStr);
      
      Serial.println("Confirmation timeout - auto cancelled");
    }
  }
  
  // Update display based on current state
  updateDisplay();
  
  // Debug: Send dummy data every 30 seconds for testing
  static unsigned long lastDummyTime = 0;
  if (millis() - lastDummyTime > 30000) {
    lastDummyTime = millis();
    // Uncomment one of the dummy data functions below for testing
    // generateDummyData(); // Sends all dummy data
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

    // Handle AP Mode detection
    if (message == "AP Mode Active") {
      isInAPMode = true;
      apModeMessage = "WiFi Setup Mode\nConnect to: MinderAP\nConfigure WiFi settings";
      currentState = STATE_HOME;  // Ensure we're on home screen to show the message
      tft.fillScreen(BACKGROUND_COLOR);
      drawHomeScreen();
    }
    
    
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
    
    // Force redraw current screen with updated data
    tft.fillScreen(BACKGROUND_COLOR);
    switch (currentState) {
      case STATE_HOME: drawHomeScreen(); break;
      case STATE_CONTAINERS: drawContainersScreen(); break;
      case STATE_REMINDERS: drawRemindersScreen(); break;
      case STATE_SCHEDULE: drawScheduleScreen(); break;
      default: updateDisplay(); break;
    }
    
  } else if (type == "containers_info") {
    // Container data update
    if (doc["containers"].is<JsonArray>()) {
      syncContainers(doc["containers"]);
      // Redraw if we're on containers screen
      if (currentState == STATE_CONTAINERS) {
        tft.fillScreen(BACKGROUND_COLOR);
        drawContainersScreen();
      }
    }
    
  } else if (type == "reminders_info") {
    // Reminder data update
    if (doc["reminders"].is<JsonArray>()) {
      syncReminders(doc["reminders"]);
      // Redraw if we're on reminders screen
      if (currentState == STATE_REMINDERS) {
        tft.fillScreen(BACKGROUND_COLOR);
        drawRemindersScreen();
      }
    }
    
  } else if (type == "daily_schedule") {
    // Daily schedule update
    if (doc["schedule"].is<JsonArray>()) {
      syncDailySchedule(doc["schedule"]);
      // Redraw if we're on schedule screen
      if (currentState == STATE_SCHEDULE) {
        tft.fillScreen(BACKGROUND_COLOR);
        drawScheduleScreen();
      }
    }
    
  } else if (type == "sensor_data") {
    // Sensor data update
    currentTemperature = doc["temperature"] | 0.0;
    currentHumidity = doc["humidity"] | 0.0;
    // Redraw home screen to show updated sensor data
    if (currentState == STATE_HOME) {
      tft.fillScreen(BACKGROUND_COLOR);
      drawHomeScreen();
    }
    
  } else if (type == "system_status") {
    // System status update
    String wifiStatus = doc["wifi_status"] | "disconnected";
    String mqttStatus = doc["mqtt_status"] | "disconnected";
    bool apMode = doc["ap_mode"] | false;
    wifiConnected = (wifiStatus == "connected");
    mqttConnected = (mqttStatus == "connected");
    timeSynced = doc["rtc_time_set"] | false;
    currentTemperature = doc["temperature"] | 0.0;
    currentHumidity = doc["humidity"] | 0.0;
    isInAPMode = apMode;
    
    // Force redraw current screen to show updated status
    tft.fillScreen(BACKGROUND_COLOR);
    switch (currentState) {
      case STATE_HOME: drawHomeScreen(); break;
      case STATE_CONTAINERS: drawContainersScreen(); break;
      case STATE_REMINDERS: drawRemindersScreen(); break;
      case STATE_SCHEDULE: drawScheduleScreen(); break;
      default: updateDisplay(); break;
    }
    
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
    
  } else if (type == "confirmation_request") {
    // Confirmation request from minder
    hasPendingConfirmation = true;
    confirmationStartTime = millis();
    
    // Read request_type (sent by minder) and convert to type number
    String requestType = doc["request_type"] | "medication";
    pendingConfirmation.type = (requestType == "device_control") ? 1 : 0; // 0=medication, 1=device_control
    pendingConfirmation.timeout_seconds = doc["timeout_seconds"] | 60;
    pendingConfirmation.sent_at = millis();
    
    if (pendingConfirmation.type == 0) {
      // Medication confirmation
      JsonArray remindersArray = doc["reminders"];
      pendingConfirmation.reminder_count = 0;
      
      for (JsonObject reminderObj : remindersArray) {
        if (pendingConfirmation.reminder_count < 10) {
          ReminderItem& item = pendingConfirmation.reminders[pendingConfirmation.reminder_count];
          item.id = reminderObj["id"] | 0;
          item.medicine_name = reminderObj["medicine_name"] | "";
          item.container_id = reminderObj["container_id"] | 0;
          item.dosage = reminderObj["dosage"] | 1;
          pendingConfirmation.reminder_count++;
        }
      }
      
      currentState = STATE_TAKE_MEDICINE;
      
    } else if (pendingConfirmation.type == 1) {
      // Device control confirmation - read from root of JSON
      pendingConfirmation.control.control_id = doc["control_id"] | 0;
      pendingConfirmation.control.action = doc["action"] | "";
      pendingConfirmation.control.medicine_name = doc["medicine_name"] | "";
      pendingConfirmation.control.container_id = doc["container_id"] | 0;
      pendingConfirmation.control.quantity = doc["quantity"] | 0;
      pendingConfirmation.control.message = doc["message"] | "";
      
      currentState = STATE_CONTROL_QUEUE_CONFIRMATION;
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
    // Dispensing status update
    String dispStatus = doc["status"] | "";
    dispensingMedicineName = doc["medicine_name"] | "";
    dispensingContainer = doc["container_number"] | 0;
    dispensingDosage = doc["dosage"] | 0;
    
    if (dispStatus == "started" || dispStatus == "in_progress") {
      isDispensing = true;
      dispensingComplete = false;
      if (currentState != STATE_DISPENSING) {
        currentState = STATE_DISPENSING;
      }
    } else if (dispStatus == "completed") {
      dispensingComplete = true;
    }
    
  } else if (type == "all_dispensing_completed") {
    // All medicines dispensed, show quantity confirmation
    isDispensing = false;
    currentState = STATE_QUANTITY_CONFIRMATION;
    
  } else if (type == "stock_alert") {
    // Stock alert
    String medicineName = doc["medicine_name"] | "";
    int current = doc["current_stock"] | 0;
    int minimum = doc["minimum_stock"] | 0;
    Serial.printf("Stock Alert: %s - Current: %d, Minimum: %d\n", medicineName.c_str(), current, minimum);
    
  } else if (type == "jam_alert") {
    // Jam alert
    jamAlertContainer = doc["container_number"] | 0;
    jamAlertMedicine = doc["medicine_name"] | "";
    jamAlertPillsRemaining = doc["pills_remaining"] | 0;
    currentState = STATE_JAM_ALERT;
    
  } else if (type == "wifi_error_alert") {
    // WiFi error alert
    wifiErrorMessage = doc["message"] | "";
    wifiErrorInstruction = doc["instruction"] | "";
    currentState = STATE_WIFI_ERROR;
    
  } else if (type == "current_time") {
    // Time update from minder
    currentTimeString = doc["time"] | "00:00";
    // Only redraw if on home screen
    if (currentState == STATE_HOME) {
      drawHomeScreen();
    }
    
  } else if (type == "system_status") {
    // System status update
    String wifiStatus = doc["wifi_status"] | "disconnected";
    String mqttStatus = doc["mqtt_status"] | "disconnected";
    wifiConnected = (wifiStatus == "connected");
    mqttConnected = (mqttStatus == "connected");
    timeSynced = doc["rtc_time_set"] | false;
    currentTemperature = doc["temperature"] | 0.0;
    currentHumidity = doc["humidity"] | 0.0;
    
    // Check AP mode status
    isInAPMode = doc["ap_mode"] | false;
    if (isInAPMode) {
      apModeMessage = "WiFi Setup Mode\nConnect to: MinderAP\nConfigure WiFi settings";
    }

    updateDisplay();
    
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
      case STATE_TAKE_MEDICINE:
        handleTakeMedicineTouch(x, y);
        break;
      case STATE_DISPENSING:
        handleDispensingTouch(x, y);
        break;
      case STATE_QUANTITY_CONFIRMATION:
        handleQuantityConfirmationTouch(x, y);
        break;
      case STATE_JAM_ALERT:
        handleJamAlertTouch(x, y);
        break;
      case STATE_WIFI_ERROR:
        handleWiFiErrorTouch(x, y);
        break;
      case STATE_CONTROL_QUEUE_CONFIRMATION:
        handleControlQueueConfirmationTouch(x, y);
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

void handleTakeMedicineTouch(int x, int y) {
  // Confirm button (left side, 145x60)
  int confirmX = 10;
  int confirmY = tft.height() - 70;
  if (x >= confirmX && x <= confirmX + 145 && y >= confirmY && y <= confirmY + 60) {
    // Send confirmation response
    StaticJsonDocument<512> doc;
    doc["type"] = "confirmation_response";
    doc["confirmed"] = true;
    doc["confirmation_type"] = pendingConfirmation.type;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    hasPendingConfirmation = false;
    currentState = STATE_DISPENSING;
    return;
  }
  
  // Cancel button (right side, 145x60)
  int cancelX = 165;
  int cancelY = tft.height() - 70;
  if (x >= cancelX && x <= cancelX + 145 && y >= cancelY && y <= cancelY + 60) {
    // Send cancel response
    StaticJsonDocument<512> doc;
    doc["type"] = "confirmation_response";
    doc["confirmed"] = false;
    doc["confirmation_type"] = pendingConfirmation.type;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    hasPendingConfirmation = false;
    currentState = STATE_HOME;
    return;
  }
}

void handleQuantityConfirmationTouch(int x, int y) {
  // Yes button
  int yesX = 20;
  int yesY = tft.height() - 60;
  if (x >= yesX && x <= yesX + 100 && y >= yesY && y <= yesY + 40) {
    // Send quantity confirmed
    StaticJsonDocument<256> doc;
    doc["type"] = "quantity_confirmed";
    doc["confirmed"] = true;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    currentState = STATE_HOME;
    return;
  }
  
  // One More button
  int oneMoreX = tft.width() - 120;
  int oneMoreY = tft.height() - 60;
  if (x >= oneMoreX && x <= oneMoreX + 100 && y >= oneMoreY && y <= oneMoreY + 40) {
    // Send one more request
    StaticJsonDocument<256> doc;
    doc["type"] = "quantity_confirmed";
    doc["confirmed"] = false;
    doc["one_more"] = true;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    currentState = STATE_DISPENSING;
    return;
  }
}

void handleJamAlertTouch(int x, int y) {
  // Continue button
  int continueX = tft.width() / 2 - 60;
  int continueY = tft.height() - 60;
  if (x >= continueX && x <= continueX + 120 && y >= continueY && y <= continueY + 40) {
    // Send jam cleared
    StaticJsonDocument<256> doc;
    doc["type"] = "jam_cleared";
    doc["container_number"] = jamAlertContainer;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    currentState = STATE_DISPENSING;
    return;
  }
}

void handleWiFiErrorTouch(int x, int y) {
  // OK button
  int okX = tft.width() / 2 - 40;
  int okY = tft.height() - 60;
  if (x >= okX && x <= okX + 80 && y >= okY && y <= okY + 40) {
    currentState = STATE_HOME;
    return;
  }
}

void handleControlQueueConfirmationTouch(int x, int y) {
  // Confirm button (left side, 145x60)
  int confirmX = 10;
  int confirmY = tft.height() - 70;
  if (x >= confirmX && x <= confirmX + 145 && y >= confirmY && y <= confirmY + 60) {
    // Send confirmation response
    StaticJsonDocument<512> doc;
    doc["type"] = "confirmation_response";
    doc["confirmed"] = true;
    doc["confirmation_type"] = 1; // device_control
    doc["control_id"] = pendingConfirmation.control.control_id;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    hasPendingConfirmation = false;
    currentState = STATE_HOME;
    return;
  }
  
  // Cancel button (right side, 145x60)
  int cancelX = 165;
  int cancelY = tft.height() - 70;
  if (x >= cancelX && x <= cancelX + 145 && y >= cancelY && y <= cancelY + 60) {
    // Send cancel response
    StaticJsonDocument<512> doc;
    doc["type"] = "confirmation_response";
    doc["confirmed"] = false;
    doc["confirmation_type"] = 1; // device_control
    doc["control_id"] = pendingConfirmation.control.control_id;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    SerialPort.println(jsonStr);
    
    hasPendingConfirmation = false;
    currentState = STATE_HOME;
    return;
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
    case STATE_ON_REMINDERS:
      // Not implemented yet
      break;
    case STATE_TAKE_MEDICINE:
      drawTakeMedicineConfirmation();
      break;
    case STATE_DISPENSING:
      drawDispensingScreen();
      break;
    case STATE_QUANTITY_CONFIRMATION:
      drawQuantityConfirmation();
      break;
    case STATE_JAM_ALERT:
      drawJamAlert();
      break;
    case STATE_WIFI_ERROR:
      drawWiFiError();
      break;
    case STATE_CONTROL_QUEUE_LIST:
      // Not implemented yet
      break;
    case STATE_CONTROL_QUEUE_CONFIRMATION:
      drawControlConfirmation();
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
  static String lastTimeString = "";
  static bool lastAPMode = false;
  static bool lastWifiConnected = false;
  static bool lastMqttConnected = false;
  static float lastTemp = -999;
  static float lastHum = -999;
  static DisplayState lastState = STATE_HOME;
  
  // Force redraw if we just switched to this state
  bool stateChanged = (lastState != currentState);
  if (stateChanged) {
    lastState = currentState;
    lastTimeString = "";
    lastTemp = -999;
    lastHum = -999;
  }
  
  // Only clear and redraw if something changed
  bool needsRedraw = stateChanged || 
                     (currentTimeString != lastTimeString) || 
                     (isInAPMode != lastAPMode) ||
                     (wifiConnected != lastWifiConnected) ||
                     (mqttConnected != lastMqttConnected) ||
                     (currentTemperature != lastTemp) ||
                     (currentHumidity != lastHum);
  
  if (needsRedraw) {
    // Clear specific areas that will be redrawn
    tft.fillRect(0, 0, tft.width(), 60, BACKGROUND_COLOR); // Clear clock area
    tft.fillRect(0, 60, 240, 60, BACKGROUND_COLOR); // Clear sensor data area
    tft.fillRect(240, 60, 80, 60, BACKGROUND_COLOR); // Clear connection status area
    
    lastTimeString = currentTimeString;
    lastAPMode = isInAPMode;
    lastWifiConnected = wifiConnected;
    lastMqttConnected = mqttConnected;
    lastTemp = currentTemperature;
    lastHum = currentHumidity;
  }
  
  // Clock display (large, centered at top)
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(5);
  int clockWidth = currentTimeString.length() * 30; // Approximate width for size 5
  tft.setCursor((tft.width() - clockWidth) / 2, 15);
  tft.print(currentTimeString);
  
  // Connection status (smaller, right side)
  tft.setTextSize(3);
  tft.setCursor(250, 60);
  tft.setTextColor(TEXT_COLOR);
  tft.print("W:");
  tft.setTextColor(wifiConnected ? SUCCESS_COLOR : WARNING_COLOR);
  tft.print(wifiConnected ? "C" : "D");
  
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(250, 100);
  tft.print("M:");
  tft.setTextColor(mqttConnected ? SUCCESS_COLOR : WARNING_COLOR);
  tft.print(mqttConnected ? "C" : "D");
  
  // Sensor data
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(3);
  tft.setCursor(10, 60);
  tft.printf("%.1fC", currentTemperature);
  tft.setCursor(10, 100);
  tft.printf("%.1f%%", currentHumidity);
  
  // AP Mode message (center of screen)
  if (isInAPMode) {
    int boxX = 20;
    int boxY = tft.height() / 2 - 60;
    int boxW = tft.width() - 40;
    int boxH = 120;
    
    // Orange box
    tft.drawRect(boxX, boxY, boxW, boxH, TFT_ORANGE);
    tft.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, TFT_ORANGE);
    
    // Title
    tft.setTextColor(TFT_ORANGE);
    tft.setTextSize(2);
    tft.setCursor(boxX + 20, boxY + 15);
    tft.print("WiFi Setup Mode");
    
    // Instructions
    tft.setTextSize(2);
    tft.setCursor(boxX + 20, boxY + 45);
    tft.print("Connect to:");
    tft.setCursor(boxX + 20, boxY + 70);
    tft.print("MinderAP");
    tft.setCursor(boxX + 20, boxY + 95);
    tft.setTextSize(1);
    tft.print("Configure WiFi settings");
  }
}

void drawContainersScreen() {
  static int lastContainerCount = -1;
  
  // Clear screen if container count changed
  if (containerCount != lastContainerCount) {
    tft.fillScreen(BACKGROUND_COLOR);
    lastContainerCount = containerCount;
  }
  
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print("Containers");
  
  // Back button
  drawButton(buttonMargin, buttonMargin, 60, 30, "Back", HIGHLIGHT_COLOR);
  
  // Container list
  int yPos = 60;
  for (int i = 0; i < containerCount && yPos < tft.height() - 50; i++) {
    drawContainerItem(10, yPos, containers[i]);
    yPos += 50;
  }
  
  if (containerCount == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.print("No containers");
  }
}

void drawRemindersScreen() {
  static int lastReminderCount = -1;
  
  // Clear screen if reminder count changed
  int activeCount = getActiveReminderCount();
  if (activeCount != lastReminderCount) {
    tft.fillScreen(BACKGROUND_COLOR);
    lastReminderCount = activeCount;
  }
  
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(3);
  tft.setCursor(50, 10);
  tft.print("Reminders");
  
  // Back button
  drawButton(buttonMargin, buttonMargin, 60, 30, "Back", HIGHLIGHT_COLOR);
  
  // Reminder list
  int yPos = 60;
  for (int i = 0; i < reminderCount && yPos < tft.height() - 50; i++) {
    if (reminders[i].active) {
      drawReminderItem(10, yPos, reminders[i]);
      yPos += 45;
    }
  }
  
  if (activeCount == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.print("No reminders");
  }
}

void drawScheduleScreen() {
  static int lastScheduleCount = -1;
  
  // Clear screen if schedule count changed
  if (scheduleCount != lastScheduleCount) {
    tft.fillScreen(BACKGROUND_COLOR);
    lastScheduleCount = scheduleCount;
  }
  
  // Header
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print("Schedule");
  
  // Back button
  drawButton(buttonMargin, buttonMargin, 60, 30, "Back", HIGHLIGHT_COLOR);
  
  // Schedule list
  int yPos = 60;
  for (int i = 0; i < scheduleCount && yPos < tft.height() - 50; i++) {
    drawScheduleItem(10, yPos, dailySchedule[i]);
    yPos += 40;
  }
  
  if (scheduleCount == 0) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(10, 80);
    tft.print("No schedule");
  }
}

void drawAlarmScreen() {
  tft.fillScreen(ALARM_COLOR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(4);
  
  // Alarm title
  tft.setCursor(tft.width() / 2 - 75, 40);
  tft.print("ALERT!");
  
  tft.setTextSize(3);
  tft.setCursor(20, 100);
  tft.print("Medicine");
  tft.setCursor(20, 130);
  tft.print("Time");
  
  // Medicine info
  tft.setTextSize(2);
  tft.setCursor(20, 170);
  if (!alarmMessage.isEmpty()) {
    tft.print(alarmMessage);
  } else {
    tft.print("Check medication");
  }
  
  // Dismiss button (larger)
  tft.fillRect(tft.width() / 2 - 60, tft.height() - 80, 120, 50, TFT_WHITE);
  tft.setTextColor(ALARM_COLOR);
  tft.setTextSize(2);
  tft.setCursor(tft.width() / 2 - 45, tft.height() - 65);
  tft.print("DISMISS");
}

void drawDispensingScreen() {
  static bool lastIsDispensing = false;
  static bool lastDispensingComplete = false;
  
  // Clear screen if dispensing state changed
  if (isDispensing != lastIsDispensing || dispensingComplete != lastDispensingComplete) {
    tft.fillScreen(BACKGROUND_COLOR);
    lastIsDispensing = isDispensing;
    lastDispensingComplete = dispensingComplete;
  }
  
  tft.setTextColor(TEXT_COLOR);
  
  if (isDispensing && !dispensingComplete) {
    tft.setTextSize(3);
    tft.setCursor(30, 60);
    tft.print("Dispensing");
    
    tft.setTextSize(2);
    tft.setCursor(30, 110);
    tft.print(dispensingMedicineName);
    
    tft.setCursor(30, 140);
    tft.printf("Container: %d", dispensingContainer);
    
    tft.setCursor(30, 170);
    tft.printf("Pills: %d", dispensingDosage);
    
    // Loading animation
    static unsigned long lastAnim = 0;
    static int animState = 0;
    if (millis() - lastAnim > 500) {
      lastAnim = millis();
      animState = (animState + 1) % 4;
      
      tft.fillRect(30, 220, 120, 15, BACKGROUND_COLOR);
      for (int i = 0; i <= animState; i++) {
        tft.fillRect(30 + i * 30, 220, 20, 15, HIGHLIGHT_COLOR);
      }
    }
  } else if (dispensingComplete) {
    tft.setTextSize(3);
    tft.setCursor(50, 60);
    tft.print("Complete!");
    
    tft.setTextSize(2);
    tft.setCursor(30, 120);
    tft.printf("Dispensed:");
    
    tft.setCursor(30, 150);
    tft.print(dispensingMedicineName);
    
    tft.setCursor(30, 180);
    tft.printf("%d pills", dispensingDosage);
    
    tft.setCursor(30, 210);
    tft.printf("Container: %d", dispensingContainer);
    
    tft.fillRect(tft.width() / 2 - 50, tft.height() - 70, 100, 50, SUCCESS_COLOR);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(tft.width() / 2 - 20, tft.height() - 55);
    tft.print("OK");
  }
}

void drawContainerItem(int x, int y, Container container) {
  // Container box
  tft.drawRect(x, y, tft.width() - 20, 45, HIGHLIGHT_COLOR);
  tft.fillRect(x + 1, y + 1, (tft.width() - 22) * container.current_capacity / container.max_capacity, 43, 
               container.low_stock ? WARNING_COLOR : SUCCESS_COLOR);
  
  // Text
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(x + 5, y + 5);
  tft.print(container.medicine_name);
  
  tft.setCursor(x + 5, y + 25);
  tft.printf("%d/%d", container.current_capacity, container.max_capacity);
  
  if (container.low_stock) {
    tft.setTextColor(WARNING_COLOR);
    tft.setCursor(x + 150, y + 25);
    tft.print("LOW");
  }
}

void drawReminderItem(int x, int y, Reminder reminder) {
  // Draw rectangle box with pink background
  uint16_t pinkColor = 0xF81F; // Pink color
  tft.fillRect(x, y, tft.width() - 20, 40, pinkColor);
  tft.drawRect(x, y, tft.width() - 20, 40, TEXT_COLOR);
  
  // Draw text
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  
  tft.setCursor(x + 8, y + 5);
  tft.print(reminder.medicine_name);
  
  // Build time string from all times (show first 2)
  String timeStr = "";
  for (int i = 0; i < reminder.timeCount && i < 2; i++) {
    if (i > 0) timeStr += ", ";
    timeStr += reminder.times[i];
  }
  if (reminder.timeCount > 2) timeStr += "...";
  
  tft.setCursor(x + 8, y + 22);
  tft.printf("C%d | %s", reminder.container_id, timeStr.c_str());
}

void drawScheduleItem(int x, int y, DailySchedule schedule) {
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  
  tft.setCursor(x, y);
  tft.printf("%s - %s", schedule.time.c_str(), schedule.medicine_name.c_str());
  
  tft.setCursor(x, y + 20);
  tft.printf("%d pills", schedule.dosage);
  
  // Status indicator
  uint16_t statusColor = (schedule.status == "completed") ? SUCCESS_COLOR : 
                         (schedule.status == "pending") ? WARNING_COLOR : TEXT_COLOR;
  tft.fillCircle(x + tft.width() - 30, y + 12, 6, statusColor);
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

void drawTakeMedicineConfirmation() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  
  // Title
  tft.setTextSize(3);
  tft.setCursor(20, 15);
  tft.print("Medication");
  
  // Timer - clear area first
  tft.fillRect(270, 15, 50, 20, BACKGROUND_COLOR);
  unsigned long elapsed = (millis() - confirmationStartTime) / 1000;
  int remaining = pendingConfirmation.timeout_seconds - elapsed;
  if (remaining < 0) remaining = 0;
  tft.setTextSize(2);
  tft.setCursor(270, 15);
  tft.printf("%ds", remaining);
  
  // Medicine list
  int yPos = 50;
  tft.setTextSize(2);
  for (int i = 0; i < pendingConfirmation.reminder_count && i < 10; i++) {
    ReminderItem& item = pendingConfirmation.reminders[i];
    
    tft.setCursor(10, yPos);
    tft.print(item.medicine_name);
    
    tft.setCursor(10, yPos + 20);
    tft.setTextSize(2);
    tft.printf("Container %d | %d pills", item.container_id, item.dosage);
    
    yPos += 50;
    
    if (yPos > tft.height() - 140) break; // Stop if too many
  }
  
  // Confirm button (left side, bigger for elderly)
  tft.fillRect(10, tft.height() - 70, 145, 60, SUCCESS_COLOR);
  tft.drawRect(10, tft.height() - 70, 145, 60, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(20, tft.height() - 55);
  tft.print("CONFIRM");
  
  // Cancel button (right side, bigger for elderly)
  tft.fillRect(165, tft.height() - 70, 145, 60, ALARM_COLOR);
  tft.drawRect(165, tft.height() - 70, 145, 60, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(185, tft.height() - 55);
  tft.print("CANCEL");
}

void drawQuantityConfirmation() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  
  // Title
  tft.setTextSize(3);
  tft.setCursor(30, 20);
  tft.print("Check Pills");
  
  // Medicine info
  tft.setTextSize(2);
  int yPos = 80;
  for (int i = 0; i < pendingConfirmation.reminder_count && i < 10; i++) {
    ReminderItem& item = pendingConfirmation.reminders[i];
    
    tft.setCursor(20, yPos);
    tft.print(item.medicine_name);
    
    tft.setCursor(20, yPos + 25);
    tft.printf("Expected: %d pills", item.dosage);
    
    yPos += 60;
    
    if (yPos > tft.height() - 140) break;
  }
  
  // Question
  tft.setTextSize(2);
  tft.setCursor(20, tft.height() - 120);
  tft.print("Got correct amount?");
  
  // Yes button (left)
  tft.fillRect(20, tft.height() - 60, 100, 40, SUCCESS_COLOR);
  tft.drawRect(20, tft.height() - 60, 100, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(45, tft.height() - 45);
  tft.print("YES");
  
  // One More button (right)
  tft.fillRect(tft.width() - 120, tft.height() - 60, 100, 40, WARNING_COLOR);
  tft.drawRect(tft.width() - 120, tft.height() - 60, 100, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(tft.width() - 105, tft.height() - 50);
  tft.print("ONE");
  tft.setCursor(tft.width() - 105, tft.height() - 35);
  tft.print("MORE");
}

void drawJamAlert() {
  tft.fillScreen(ALARM_COLOR);
  tft.setTextColor(TFT_WHITE);
  
  // Warning icon (!)
  tft.setTextSize(4);
  tft.setCursor(tft.width() / 2 - 10, 20);
  tft.print("!");
  
  // Title
  tft.setTextSize(3);
  tft.setCursor(40, 80);
  tft.print("JAM DETECTED");
  
  // Details
  tft.setTextSize(2);
  tft.setCursor(20, 130);
  tft.printf("Container: %d", jamAlertContainer);
  
  tft.setCursor(20, 155);
  tft.print(jamAlertMedicine);
  
  tft.setCursor(20, 180);
  tft.printf("%d pills remaining", jamAlertPillsRemaining);
  
  // Instructions
  tft.setTextSize(2);
  tft.setCursor(20, 220);
  tft.print("Please clear the jam");
  tft.setCursor(20, 245);
  tft.print("and press Continue");
  
  // Continue button
  tft.fillRect(tft.width() / 2 - 60, tft.height() - 60, 120, 40, TFT_WHITE);
  tft.setTextColor(ALARM_COLOR);
  tft.setTextSize(2);
  tft.setCursor(tft.width() / 2 - 50, tft.height() - 45);
  tft.print("CONTINUE");
}

void drawWiFiError() {
  tft.fillScreen(ALARM_COLOR);
  tft.setTextColor(TFT_WHITE);
  
  // Error icon (X)
  tft.setTextSize(4);
  tft.setCursor(tft.width() / 2 - 10, 20);
  tft.print("X");
  
  // Title
  tft.setTextSize(3);
  tft.setCursor(30, 80);
  tft.print("WiFi Error");
  
  // Message
  tft.setTextSize(2);
  tft.setCursor(20, 130);
  // Word wrap the message
  int lineY = 130;
  int lineLen = 0;
  String word = "";
  for (unsigned int i = 0; i < wifiErrorMessage.length(); i++) {
    char c = wifiErrorMessage[i];
    if (c == ' ' || i == wifiErrorMessage.length() - 1) {
      if (i == wifiErrorMessage.length() - 1 && c != ' ') word += c;
      if (lineLen + word.length() > 20) {
        lineY += 25;
        lineLen = 0;
        tft.setCursor(20, lineY);
      }
      tft.print(word + " ");
      lineLen += word.length() + 1;
      word = "";
    } else {
      word += c;
    }
  }
  
  // Instruction
  tft.setTextSize(2);
  tft.setCursor(20, 220);
  if (wifiErrorInstruction.length() > 0) {
    tft.print(wifiErrorInstruction);
  } else {
    tft.print("Please restart device");
  }
  
  // OK button
  tft.fillRect(tft.width() / 2 - 40, tft.height() - 60, 80, 40, TFT_WHITE);
  tft.setTextColor(ALARM_COLOR);
  tft.setTextSize(2);
  tft.setCursor(tft.width() / 2 - 15, tft.height() - 45);
  tft.print("OK");
}

void drawControlConfirmation() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);
  
  // Title
  tft.setTextSize(3);
  tft.setCursor(20, 15);
  tft.print("Control");
  
  // Timer - clear area first
  tft.fillRect(270, 15, 50, 20, BACKGROUND_COLOR);
  unsigned long elapsed = (millis() - confirmationStartTime) / 1000;
  int remaining = pendingConfirmation.timeout_seconds - elapsed;
  if (remaining < 0) remaining = 0;
  tft.setTextSize(2);
  tft.setCursor(270, 15);
  tft.printf("%ds", remaining);
  
  // Control details
  tft.setTextSize(2);
  tft.setCursor(20, 60);
  tft.print("Action:");
  tft.setCursor(20, 85);
  tft.print(pendingConfirmation.control.action);
  
  tft.setCursor(20, 120);
  tft.print("Medicine:");
  tft.setCursor(20, 145);
  tft.print(pendingConfirmation.control.medicine_name);
  
  tft.setCursor(20, 180);
  tft.printf("Container: %d", pendingConfirmation.control.container_id);
  
  tft.setCursor(20, 205);
  tft.printf("Quantity: %d", pendingConfirmation.control.quantity);
  
  // Message if available
  if (pendingConfirmation.control.message.length() > 0) {
    tft.setCursor(20, 230);
    tft.print(pendingConfirmation.control.message);
  }
  
  // Confirm button (left side, bigger for elderly)
  tft.fillRect(10, tft.height() - 70, 145, 60, SUCCESS_COLOR);
  tft.drawRect(10, tft.height() - 70, 145, 60, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(20, tft.height() - 55);
  tft.print("CONFIRM");
  
  // Cancel button (right side, bigger for elderly)
  tft.fillRect(165, tft.height() - 70, 145, 60, ALARM_COLOR);
  tft.drawRect(165, tft.height() - 70, 145, 60, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(185, tft.height() - 55);
  tft.print("CANCEL");
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