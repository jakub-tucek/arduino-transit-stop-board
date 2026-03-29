#include "DisplayManager.h"
#include <Arduino.h>

DisplayManager::DisplayManager() 
  : tft(TFT_CS, TFT_DC, TFT_RST)
  , touch(TOUCH_CS, TOUCH_IRQ) {
}

bool DisplayManager::begin() {
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(BG);
  tft.setTextWrap(false);
  displayOk = true;

  touch.begin();
  touch.setRotation(1);
  touchOk = true;

  Serial.println("Display init OK (ILI9341 landscape 320x240)");
  return true;
}

void DisplayManager::clearScreen() {
  if (displayOk) {
    tft.fillScreen(BG);
  }
}

void DisplayManager::fillScreen(uint16_t color) {
  if (displayOk) {
    tft.fillScreen(color);
  }
}

void DisplayManager::drawText(int16_t x, int16_t y, const String& text, uint16_t color, uint8_t size) {
  if (!displayOk) return;
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.print(text);
}

void DisplayManager::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!displayOk) return;
  tft.fillRect(x, y, w, h, color);
}

void DisplayManager::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  if (!displayOk) return;
  tft.fillRoundRect(x, y, w, h, r, color);
}

void DisplayManager::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  if (!displayOk) return;
  tft.drawRoundRect(x, y, w, h, r, color);
}

void DisplayManager::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  if (!displayOk) return;
  tft.fillCircle(x, y, r, color);
}

void DisplayManager::drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  if (!displayOk) return;
  tft.drawCircle(x, y, r, color);
}

TouchAction DisplayManager::pollTouch() {
  if (!touchOk || !touch.touched()) return TouchAction::NONE;

  TS_Point p = touch.getPoint();
  Serial.print("[TOUCH] Raw: x=");
  Serial.print(p.x);
  Serial.print(" y=");
  Serial.println(p.y);

  // Validate touch coordinates
  if (p.x < TOUCH_MIN_X || p.x > TOUCH_MAX_X || p.y < TOUCH_MIN_Y || p.y > TOUCH_MAX_Y) {
    Serial.println("[TOUCH] Out of bounds, ignored");
    return TouchAction::NONE;
  }

  int x = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, WIDTH);
  int y = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, HEIGHT, 0);  // Reversed: touch bottom -> screen bottom

  Serial.print("[TOUCH] Mapped: x=");
  Serial.print(x);
  Serial.print(" y=");
  Serial.println(y);

  // Wait for release with timeout
  unsigned long touchStart = millis();
  while (touch.touched() && millis() - touchStart < 500) {
    delay(10);
  }

  // Header or stop bar tap = switch stop (expanded area)
  if (x >= 6 && x < WIDTH - 6 && y >= 0 && y <= HEADER_H + STOP_BAR_H + 10) {
    Serial.println("[TOUCH] Action: SWITCH_STOP");
    return TouchAction::SWITCH_STOP;
  }

  // Departure row tap (content area)
  if (x >= 0 && x < WIDTH && y >= CONTENT_TOP && y < HEIGHT - FOOTER_H) {
    int rowHeight = 23;
    int rowIndex = (y - CONTENT_TOP) / rowHeight;
    if (rowIndex >= 0 && rowIndex < 7) {
      lastTouchedRow = rowIndex;
      Serial.print("[TOUCH] Action: WATCH_SELECT row=");
      Serial.println(rowIndex);
      return TouchAction::WATCH_SELECT;
    }
  }

  // Footer button area
  if (x < 0 || x >= WIDTH || y < HEIGHT - FOOTER_H || y >= HEIGHT) {
    Serial.println("[TOUCH] Outside actionable area");
    return TouchAction::NONE;
  }

  const int buttonWidth = WIDTH / BUTTON_COUNT;
  int index = x / buttonWidth;

  Serial.print("[TOUCH] Button index: ");
  Serial.println(index);

  switch (index) {
    case 0:
      Serial.println("[TOUCH] Action: PREV_PAGE");
      return TouchAction::PREV_PAGE;
    case 1:
      Serial.println("[TOUCH] Action: NEXT_PAGE");
      return TouchAction::NEXT_PAGE;
    default:
      Serial.println("[TOUCH] Unknown button");
      return TouchAction::NONE;
  }
}