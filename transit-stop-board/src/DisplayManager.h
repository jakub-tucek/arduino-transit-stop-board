#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

enum class TouchAction {
  NONE,
  SWITCH_STOP,
  PREV_PAGE,
  NEXT_PAGE,
  WATCH_SELECT,
  WATCH_CANCEL
};

class DisplayManager {
public:
  DisplayManager();
  
  bool begin();
  bool isDisplayOk() const { return displayOk; }
  bool isTouchOk() const { return touchOk; }
  
  void clearScreen();
  void fillScreen(uint16_t color);
  
  TouchAction pollTouch();
  int getLastTouchedRow() const { return lastTouchedRow; }
   
  // Drawing primitives
  void drawText(int16_t x, int16_t y, const String& text, uint16_t color, uint8_t size);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
  void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
  
  // Screen dimensions
  static constexpr int WIDTH = 320;
  static constexpr int HEIGHT = 240;
  static constexpr int HEADER_H = 30;
  static constexpr int STOP_BAR_H = 4;  // Reduced to minimal separator
  static constexpr int FOOTER_H = 44;
  static constexpr int CONTENT_TOP = HEADER_H + STOP_BAR_H + 2;
  static constexpr int CONTENT_BOTTOM = HEIGHT - FOOTER_H - 2;
  
  // Colors
  static constexpr uint16_t BG = 0x0000;
  static constexpr uint16_t HEADER = 0x000F;
  static constexpr uint16_t PANEL = 0x03EF;
  static constexpr uint16_t BUTTON = 0x001F;
  static constexpr uint16_t BUTTON_ACTIVE = 0xFDA0;
  static constexpr uint16_t TEXT = 0xFFFF;
  static constexpr uint16_t DIM = 0xC618;
  static constexpr uint16_t LINE = 0x07FF;
  static constexpr uint16_t WARN = 0xFFE0;
  static constexpr uint16_t ERR = 0xF800;
  static constexpr uint16_t OK = 0x07E0;

private:
  Adafruit_ILI9341 tft;
  XPT2046_Touchscreen touch;
  
  bool displayOk = false;
  bool touchOk = false;
  int lastTouchedRow = -1;
  
  static constexpr int TFT_CS = 5;
  static constexpr int TFT_DC = 2;
  static constexpr int TFT_RST = 4;
  static constexpr int TOUCH_CS = 14;
  static constexpr int TOUCH_IRQ = 27;
  
  static constexpr int TOUCH_MIN_X = 200;
  static constexpr int TOUCH_MAX_X = 3900;
  static constexpr int TOUCH_MIN_Y = 200;
  static constexpr int TOUCH_MAX_Y = 3900;
  
  static constexpr int BUTTON_COUNT = 3;
};
