#include "UIManager.h"
#include "BoardSetup.h"
#include <Arduino.h>

UIManager::UIManager(DisplayManager& disp) : display(disp) {
}

String UIManager::trimToLength(const String& input, size_t maxLen) {
  if (input.length() <= maxLen) return input;
  if (maxLen <= 3) return input.substring(0, maxLen);
  return input.substring(0, maxLen - 3) + "...";
}

String UIManager::isoToHHMM(const String& iso) {
  int tPos = iso.indexOf('T');
  if (tPos < 0 || iso.length() < tPos + 6) return "--:--";
  return iso.substring(tPos + 1, tPos + 6);
}

void UIManager::clear() {
  display.clearScreen();
}

void UIManager::drawHeader(const StopConfig& stop, bool wifiOk, bool dataOk, bool isLoading) {
  display.fillRect(0, 0, DisplayManager::WIDTH, DisplayManager::HEADER_H, DisplayManager::HEADER);

  // Time in top right corner
  extern BoardSetup boardSetup;
  String timeStr = boardSetup.getCurrentTimeStr();
  int timeWidth = timeStr.length() * 12;
  display.drawText(DisplayManager::WIDTH - timeWidth - 10, 7, timeStr, DisplayManager::WARN, 2);

  // Stop name with tap icon on the left
  String stopName = trimToLength(stop.label, 14);
  display.drawText(28, 8, stopName, DisplayManager::TEXT, 2);

  // Draw small circle icon to indicate tap-to-switch
  display.fillCircle(14, 15, 5, DisplayManager::WARN);
  display.drawCircle(14, 15, 5, DisplayManager::TEXT);
  
  // Draw loading indicator next to stop name when loading
  if (isLoading) {
    static unsigned long lastUpdate = 0;
    static int frame = 0;
    int spinnerX = 32 + (stopName.length() * 12);
    if (spinnerX < DisplayManager::WIDTH - 60) {
      // Simple rotating circle animation
      unsigned long now = millis();
      if (now - lastUpdate > 200) {  // Update every 200ms
        frame = (frame + 1) % 4;
        lastUpdate = now;
      }
      // Draw animated dots - clear area first to prevent leftover characters
      display.fillRect(spinnerX - 2, 6, 35, 20, DisplayManager::HEADER);
      String dots = "";
      for (int i = 0; i <= frame; i++) dots += ".";
      display.drawText(spinnerX, 8, dots, DisplayManager::WARN, 2);
    }
  }

  // Debug to serial
  Serial.println();
  Serial.println("========================================");
  Serial.print("Stop: ");
  Serial.println(stop.label);
  Serial.print("Current time: ");
  Serial.println(timeStr);
  Serial.print("WiFi: ");
  Serial.print(wifiOk ? "OK" : "ERR");
  Serial.print(" | Data: ");
  Serial.println(dataOk ? "OK" : "ERR");
  Serial.println("----------------------------------------");
}

void UIManager::drawStopBar() {
  // Thin separator line
  display.fillRect(6, DisplayManager::HEADER_H + 1, 
                   DisplayManager::WIDTH - 12, 2, 
                   DisplayManager::PANEL);
}

void UIManager::drawWatchedIndicator(int row) {
  int y = DisplayManager::CONTENT_TOP + row * ROW_HEIGHT;
  int cy = y + ROW_HEIGHT / 2;
  // Draw eye icon as a filled circle with dot
  display.fillCircle(300, cy, 6, DisplayManager::WARN);
  display.drawCircle(300, cy, 6, DisplayManager::TEXT);
  display.fillCircle(300, cy, 2, DisplayManager::TEXT);
}

void UIManager::drawDepartureRow(int index, const Departure& item, bool isWatched) {
  int y = DisplayManager::CONTENT_TOP + index * ROW_HEIGHT;
  bool odd = (index % 2) == 1;
  uint16_t rowBg = odd ? DisplayManager::BG : DisplayManager::HEADER;
  
  // Highlight watched row
  if (isWatched) {
    rowBg = DisplayManager::BUTTON_ACTIVE;  // Use active button color for highlight
  }
  
  // Status text - show platform or delay
  String statusText = item.platform.length() ? item.platform : "";
  uint16_t statusColor = DisplayManager::DIM;
  uint8_t statusSize = 1;
  int statusX = 280;
  
  // Show delay if present (positive delay = late, negative = early)
  if (item.delaySeconds > 60) {
    int delayMins = item.delaySeconds / 60;
    statusText = "+" + String(delayMins);
    statusColor = DisplayManager::ERR;  // Red for delay
    statusSize = 2;
    statusX = 256;
  } else if (item.delaySeconds < -30) {
    int earlyMins = abs(item.delaySeconds) / 60;
    statusText = "-" + String(earlyMins);
    statusColor = DisplayManager::OK;  // Green for early
    statusSize = 2;
    statusX = 256;
  }

  display.fillRect(0, y, DisplayManager::WIDTH, ROW_HEIGHT - 1, rowBg);
  
  // Line number
  display.drawText(6, y + 5, trimToLength(item.line, 5), DisplayManager::WARN, 2);
  
  // Destination uses the larger font, so keep it slightly shorter to preserve spacing.
  display.drawText(50, y + 4, trimToLength(item.headsign, 12), DisplayManager::TEXT, 2);
  
  // Minutes until departure or departure time for far departures
  String displayStr;
  bool isFarDeparture = false;
  
  if (item.minutes == "<1") {
    displayStr = "<1m";
  } else {
    int minutes = item.minutes.toInt();
    if (minutes > 60) {
      // Show departure time for departures > 60 min
      displayStr = item.departureTime;
      isFarDeparture = true;
    } else {
      displayStr = item.minutes + "m";
    }
  }
  
  // Draw departure info - time in smaller font if it's a far departure
  if (isFarDeparture) {
    display.drawText(200, y + 5, displayStr, DisplayManager::LINE, 2);
  } else {
    display.drawText(200, y + 5, displayStr, DisplayManager::LINE, 2);
  }
  
  // Status/platform (far right)
  if (statusText.length() > 0 && !isWatched) {
    display.drawText(statusX, y + 5, trimToLength(statusText, 6), statusColor, statusSize);
  }
  
  // Draw watched indicator
  if (isWatched) {
    drawWatchedIndicator(index);
  }
}

void UIManager::drawDepartures(const Departure* departures, int count, int pageOffset, int watchedIndex, bool isLoading) {
  display.fillRect(0, DisplayManager::CONTENT_TOP, 
                   DisplayManager::WIDTH, 
                   DisplayManager::CONTENT_BOTTOM - DisplayManager::CONTENT_TOP, 
                   DisplayManager::BG);

  if (isLoading) {
    // Show loading message with spinner
    display.fillRoundRect(10, 70, DisplayManager::WIDTH - 20, 80, 8, DisplayManager::PANEL);
    display.drawText(100, 95, "Nacitam...", DisplayManager::TEXT, 2);
    static int spinnerFrame = 0;
    drawSpinner(200, 105, spinnerFrame++, DisplayManager::LINE);
    if (spinnerFrame > 7) spinnerFrame = 0;
    Serial.println("Loading departures...");
    return;
  }

  if (count == 0) {
    display.fillRoundRect(10, 70, DisplayManager::WIDTH - 20, 80, 8, DisplayManager::PANEL);
    display.drawText(28, 95, "Zadne odjezdy", DisplayManager::TEXT, 2);
    Serial.println("No departures.");
    return;
  }

  int startIdx = pageOffset;
  int endIdx = min(startIdx + ROWS_PER_PAGE, count);
  int rowsToShow = endIdx - startIdx;

  for (int i = 0; i < rowsToShow; i++) {
    int depIdx = startIdx + i;
    bool isWatched = (depIdx == watchedIndex);
    drawDepartureRow(i, departures[depIdx], isWatched);

    Serial.print(depIdx + 1);
    Serial.print(". ");
    Serial.print(departures[depIdx].line);
    Serial.print(" -> ");
    Serial.print(departures[depIdx].headsign);
    Serial.print(" | ");
    Serial.print(departures[depIdx].minutes);
    Serial.print(" min");
    if (isWatched) Serial.print(" [WATCHED]");
    if (departures[depIdx].platform.length()) {
      Serial.print(" | plat ");
      Serial.print(departures[depIdx].platform);
    }
    Serial.print(" s");
    if (departures[depIdx].platform.length()) {
      Serial.print(" | platform ");
      Serial.print(departures[depIdx].platform);
    }
    Serial.println();
  }

  Serial.print("Page: ");
  Serial.print(pageOffset / ROWS_PER_PAGE + 1);
  Serial.print("/");
  Serial.println((count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
}

void UIManager::drawButtons(int currentPage, int totalPages, int departureCount, bool canLoadMore) {
  bool hasPrevPage = currentPage > 1;
  bool hasNextPage = currentPage < totalPages || (departureCount > 0 && canLoadMore);

  const int BUTTON_COUNT = 3;
  const int buttonWidth = DisplayManager::WIDTH / BUTTON_COUNT;

  // Previous page button
  int x1 = 0 * buttonWidth + 2;
  int y1 = DisplayManager::HEIGHT - DisplayManager::FOOTER_H + 4;
  int w1 = buttonWidth - 4;
  int h1 = DisplayManager::FOOTER_H - 8;
  display.fillRoundRect(x1, y1, w1, h1, 6, hasPrevPage ? DisplayManager::BUTTON_ACTIVE : DisplayManager::BUTTON);
  display.drawRoundRect(x1, y1, w1, h1, 6, DisplayManager::TEXT);
  display.drawText(x1 + 40, y1 + 8, "<", DisplayManager::TEXT, 3);

  // Stop switch button
  int x2 = 1 * buttonWidth + 2;
  display.fillRoundRect(x2, y1, w1, h1, 6, DisplayManager::WARN);
  display.drawRoundRect(x2, y1, w1, h1, 6, DisplayManager::TEXT);
  display.drawText(x2 + 20, y1 + 3, "ZMENA", DisplayManager::HEADER, 1);
  display.drawText(x2 + 12, y1 + 18, "STANICE", DisplayManager::HEADER, 2);

  // Next page button
  int x3 = 2 * buttonWidth + 2;
  display.fillRoundRect(x3, y1, w1, h1, 6, hasNextPage ? DisplayManager::BUTTON_ACTIVE : DisplayManager::BUTTON);
  display.drawRoundRect(x3, y1, w1, h1, 6, DisplayManager::TEXT);
  display.drawText(x3 + 40, y1 + 8, ">", DisplayManager::TEXT, 3);

  Serial.println("----------------------------------------");
  Serial.print("Page: ");
  Serial.print(currentPage);
  Serial.print("/");
  Serial.println(totalPages);
  Serial.println("Serial debug: [P]PREV [H]NEXT STOP [N]NEXT");
  Serial.println("Tap row to watch/unwatch");
  Serial.println("========================================");
}

void UIManager::drawModal(const char* title, const char* line1, const char* line2, uint16_t accentColor) {
  // Darken background
  display.fillRect(0, DisplayManager::CONTENT_TOP, 
                   DisplayManager::WIDTH,
                   DisplayManager::CONTENT_BOTTOM - DisplayManager::CONTENT_TOP,
                   DisplayManager::BG);
  
  // Modal background
  int mx = 20;
  int my = 50;
  int mw = DisplayManager::WIDTH - 40;
  int mh = 120;
  display.fillRoundRect(mx, my, mw, mh, 10, DisplayManager::PANEL);
  display.drawRoundRect(mx, my, mw, mh, 10, accentColor);
  
  // Title
  display.drawText(mx + 10, my + 10, title, accentColor, 2);
  
  // Content lines
  if (line1) display.drawText(mx + 10, my + 40, line1, DisplayManager::TEXT, 1);
  if (line2) display.drawText(mx + 10, my + 60, line2, DisplayManager::TEXT, 1);
  
  // Draw Confirm/Cancel buttons
  drawModalButtons();
}

void UIManager::showWatchModal(const Departure& departure) {
  modalShowing = true;
  String title = "Sledovat " + departure.line + "?";
  String line1 = "Smer: " + trimToLength(departure.headsign, 22);
  String line2 = "Za: " + departure.minutes + " min";
  drawModal(title.c_str(), line1.c_str(), line2.c_str(), DisplayManager::OK);
}

void UIManager::showUnwatchModal(const Departure& departure) {
  modalShowing = true;
  String title = "Zrusit sledovani?";
  String line1 = departure.line + " smer " + trimToLength(departure.headsign, 16);
  String line2 = "Potvrd zruseni";
  drawModal(title.c_str(), line1.c_str(), line2.c_str(), DisplayManager::ERR);
}

void UIManager::hideModal() {
  modalShowing = false;
}

void UIManager::flashButton(int buttonIndex) {
  const int BUTTON_COUNT = 3;
  const int buttonWidth = DisplayManager::WIDTH / BUTTON_COUNT;
  int x = buttonIndex * buttonWidth + 2;
  int y = DisplayManager::HEIGHT - DisplayManager::FOOTER_H + 4;
  int w = buttonWidth - 4;
  int h = DisplayManager::FOOTER_H - 8;
  
  // Flash white
  display.fillRoundRect(x, y, w, h, 6, DisplayManager::TEXT);
  delay(100);
  
  // Redraw normally (caller's responsibility to full render if needed)
}

void UIManager::render(const Departure* departures, int count, 
                       const StopConfig& currentStop, int currentPage, int totalPages,
                       bool wifiOk, bool dataOk, int watchedIndex, bool isLoading,
                       bool canLoadMore) {
  clear();
  drawHeader(currentStop, wifiOk, dataOk, isLoading);
  drawStopBar();
  drawDepartures(departures, count, (currentPage - 1) * ROWS_PER_PAGE, watchedIndex, isLoading);
  drawButtons(currentPage, totalPages, count, canLoadMore);
}

void UIManager::drawModalButtons() {
  const int BUTTON_COUNT = 3;
  const int buttonWidth = DisplayManager::WIDTH / BUTTON_COUNT;
  int y = DisplayManager::HEIGHT - DisplayManager::FOOTER_H + 4;
  int w = buttonWidth - 4;
  int h = DisplayManager::FOOTER_H - 8;
  
  // Cancel button on left third
  int x1 = 0 * buttonWidth + 2;
  display.fillRoundRect(x1, y, w, h, 6, DisplayManager::BUTTON);
  display.drawRoundRect(x1, y, w, h, 6, DisplayManager::TEXT);
  display.drawText(x1 + 8, y + 10, "ZRUSIT", DisplayManager::TEXT, 2);
  
  // Confirm button on right third
  int x2 = 2 * buttonWidth + 2;
  display.fillRoundRect(x2, y, w, h, 6, DisplayManager::OK);
  display.drawRoundRect(x2, y, w, h, 6, DisplayManager::TEXT);
  display.drawText(x2 + 2, y + 10, "POTVRDIT", DisplayManager::TEXT, 2);
}

bool UIManager::isModalConfirmButton(int x, int y) const {
  if (!modalShowing) return false;
  
  const int BUTTON_COUNT = 3;
  const int buttonWidth = DisplayManager::WIDTH / BUTTON_COUNT;
  int buttonY = DisplayManager::HEIGHT - DisplayManager::FOOTER_H + 4;
  int buttonW = buttonWidth - 4;
  int buttonH = DisplayManager::FOOTER_H - 8;
  int x2 = 2 * buttonWidth + 2;
  
  return (x >= x2 && x < x2 + buttonW && y >= buttonY && y < buttonY + buttonH);
}

bool UIManager::isModalCancelButton(int x, int y) const {
  if (!modalShowing) return false;
  
  const int BUTTON_COUNT = 3;
  const int buttonWidth = DisplayManager::WIDTH / BUTTON_COUNT;
  int buttonY = DisplayManager::HEIGHT - DisplayManager::FOOTER_H + 4;
  int buttonW = buttonWidth - 4;
  int buttonH = DisplayManager::FOOTER_H - 8;
  int x1 = 0 * buttonWidth + 2;
  
  return (x >= x1 && x < x1 + buttonW && y >= buttonY && y < buttonY + buttonH);
}

// Loading animations
void UIManager::showBootScreen() {
  display.clearScreen();
  
  // Draw title
  display.drawText(82, 80, "Odjezdy MHD", DisplayManager::WARN, 2);
  
  // Draw version/subtitle
  display.drawText(110, 105, "Spoustim...", DisplayManager::DIM, 1);
  
  // Draw animated border
  for (int i = 0; i < 8; i++) {
    int x = 40 + i * 30;
    display.fillRect(x, 140, 20, 6, DisplayManager::LINE);
    delay(50);
  }
}

void UIManager::showWiFiConnecting(int attempt) {
  display.clearScreen();
  
  // Title
  display.drawText(82, 70, "Pripojovani", DisplayManager::WARN, 2);
  display.drawText(84, 95, "k WiFi siti...", DisplayManager::TEXT, 1);
  
  // Spinner animation frame
  static int frame = 0;
  drawSpinner(160, 130, frame++, DisplayManager::LINE);
  if (frame > 7) frame = 0;
  
  // Progress dots
  String dots = "";
  for (int i = 0; i <= (attempt % 4); i++) dots += ".";
  display.drawText(145, 155, dots, DisplayManager::DIM, 2);
  
  // Attempt counter
  String attemptStr = "Pokus " + String(attempt);
  display.drawText(115, 180, attemptStr, DisplayManager::DIM, 1);
}

void UIManager::showStopSwitching(const String& stopName) {
  display.clearScreen();
  
  // Title
  display.drawText(70, 80, "Menim zastavku", DisplayManager::WARN, 2);
  
  // Stop name
  String name = trimToLength(stopName, 18);
  int nameWidth = name.length() * 12;
  int x = (DisplayManager::WIDTH - nameWidth) / 2;
  display.drawText(x, 110, name, DisplayManager::LINE, 2);
  
  // Animated arrow
  static int offset = 0;
  display.drawText(120 + offset, 140, "->", DisplayManager::TEXT, 3);
  offset = (offset + 1) % 20;
}

void UIManager::drawSpinner(int x, int y, int frame, uint16_t color) {
  // Draw 8 lines radiating from center, with fading effect
  for (int i = 0; i < 8; i++) {
    float angle = (i * 45 + frame * 45) * PI / 180;
    int x1 = x + cos(angle) * 10;
    int y1 = y + sin(angle) * 10;
    int x2 = x + cos(angle) * 20;
    int y2 = y + sin(angle) * 20;
    
    // Fade effect - brighter for leading lines
    uint16_t lineColor = (i == 0) ? DisplayManager::WARN : 
                         (i == 1 || i == 7) ? color : DisplayManager::DIM;
    
    display.fillRect(x1, y1, x2-x1+2, y2-y1+2, lineColor);
  }
}

void UIManager::drawProgressBar(int x, int y, int width, int progress, int total, uint16_t color) {
  int height = 10;
  int filled = (width * progress) / total;
  
  // Background
  display.fillRect(x, y, width, height, DisplayManager::PANEL);
  
  // Progress
  if (filled > 0) {
    display.fillRect(x, y, filled, height, color);
  }
  
  // Border
  display.drawRoundRect(x, y, width, height, 3, DisplayManager::TEXT);
}
