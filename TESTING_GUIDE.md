# TFT Display - Testing Guide

## Overview

The TFT display firmware includes built-in dummy data generators for testing all display states and message types without needing the main ESP32 device.

---

## Quick Start Testing

### Enable Dummy Data Auto-Send

In `loop()` function, dummy data is sent every 30 seconds by default when uncommented:

```cpp
// Debug: Send dummy data every 30 seconds for testing
static unsigned long lastDummyTime = 0;
if (millis() - lastDummyTime > 30000) {
    lastDummyTime = millis();
    generateDummyData(); // Sends all dummy data
}
```

**Options:**
- `generateDummyData()` - Sends all test data sequentially (recommended for first test)
- `sendDummyDeviceInfo()` - Device identity and state
- `sendDummySensorData()` - Temperature/humidity (with random variation)
- `sendDummySystemStatus()` - WiFi/MQTT/RTC status
- `sendDummyDailySchedule()` - Today's medication schedule
- `sendDummyReminderAlert()` - Single medication alert
- `sendDummyGroupedReminderAlert()` - Multiple meds at same time
- `sendDummyAlarmStatus(true/false)` - Alarm on/off
- `sendDummyDispensingStatus("started"/"completed")` - Dispensing progress
- `sendDummyStockAlert()` - Low stock warning

---

## Testing Each Message Type

### 1. Device Information
```
Function: sendDummyDeviceInfo()
Displays: Device name, state, temperature, humidity
Expected Behavior: Shows on home screen
```

### 2. System Status
```
Function: sendDummySystemStatus()
Displays: WiFi (C/D), MQTT (C/D), SD Card, RTC status
Expected Behavior: Status icons update on home screen
```

### 3. Sensor Data
```
Function: sendDummySensorData()
Displays: Real-time temperature & humidity
Expected Behavior: Values update on home screen (includes random ±1°C variation)
```

### 4. Container Information
```
Function: sendDummyContainersInfo()
Displays: 4 medicine containers with stock levels
Test Data:
  - Container 1: Paracetamol (50/100) - Normal
  - Container 2: Aspirin (30/100) - Normal
  - Container 3: Ibuprofen (5/100) - LOW STOCK
  - Container 4: Amoxicillin (20/100) - Normal
Expected Behavior:
  - Navigate to Containers screen
  - See all 4 containers with progress bars
  - Container 3 shows "LOW STOCK" warning
```

### 5. Reminders
```
Function: sendDummyRemindersInfo()
Displays: 3 active reminders with schedules
Test Data:
  - Reminder 1: Paracetamol - Once Daily at 08:00
  - Reminder 2: Aspirin - Twice Daily at 08:00 & 14:00
  - Reminder 3: Ibuprofen - Inactive (As needed)
Expected Behavior:
  - Navigate to Reminders screen
  - See only 2 active reminders
  - Inactive reminder not displayed
```

### 6. Daily Schedule
```
Function: sendDummyDailySchedule()
Displays: Today's medication schedule with status
Test Data:
  - 08:00 Paracetamol - Pending
  - 08:00 Aspirin - Completed
  - 14:00 Aspirin - Pending
  - 20:00 Paracetamol - Pending
Expected Behavior:
  - Navigate to Schedule screen
  - Shows all 4 items
  - Color indicators: Completed (green), Pending (yellow)
```

### 7. Single Reminder Alert
```
Function: sendDummyReminderAlert()
Triggers: STATE_ALARM (full screen alert)
Test Data: Paracetamol at 17:25, dosage 1
Expected Behavior:
  - Screen turns RED
  - "ALERT!" displays with medicine name
  - "Dismiss" button appears
  - Click Dismiss to return to home
```

### 8. Grouped Reminder Alert
```
Function: sendDummyGroupedReminderAlert()
Triggers: STATE_ALARM with multiple medicines
Test Data: 2 medicines at 14:00
Expected Behavior:
  - Screen turns RED
  - Shows both Paracetamol and Aspirin
  - Alert count: 2
  - Single "Dismiss" button dismisses both
```

### 9. Alarm Status
```
Function: sendDummyAlarmStatus(true)  // Activate
Function: sendDummyAlarmStatus(false) // Deactivate
Expected Behavior:
  - sendDummyAlarmStatus(true): Switches to alarm screen
  - sendDummyAlarmStatus(false): Returns to previous screen
```

### 10. Dispensing Status
```
Function: sendDummyDispensingStatus("started")
  - Shows progress: "Dispensing..." with animation
  - Container 1, Dosage 2

Function: sendDummyDispensingStatus("completed")
  - Shows success: "Complete! Dispensed 2 pills"
  - "OK" button appears
Expected Behavior:
  - Animation shows during "started" state
  - Success message on completion
```

### 11. Stock Alert
```
Function: sendDummyStockAlert()
Test Data: Ibuprofen - Current: 5, Minimum: 10
Expected Behavior:
  - Serial shows: "Stock Alert: Ibuprofen - Current: 5, Minimum: 10"
  - Bottom bar displays alert (if implemented in UI)
```

---

## Complete Testing Sequence

### Option 1: Auto-Send All Data (Easiest)
```cpp
// In loop(), uncomment:
generateDummyData(); // Sends all test data
```
**Result:** Every 30 seconds, all dummy data is sent sequentially

**Sequence:**
1. Device Info
2. System Status
3. Sensor Data
4. Containers Info
5. Reminders Info
6. Daily Schedule

### Option 2: Send Individual Data (Controlled)
```cpp
// Comment out auto-send, then manually call in setup():
void setup() {
    // ... existing setup code ...
    
    // Test one message at a time
    delay(2000);
    sendDummyDailySchedule();
    delay(2000);
    sendDummyReminderAlert();
    delay(5000);
    sendDummyDispensingStatus("started");
    delay(3000);
    sendDummyDispensingStatus("completed");
}
```

### Option 3: Serial Commands (For Interactive Testing)
Add this to the loop for serial-based testing:
```cpp
if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "all") generateDummyData();
    else if (cmd == "dev") sendDummyDeviceInfo();
    else if (cmd == "sys") sendDummySystemStatus();
    else if (cmd == "sensor") sendDummySensorData();
    else if (cmd == "container") sendDummyContainersInfo();
    else if (cmd == "reminder") sendDummyRemindersInfo();
    else if (cmd == "schedule") sendDummyDailySchedule();
    else if (cmd == "alert") sendDummyReminderAlert();
    else if (cmd == "grouped") sendDummyGroupedReminderAlert();
    else if (cmd == "alarm_on") sendDummyAlarmStatus(true);
    else if (cmd == "alarm_off") sendDummyAlarmStatus(false);
    else if (cmd == "dispense_start") sendDummyDispensingStatus("started");
    else if (cmd == "dispense_done") sendDummyDispensingStatus("completed");
    else if (cmd == "stock") sendDummyStockAlert();
    else if (cmd == "help") {
        Serial.println("Commands: all, dev, sys, sensor, container, reminder, schedule, alert, grouped, alarm_on, alarm_off, dispense_start, dispense_done, stock");
    }
}
```

---

## Test Data Details

### Containers Test Data
```json
{
  "Paracetamol": {
    "id": 1,
    "quantity": 50,
    "max": 100,
    "status": "normal"
  },
  "Aspirin": {
    "id": 2,
    "quantity": 30,
    "max": 100,
    "status": "normal"
  },
  "Ibuprofen": {
    "id": 3,
    "quantity": 5,
    "max": 100,
    "status": "LOW STOCK"
  },
  "Amoxicillin": {
    "id": 4,
    "quantity": 20,
    "max": 100,
    "status": "normal"
  }
}
```

### Schedule Test Data
```
Time | Medicine   | Dosage | Status
-----|------------|--------|----------
08:00| Paracetamol| 1      | Pending
08:00| Aspirin    | 1      | Completed
14:00| Aspirin    | 1      | Pending
20:00| Paracetamol| 1      | Pending
```

### Sensor Data Variation
- Base Temperature: 26.7°C
- Random Variation: ±1°C
- Base Humidity: 57.0%
- Random Variation: ±2%

---

## Display State Testing

### STATE_HOME (Home Screen)
- ✅ Shows current date & time
- ✅ WiFi/MQTT status icons (C/D)
- ✅ Temperature & Humidity
- ✅ 2 upcoming reminders preview
- ✅ Navigation buttons

**Test:**
```cpp
sendDummyDeviceInfo();
sendDummySensorData();
sendDummyDailySchedule();
```

### STATE_CONTAINERS (Container List)
- ✅ 4 containers displayed
- ✅ Progress bars for stock
- ✅ "LOW STOCK" warning for container 3
- ✅ Back button

**Test:**
```cpp
sendDummyContainersInfo();
// Then click "Containers" button
```

### STATE_REMINDERS (Reminders List)
- ✅ Only active reminders shown
- ✅ Medicine name & container info
- ✅ Active indicator (green dot)
- ✅ Back button

**Test:**
```cpp
sendDummyRemindersInfo();
// Then click "View More" button
```

### STATE_SCHEDULE (Daily Schedule)
- ✅ All scheduled items listed
- ✅ Time, medicine, dosage displayed
- ✅ Status indicators (pending/completed)
- ✅ Back button

**Test:**
```cpp
sendDummyDailySchedule();
// Then click "Schedule" button
```

### STATE_ALARM (Alert Screen)
- ✅ Full RED screen
- ✅ "ALERT!" message
- ✅ Medicine name prominent
- ✅ "Dismiss" button
- ✅ Returns to home on dismiss

**Test:**
```cpp
sendDummyReminderAlert();
// Screen automatically switches to alarm
// Click "Dismiss" button
```

### STATE_DISPENSING (Dispensing Progress)
- ✅ Shows "Dispensing..." with animation
- ✅ Container & dosage info
- ✅ Loading animation

**Test:**
```cpp
sendDummyDispensingStatus("started");
// Wait ~3 seconds
sendDummyDispensingStatus("completed");
// Click "OK" button
```

---

## Troubleshooting

### Dummy Data Not Appearing

1. **Check Serial Output**
   - Open Serial Monitor
   - Should see: "Sent dummy: [type]"

2. **Verify Loop Timing**
   - Default: Every 30 seconds
   - Check `lastDummyTime` logic

3. **Check JSON Serialization**
   - Ensure `ArduinoJson` library installed
   - Check serial monitor for parse errors

### Display Not Updating

1. **Check State Transitions**
   - Verify `currentState` changes
   - Check if button touch is registered

2. **Verify Data Reception**
   - Serial.println should show received JSON
   - Check `processIncomingData()` is called

3. **Check Touch Calibration**
   - Verify touch coordinates are mapped correctly
   - Try adjusting coordinate mapping in `handleTouchInput()`

### Sensor Values Not Changing

- Sensor data includes random variation (±1-2%)
- Wait 30 seconds between sends to see change

---

## Performance Testing

### Memory Usage
Monitor `ESP.getFreeHeap()` when:
- Creating large JSON documents
- Updating all data simultaneously
- Running dummy data in loop

### Touch Response
- Touch should respond within 100ms
- If delayed, reduce loop delay from 100ms

### Display Refresh
- Each screen update should be smooth
- No flicker between states

---

## Integration Testing

### With Main ESP32
1. Disable dummy data generation
2. Connect TFT RX (GPIO 16) to Main ESP32 TX (GPIO 17)
3. Connect TFT TX (GPIO 17) to Main ESP32 RX (GPIO 16)
4. Use "check" command on main ESP32 to trigger test data

### Real Message Flow
```
Main ESP32 → JSON Message → TFT Serial RX
TFT → Parse JSON → Update Display
TFT → Display State Changes
```

---

## Recommended Testing Order

1. **Power On**
   - Should show "MINDER DEVICE" startup screen
   - Then show home screen

2. **Enable Dummy Data**
   - Uncomment `generateDummyData()` in loop
   - Watch all data populate

3. **Test Each Screen**
   - Tap "Containers" button → should show container list
   - Tap "View More" button → should show reminders
   - Tap "Schedule" button → should show daily schedule

4. **Test Alerts**
   - Call `sendDummyReminderAlert()` → red alert screen
   - Click "Dismiss" → back to home

5. **Test Dispensing**
   - Call `sendDummyDispensingStatus("started")` → animation
   - Call `sendDummyDispensingStatus("completed")` → success
   - Click "OK" → back to home

---

## Cleanup

Before deploying with real data:

1. **Comment out auto-send:**
   ```cpp
   // generateDummyData(); // Commented out
   ```

2. **Remove test functions if not needed** (they take ~2KB memory)

3. **Enable real serial communication** with main ESP32

---

## Serial Monitor Output Reference

### Successful Startup
```
TFT Display Ready
```

### Dummy Data Sent
```
Sent dummy: device_info
Sent dummy: system_status
Sent dummy: sensor_data
Sent dummy: containers_info
Sent dummy: reminders_info
Sent dummy: daily_schedule
```

### Received Messages
```
Received: {"type":"device_info",...}
```

### Display State Changes
```
Touch at (160, 120)
```

---

**Version:** 1.0  
**Last Updated:** December 2025  
**Board:** ESP32 with TFT_eSPI + XPT2046 Touch
