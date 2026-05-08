#include "UIManager.h"
#include "BoardSetup.h"
#include <Arduino.h>

namespace {
String getOfflineStatusLine(const String& wifiStatus) {
  if (wifiStatus == "No SSID") return "sit nenalezena";
  if (wifiStatus == "Failed") return "pripojeni selhalo";
  if (wifiStatus == "No WiFi") return "wifi modul neni ready";
  return "cekam na pripojeni";
}

String asciiDisplayText(const String& input) {
  String out = input;

  out.replace("\xC3\xA1", "a"); out.replace("\xC3\xA9", "e"); out.replace("\xC3\xAD", "i");
  out.replace("\xC3\xB3", "o"); out.replace("\xC3\xBA", "u"); out.replace("\xC3\xBD", "y");
  out.replace("\xC4\x8D", "c"); out.replace("\xC4\x8F", "d"); out.replace("\xC4\x9B", "e");
  out.replace("\xC5\x88", "n"); out.replace("\xC5\x99", "r"); out.replace("\xC5\xA1", "s");
  out.replace("\xC5\xA5", "t"); out.replace("\xC5\xAF", "u"); out.replace("\xC5\xBE", "z");
  out.replace("\xC3\x81", "A"); out.replace("\xC3\x89", "E"); out.replace("\xC3\x8D", "I");
  out.replace("\xC3\x93", "O"); out.replace("\xC3\x9A", "U"); out.replace("\xC3\x9D", "Y");
  out.replace("\xC4\x8C", "C"); out.replace("\xC4\x8E", "D"); out.replace("\xC4\x9A", "E");
  out.replace("\xC5\x87", "N"); out.replace("\xC5\x98", "R"); out.replace("\xC5\xA0", "S");
  out.replace("\xC5\xA4", "T"); out.replace("\xC5\xAE", "U"); out.replace("\xC5\xBD", "Z");

  String ascii;
  ascii.reserve(out.length());
  for (size_t i = 0; i < out.length(); i++) {
    unsigned char ch = static_cast<unsigned char>(out[i]);
    if (ch >= 32 && ch <= 126) {
      ascii += static_cast<char>(ch);
    }
  }
  return ascii;
}

int textWidthPx(const String& text, uint8_t size) {
  return text.length() * 6 * size;
}
}

UIManager::UIManager(DisplayManager& disp) : display(disp) {
}

String UIManager::trimToLength(const String& input, size_t maxLen) {
  if (input.length() <= maxLen) return input;
  if (maxLen <= 3) return input.substring(0, maxLen);
  return input.substring(0, maxLen - 3) + "...";
}

String UIManager::shortenHeadsign(const String& input, size_t maxLen) {
  String text = asciiDisplayText(input);
  if (text.length() <= maxLen) return text;
  if (maxLen <= 4) return trimToLength(text, maxLen);

  constexpr int MAX_WORDS = 6;
  String words[MAX_WORDS];
  size_t visibleLengths[MAX_WORDS] = {0};
  size_t minLengths[MAX_WORDS] = {0};
  int wordCount = 0;
  int start = 0;

  while (start < text.length() && wordCount < MAX_WORDS) {
    while (start < text.length() && text[start] == ' ') start++;
    if (start >= text.length()) break;

    int end = text.indexOf(' ', start);
    if (end < 0) end = text.length();
    words[wordCount] = text.substring(start, end);
    visibleLengths[wordCount] = words[wordCount].length();
    minLengths[wordCount] = words[wordCount].length() > 4 ? 4 : words[wordCount].length();
    wordCount++;
    start = end + 1;
  }

  if (wordCount < 2 || start < text.length()) {
    return trimToLength(text, maxLen);
  }

  auto renderedLength = [&]() -> size_t {
    size_t total = wordCount > 0 ? static_cast<size_t>(wordCount - 1) : 0;
    for (int i = 0; i < wordCount; i++) {
      total += visibleLengths[i];
      if (visibleLengths[i] < words[i].length()) {
        total += 1;
      }
    }
    return total;
  };

  while (renderedLength() > maxLen) {
    int longestWord = -1;
    for (int i = 0; i < wordCount; i++) {
      if (visibleLengths[i] > minLengths[i] &&
          (longestWord < 0 || visibleLengths[i] > visibleLengths[longestWord])) {
        longestWord = i;
      }
    }

    if (longestWord < 0) {
      return trimToLength(text, maxLen);
    }

    visibleLengths[longestWord]--;
  }

  String shortened;
  for (int i = 0; i < wordCount; i++) {
    if (i > 0) shortened += ' ';
    if (visibleLengths[i] < words[i].length()) {
      shortened += words[i].substring(0, visibleLengths[i]) + ".";
    } else {
      shortened += words[i];
    }
  }

  return shortened.length() <= maxLen ? shortened : trimToLength(text, maxLen);
}

String UIManager::isoToHHMM(const String& iso) {
  int tPos = iso.indexOf('T');
  if (tPos < 0 || iso.length() < tPos + 6) return "--:--";
  return iso.substring(tPos + 1, tPos + 6);
}

void UIManager::clear() {
  display.clearScreen();
}

void UIManager::drawHeader(const StopConfig& stop, bool wifiOk, bool dataOk, bool isLoading,
                           bool showOfflineBadge, const String& statusText) {
  display.fillRect(0, 0, DisplayManager::WIDTH, DisplayManager::HEADER_H, DisplayManager::HEADER);

  // Time in top right corner
  extern BoardSetup boardSetup;
  String timeStr = boardSetup.getCurrentTimeStr();
  if (boardSetup.hasValidTime()) {
    int timeWidth = timeStr.length() * 12;
    display.drawText(DisplayManager::WIDTH - timeWidth - 10, 7, timeStr, DisplayManager::WARN, 2);
  }

  // Stop name with a small transit icon on the left
  drawBusIcon(6, 7);
  String stopName = trimToLength(asciiDisplayText(stop.label), 14);
  display.drawText(30, 8, stopName, DisplayManager::TEXT, 2);

  if (showOfflineBadge && statusText.length() > 0) {
    display.drawText(30, 22, asciiDisplayText(statusText), DisplayManager::DIM, 1);
  }
  
  // Draw loading indicator next to stop name when loading
  if (isLoading) {
    static unsigned long lastUpdate = 0;
    static int frame = 0;
    int spinnerX = 40 + (stopName.length() * 12);
    if (spinnerX < DisplayManager::WIDTH - 60) {
      unsigned long now = millis();
      if (now - lastUpdate > 120) {
        frame = (frame + 1) % 4;
        lastUpdate = now;
      }

      display.fillRect(spinnerX - 2, 6, 14, 14, DisplayManager::HEADER);
      display.fillCircle(spinnerX + 4, 8 + (frame == 3 ? 4 : frame == 1 ? -4 : 0), 2,
                         frame == 0 || frame == 2 ? DisplayManager::LINE : DisplayManager::WARN);
      display.fillCircle(spinnerX + (frame == 0 ? 8 : frame == 2 ? 0 : 4), 12, 2,
                         frame == 1 || frame == 3 ? DisplayManager::LINE : DisplayManager::WARN);
    }
  }

  // Debug to serial
  Serial.println();
  Serial.println("========================================");
  Serial.print("Stop: ");
  Serial.println(asciiDisplayText(stop.label));
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

void UIManager::drawBusIcon(int x, int y) {
  display.fillRoundRect(x, y, 16, 11, 3, DisplayManager::WARN);
  display.drawRoundRect(x, y, 16, 11, 3, DisplayManager::TEXT);
  display.fillRect(x + 3, y + 3, 4, 3, DisplayManager::HEADER);
  display.fillRect(x + 9, y + 3, 4, 3, DisplayManager::HEADER);
  display.fillRect(x + 2, y + 7, 12, 1, DisplayManager::HEADER);
  display.fillCircle(x + 4, y + 12, 2, DisplayManager::TEXT);
  display.fillCircle(x + 12, y + 12, 2, DisplayManager::TEXT);
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
  
  // Show delay if present (positive delay = late, negative = early)
  if (item.delaySeconds > 60) {
    int delayMins = item.delaySeconds / 60;
    statusText = "+" + String(delayMins);
    statusColor = DisplayManager::ERR;  // Red for delay
    statusSize = 2;
  } else if (item.delaySeconds < -30) {
    int earlyMins = abs(item.delaySeconds) / 60;
    statusText = "-" + String(earlyMins);
    statusColor = DisplayManager::OK;  // Green for early
    statusSize = 2;
  }

  display.fillRect(0, y, DisplayManager::WIDTH, ROW_HEIGHT - 1, rowBg);

  String displayStatus = asciiDisplayText(statusText);

  // Line number
  display.drawText(6, y + 5, trimToLength(item.line, 5), DisplayManager::WARN, 2);

  extern BoardSetup boardSetup;

  // Minutes until departure or departure time for far departures
  String displayStr;
  bool isFarDeparture = false;

  if (!boardSetup.hasValidTime() && item.departureTime.length() > 0) {
    displayStr = item.departureTime;
    isFarDeparture = true;
  } else if (item.minutes == "<1") {
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

  int statusWidth = displayStatus.length() > 0 ? textWidthPx(displayStatus, statusSize) : 0;
  int statusX = DisplayManager::WIDTH - statusWidth - 8;
  int rightEdge = DisplayManager::WIDTH - 8;
  if (isWatched) {
    rightEdge = 286;
  } else if (displayStatus.length() > 0) {
    rightEdge = statusX - 8;
  }

  int timeX = rightEdge - textWidthPx(displayStr, 2);
  int headsignChars = (timeX - 56) / 12;
  if (headsignChars < 6) headsignChars = 6;

  display.drawText(50, y + 4, shortenHeadsign(item.headsign, headsignChars), DisplayManager::TEXT, 2);
  
  // Draw departure info - time in smaller font if it's a far departure
  if (isFarDeparture) {
    display.drawText(timeX, y + 5, displayStr, DisplayManager::LINE, 2);
  } else {
    display.drawText(timeX, y + 5, displayStr, DisplayManager::LINE, 2);
  }
  
  // Status/platform (far right)
  if (displayStatus.length() > 0 && !isWatched) {
    display.drawText(statusX, y + 5, trimToLength(displayStatus, 6), statusColor, statusSize);
  }
  
  // Draw watched indicator
  if (isWatched) {
    drawWatchedIndicator(index);
  }
}

void UIManager::drawDepartures(const Departure* departures, int count, int pageOffset, int watchedIndex,
                               bool isLoading, bool showLoadingOverlay) {
  display.fillRect(0, DisplayManager::CONTENT_TOP, 
                   DisplayManager::WIDTH, 
                   DisplayManager::CONTENT_BOTTOM - DisplayManager::CONTENT_TOP, 
                   DisplayManager::BG);

  extern BoardSetup boardSetup;

  if (count == 0) {
    if (isLoading && showLoadingOverlay) {
      String loadingLabel = "Nacitam odjezdy";
      int textX = (DisplayManager::WIDTH - (loadingLabel.length() * 12)) / 2;
      display.drawText(textX, 95, loadingLabel, DisplayManager::TEXT, 2);
      static int spinnerFrame = 0;
      drawSpinner(textX + (loadingLabel.length() * 12) + 18, 105, spinnerFrame++, DisplayManager::LINE);
      if (spinnerFrame > 7) spinnerFrame = 0;
      Serial.println("Loading departures...");
    } else if (!boardSetup.isWiFiConnected()) {
      String title = "Bez WiFi";
      String detail = getOfflineStatusLine(boardSetup.getWiFiStatus());
      int titleX = (DisplayManager::WIDTH - (title.length() * 12)) / 2;
      int detailX = (DisplayManager::WIDTH - (detail.length() * 6)) / 2;
      display.drawText(titleX, 88, title, DisplayManager::WARN, 2);
      display.drawText(detailX, 114, detail, DisplayManager::DIM, 1);
      Serial.print("WiFi unavailable: ");
      Serial.println(boardSetup.getWiFiStatus());
    } else {
      display.drawText(28, 95, "Zadne odjezdy", DisplayManager::TEXT, 2);
      Serial.println("No departures.");
    }
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
    Serial.print(asciiDisplayText(departures[depIdx].headsign));
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

void UIManager::drawButtons(int currentPage, int totalPages, int departureCount, bool canLoadMore, bool canGoBack) {
  bool hasPrevPage = currentPage > 1 || canGoBack;
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

  String safeTitle = title ? asciiDisplayText(String(title)) : String("");
  String safeLine1 = line1 ? asciiDisplayText(String(line1)) : String("");
  String safeLine2 = line2 ? asciiDisplayText(String(line2)) : String("");
  
  // Title
  display.drawText(mx + 10, my + 10, safeTitle, accentColor, 2);
  
  // Content lines
  if (safeLine1.length() > 0) display.drawText(mx + 10, my + 40, safeLine1, DisplayManager::TEXT, 1);
  if (safeLine2.length() > 0) display.drawText(mx + 10, my + 60, safeLine2, DisplayManager::TEXT, 1);
  
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
                       bool showLoadingOverlay,
                       bool canLoadMore, bool canGoBack,
                       bool showOfflineBadge, const String& statusText) {
  clear();
  drawHeader(currentStop, wifiOk, dataOk, isLoading, showOfflineBadge, statusText);
  drawStopBar();
  drawDepartures(departures, count, (currentPage - 1) * ROWS_PER_PAGE, watchedIndex,
                 isLoading, showLoadingOverlay);
  drawButtons(currentPage, totalPages, count, canLoadMore, canGoBack);
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
  String name = trimToLength(asciiDisplayText(stopName), 18);
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
