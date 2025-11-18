#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#define SELENOID_1 26
#define SELENOID_2 27

TFT_eSPI tft = TFT_eSPI();
#define TOUCH_CS 15   // T_CS connected to GPIO 15
// #define TOUCH_IRQ 2   // T_IRQ connected to GPIO 2
XPT2046_Touchscreen ts(TOUCH_CS);
#define TFT_GREY 0x5AEB
#define TFT_DARKBLUE 0x0011
#define TFT_LIGHTGREY 0xC618

// Keypad layout
#define KEY_W 100
#define KEY_H 45
#define KEY_SPACING 6
#define KEY_START_X 40
#define KEY_START_Y 105

// Text input
String phoneNumber = "";
#define MAX_PHONE_LENGTH 15

String otpNumber = "";
#define MAX_OTP_LENGTH 6

String isbnNumber = "";
#define MAX_ISBN_LENGTH 13

// --- Button layout constants ---
#define BTN_WIDTH  220
#define BTN_HEIGHT 60
#define BTN_RADIUS 10

#define BACKLIGHT 4

enum ScreenState { MENU, PENGAMBILAN, PENGEMBALIAN, INPUT_OTP, INPUT_ISBN };
ScreenState currentScreen = MENU;

int btnX, btnY1, btnY2;

void drawButton(int x, int y, const char* label) {
  // Draw rounded black rectangle button
  tft.fillRoundRect(x, y, BTN_WIDTH, BTN_HEIGHT, BTN_RADIUS, TFT_BLACK);
  tft.drawRoundRect(x, y, BTN_WIDTH, BTN_HEIGHT, BTN_RADIUS, TFT_BLACK);
  
  // Center text
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);  // Middle-center text alignment
  tft.drawString(label, x + BTN_WIDTH / 2, y + BTN_HEIGHT / 2, 4);
}

void drawMenu() {
  tft.fillScreen(TFT_GREY);
  int screenW = tft.width();
  int screenH = tft.height();
  btnX = (screenW - BTN_WIDTH) / 2;
  btnY1 = screenH / 2 - BTN_HEIGHT - 10;
  btnY2 = screenH / 2 + 10;
  drawButton(btnX, btnY1, "PENGAMBILAN");
  drawButton(btnX, btnY2, "PENGEMBALIAN");
}

void drawTextBox() {
  // Draw text input box
  int boxX = 20;
  int boxY = 60;
  int boxW = tft.width() - 40;
  int boxH = 40;
  
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 5, TFT_WHITE);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, TFT_BLACK);
  
  // Display phone number
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(phoneNumber, boxX + 10, boxY + boxH/2, 4);
}

void drawKeypad() {
  const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
  
  for (int i = 0; i < 12; i++) {
    int row = i / 3;
    int col = i % 3;
    int x = KEY_START_X + col * (KEY_W + KEY_SPACING);
    int y = KEY_START_Y + row * (KEY_H + KEY_SPACING);
    
    // Draw button
    tft.fillRoundRect(x, y, KEY_W, KEY_H, 8, TFT_DARKBLUE);
    tft.drawRoundRect(x, y, KEY_W, KEY_H, 8, TFT_WHITE);
    
    // Draw key label
    tft.setTextColor(TFT_WHITE, TFT_DARKBLUE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(keys[i], x + KEY_W/2, y + KEY_H/2, 4);
  }
  
  // Draw backspace button
  int delX = KEY_START_X + 3 * (KEY_W + KEY_SPACING);
  int delY = KEY_START_Y;
  tft.fillRoundRect(delX, delY, KEY_W, KEY_H, 8, TFT_RED);
  tft.drawRoundRect(delX, delY, KEY_W, KEY_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("DEL", delX + KEY_W/2, delY + KEY_H/2, 4);
  
  // Draw OK button
  int okY = KEY_START_Y + (KEY_H + KEY_SPACING);
  tft.fillRoundRect(delX, okY, KEY_W, KEY_H, 8, TFT_GREEN);
  tft.drawRoundRect(delX, okY, KEY_W, KEY_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_GREEN);
  tft.drawString("OK", delX + KEY_W/2, okY + KEY_H/2, 4);

  // Draw BACK button (row 4, column 4 - under OK button)
  int backY = KEY_START_Y + 2 * (KEY_H + KEY_SPACING);
  tft.fillRoundRect(delX, backY, KEY_W, KEY_H, 8, TFT_ORANGE);
  tft.drawRoundRect(delX, backY, KEY_W, KEY_H, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.drawString("BACK", delX + KEY_W/2, backY + KEY_H/2, 4);
}

int getTouchedKey(int x, int y) {
  // Check number pad (0-11)
  for (int i = 0; i < 12; i++) {
    int row = i / 3;
    int col = i % 3;
    int keyX = KEY_START_X + col * (KEY_W + KEY_SPACING);
    int keyY = KEY_START_Y + row * (KEY_H + KEY_SPACING);
    
    if (x >= keyX && x < keyX + KEY_W && y >= keyY && y < keyY + KEY_H) {
      return i;
    }
  }
  
  // Check DEL button (12)
  int delX = KEY_START_X + 3 * (KEY_W + KEY_SPACING);
  int delY = KEY_START_Y;
  if (x >= delX && x < delX + KEY_W && y >= delY && y < delY + KEY_H) {
    return 12;
  }
  
  // Check OK button (13)
  int okY = KEY_START_Y + (KEY_H + KEY_SPACING);
  if (x >= delX && x < delX + KEY_W && y >= okY && y < okY + KEY_H) {
    return 13;
  }
  
  // Check BACK button (14) - row 4, column 4
  int backY = KEY_START_Y + 2 * (KEY_H + KEY_SPACING);
  if (x >= delX && x < delX + KEY_W && y >= backY && y < backY + KEY_H) {
    return 14;
  }
  
  return -1;
}

void handlePhoneNumberInput() {
  tft.fillScreen(TFT_LIGHTGREY);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Enter Phone Number:", tft.width() / 2, 20, 4);
  
  phoneNumber = "";
  drawTextBox();
  drawKeypad();
  
  // Input loop
  while (true) {
    if (!ts.touched()) continue;
    TS_Point p = ts.getPoint();
    int x = map(p.x, 3800, 200, 0, tft.width());
    int y = map(p.y, 3800, 200, 0, tft.height());
    
    int key = getTouchedKey(x, y);
    
    if (key >= 0 && key <= 11) {
      const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
      if (phoneNumber.length() < MAX_PHONE_LENGTH) {
        phoneNumber += keys[key];
        drawTextBox();
      }
      delay(200);
    }
    else if (key == 12) {
      if (phoneNumber.length() > 0) {
        phoneNumber.remove(phoneNumber.length() - 1);
        drawTextBox();
      }
      delay(200);
    }
    else if (key == 13) {
      Serial.printf("Phone number entered: %s\n", phoneNumber.c_str());
      currentScreen = INPUT_OTP;
      return;
    }
    else if (key == 14) {
      currentScreen = MENU;
      return;
    }
  }
}

void handleOTPInput() {
  tft.fillScreen(TFT_LIGHTGREY);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Enter OTP Number:", tft.width() / 2, 20, 4);
  
  otpNumber = "";
  drawTextBox();
  drawKeypad();

  // Input loop
  while (true) {
    if (!ts.touched()) continue;
    TS_Point p = ts.getPoint();
    int x = map(p.x, 3800, 200, 0, tft.width());
    int y = map(p.y, 3800, 200, 0, tft.height());
    
    int key = getTouchedKey(x, y);
    
    if (key >= 0 && key <= 11) {
      const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
      if (phoneNumber.length() < MAX_PHONE_LENGTH) {
        phoneNumber += keys[key];
        drawTextBox();
      }
      delay(200);
    }
    else if (key == 12) {
      if (phoneNumber.length() > 0) {
        phoneNumber.remove(phoneNumber.length() - 1);
        drawTextBox();
      }
      delay(200);
    }
    else if (key == 13) {
      Serial.printf("Phone number entered: %s\n", phoneNumber.c_str());
      currentScreen = INPUT_OTP;
      return;
    }
    else if (key == 14) {
      currentScreen = MENU;
      return;
    }
  }
}

void handleISBNInput() {
  tft.fillScreen(TFT_LIGHTGREY);
  tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Enter ISBN Number:", tft.width() / 2, 20, 4);
  
  isbnNumber = "";
  drawTextBox();
  drawKeypad();

  // Input loop
  while (true) {
    if (!ts.touched()) continue;
    TS_Point p = ts.getPoint();
    int x = map(p.x, 3800, 200, 0, tft.width());
    int y = map(p.y, 3800, 200, 0, tft.height());
    
    int key = getTouchedKey(x, y);
    
    if (key >= 0 && key <= 11) {
      const char* keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
      if (phoneNumber.length() < MAX_PHONE_LENGTH) {
        phoneNumber += keys[key];
        drawTextBox();
      }
      delay(200);
    }
    else if (key == 12) {
      if (phoneNumber.length() > 0) {
        phoneNumber.remove(phoneNumber.length() - 1);
        drawTextBox();
      }
      delay(200);
    }
    else if (key == 13) {
      Serial.printf("Phone number entered: %s\n", phoneNumber.c_str());
      currentScreen = MENU;
      return;
    }
    else if (key == 14) {
      currentScreen = MENU;
      return;
    }
  }
}



void setup() {
  Serial.begin(115200);
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, HIGH);
  pinMode(SELENOID_1, OUTPUT);
  pinMode(SELENOID_2, OUTPUT);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_GREY);

  ts.begin();
  ts.setRotation(3);

  drawMenu();
}

void loop() {
  digitalWrite(SELENOID_1, HIGH);
  digitalWrite(SELENOID_2, HIGH);
  
  if (currentScreen == MENU) {
    if (!ts.touched()) return;
    TS_Point p = ts.getPoint();
    int x = map(p.x, 3800, 200, 0, tft.width());
    int y = map(p.y, 3800, 200, 0, tft.height());
    
    if (x > btnX && x < btnX + BTN_WIDTH && y > btnY1 && y < btnY1 + BTN_HEIGHT) {
      currentScreen = PENGAMBILAN;
      // handlePhoneNumberInput();
      // drawMenu();
      delay(300);
    }
    else if (x > btnX && x < btnX + BTN_WIDTH && y > btnY2 && y < btnY2 + BTN_HEIGHT) {
      currentScreen = PENGEMBALIAN;
      // handlePhoneNumberInput();
      // drawMenu();
      delay(300);
    }
  } else if (currentScreen == PENGAMBILAN) {
    handlePhoneNumberInput();
    // drawMenu();
  } else if (currentScreen == PENGEMBALIAN) {
    handleISBNInput();
    // drawMenu();
  } else if (currentScreen == INPUT_OTP) {
    handleOTPInput();
    // drawMenu();
  }
  else if (currentScreen == INPUT_ISBN) {
    handleISBNInput();
    // drawMenu();
  } else {
      drawMenu();
      delay(300);
  }
}

