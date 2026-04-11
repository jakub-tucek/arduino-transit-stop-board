#include <Arduino.h>
#include <time.h>
#include "../config.h"
#include "Departure.h"
#include "DisplayManager.h"
#include "TransitAPI.h"
#include "UIManager.h"
#include "BoardSetup.h"
#include "NtfyNotifier.h"
#include "OfflineCache.h"

#ifndef NTFY_SERVER
#define NTFY_SERVER ""
#endif

#ifndef NTFY_TOPIC
#define NTFY_TOPIC ""
#endif

// Use config from config.h
static const int MAX_DEPARTURES = 96;
static const unsigned long FETCH_INTERVAL_MS = 60000UL; // 1 minute
static const unsigned long NTFY_UPDATE_INTERVAL_MS = 60000UL; // 1 minute
static const int ROWS_PER_PAGE = 7;
static const int INITIAL_MINUTES_AFTER = 180;
static const int LOAD_MORE_MINUTES_STEP = 180;
static const int MAX_MINUTES_AFTER = 4320;
static const int MAX_BATCH_HISTORY = 24;

// Global objects
DisplayManager display;
BoardSetup boardSetup(WIFI_SSID, WIFI_PASSWORD, TIME_SERVER_URL);
TransitAPI api(GOLEMIO_TOKEN);
UIManager ui(display);
NtfyNotifier notifier(NTFY_SERVER, NTFY_TOPIC);
OfflineCache offlineCache;

// State
Departure departures[MAX_DEPARTURES];
int departureCount = 0;
int currentStopIndex = 0;
int currentPage = 1;
int currentApiOffset = 0;
int lastRawDepartureCount = 0;
unsigned long lastFetchMs = 0;
unsigned long lastNtfyUpdateMs = 0;
int batchOffsetHistory[MAX_BATCH_HISTORY];
int batchHistoryCount = 0;

// Watch state
int watchedDepartureIndex = -1;  // -1 = not watching
int modalPendingRow = -1;        // Row waiting for confirmation
bool modalShowing = false;
unsigned long modalShowTime = 0;

// Track WiFi state to detect reconnections
static bool wasWifiConnected = false;
static bool currentDataFromCache = false;
static time_t currentDataSavedAt = 0;

void syncWatchedDepartureToCurrentBatch();

int normalizeDeparturesForCurrentClock(Departure* list, int count) {
  if (!boardSetup.hasValidTime()) {
    return count;
  }

  time_t now = time(nullptr);
  int writeIndex = 0;
  for (int i = 0; i < count; i++) {
    Departure updated = list[i];
    if (updated.departureEpoch > 0) {
      long diffSeconds = static_cast<long>(updated.departureEpoch - now);
      if (diffSeconds < -60) {
        continue;
      }

      int diffMinutes = static_cast<int>(diffSeconds / 60);
      updated.minutes = diffMinutes < 1 ? "<1" : String(diffMinutes);

      struct tm timeInfo;
      if (localtime_r(&updated.departureEpoch, &timeInfo)) {
        char buffer[6];
        strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
        updated.departureTime = String(buffer);
      }
    }

    list[writeIndex++] = updated;
  }

  return writeIndex;
}

void applyDepartureDataSource(bool fromCache, time_t savedAt) {
  currentDataFromCache = fromCache;
  currentDataSavedAt = savedAt;
  departureCount = normalizeDeparturesForCurrentClock(departures, departureCount);
}

bool loadCachedDeparturesForCurrentStop() {
  int cachedCount = 0;
  time_t savedAt = 0;
  if (!offlineCache.loadDepartures(STOPS[currentStopIndex], departures, MAX_DEPARTURES, cachedCount, savedAt)) {
    return false;
  }

  departureCount = cachedCount;
  currentPage = 1;
  lastRawDepartureCount = departureCount;
  applyDepartureDataSource(true, savedAt);
  syncWatchedDepartureToCurrentBatch();
  Serial.println("[MAIN] Loaded departures from offline cache");
  return true;
}

int getTotalPages() {
  if (departureCount == 0) return 1;
  return (departureCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

bool canGoToPreviousBatch() {
  return batchHistoryCount > 0;
}

bool canLoadMoreDepartures() {
  return lastRawDepartureCount == MAX_DEPARTURES || api.getMinutesAfter() < MAX_MINUTES_AFTER;
}

void resetDeparturePaging() {
  currentApiOffset = 0;
  lastRawDepartureCount = 0;
  batchHistoryCount = 0;
}

void pushBatchOffset(int offset) {
  if (batchHistoryCount >= MAX_BATCH_HISTORY) {
    for (int i = 1; i < MAX_BATCH_HISTORY; i++) {
      batchOffsetHistory[i - 1] = batchOffsetHistory[i];
    }
    batchHistoryCount = MAX_BATCH_HISTORY - 1;
  }
  batchOffsetHistory[batchHistoryCount++] = offset;
}

bool popBatchOffset(int& offset) {
  if (batchHistoryCount == 0) {
    return false;
  }
  offset = batchOffsetHistory[--batchHistoryCount];
  return true;
}

void syncWatchedDepartureToCurrentBatch() {
  watchedDepartureIndex = -1;

  if (!notifier.isWatching()) {
    return;
  }

  const String& watchedTripId = notifier.getWatchedConnection().tripId;
  if (watchedTripId.length() == 0) {
    return;
  }

  for (int i = 0; i < departureCount; i++) {
    if (departures[i].tripId == watchedTripId) {
      watchedDepartureIndex = i;
      notifier.refreshWatchedDeparture(departures[i]);
      return;
    }
  }
}

void renderScreen(bool isLoading = false) {
  // Calculate absolute watched index based on page
  int watchedAbsIndex = -1;
  if (watchedDepartureIndex >= 0) {
    int pageStart = (currentPage - 1) * ROWS_PER_PAGE;
    int pageEnd = pageStart + ROWS_PER_PAGE;
    if (watchedDepartureIndex >= pageStart && watchedDepartureIndex < pageEnd) {
      watchedAbsIndex = watchedDepartureIndex;
    }
  }
  
  ui.render(departures, departureCount, STOPS[currentStopIndex],
            currentPage, getTotalPages(), boardSetup.isWiFiConnected(), 
            departureCount > 0, watchedAbsIndex, isLoading,
            canLoadMoreDepartures(), canGoToPreviousBatch());
  
  // Show modal if pending
  if (modalShowing && modalPendingRow >= 0) {
    int absIndex = (currentPage - 1) * ROWS_PER_PAGE + modalPendingRow;
    if (absIndex >= 0 && absIndex < departureCount) {
      if (watchedDepartureIndex == absIndex) {
        // Already watching - show unwatch modal
        ui.showUnwatchModal(departures[absIndex]);
      } else {
        // Not watching - show watch modal
        ui.showWatchModal(departures[absIndex]);
      }
    }
  }
}

// Calculate departure time_t from Departure minutes field
time_t calculateDepartureTime(const Departure& dep) {
  time_t now = time(nullptr);
  int minutes = 0;
  
  // Parse minutes from departure
  if (dep.minutes == "<1") {
    minutes = 0;
  } else {
    minutes = dep.minutes.toInt();
  }
  
  // Add delay
  minutes += dep.delaySeconds / 60;
  
  return now + (minutes * 60);
}

void startWatching(int departureIndex) {
  if (departureIndex < 0 || departureIndex >= departureCount) return;
  
  Departure& dep = departures[departureIndex];
  time_t depTime = calculateDepartureTime(dep);
  
  if (notifier.startWatching(dep, depTime, STOPS[currentStopIndex].label)) {
    watchedDepartureIndex = departureIndex;
    Serial.print("[MAIN] Started watching departure: ");
    Serial.print(dep.line);
    Serial.print(" to ");
    Serial.println(dep.headsign);
  }
}

void stopWatching() {
  if (watchedDepartureIndex >= 0 && watchedDepartureIndex < departureCount) {
    Serial.print("[MAIN] Stopped watching departure: ");
    Serial.print(departures[watchedDepartureIndex].line);
    Serial.print(" to ");
    Serial.println(departures[watchedDepartureIndex].headsign);
  } else if (notifier.isWatching()) {
    const WatchedConnection& watched = notifier.getWatchedConnection();
    Serial.print("[MAIN] Stopped watching departure: ");
    Serial.print(watched.line);
    Serial.print(" to ");
    Serial.println(watched.headsign);
  }
  notifier.stopWatching();
  watchedDepartureIndex = -1;
}

void confirmWatchAction() {
  if (modalPendingRow < 0) return;
  
  int absIndex = (currentPage - 1) * ROWS_PER_PAGE + modalPendingRow;
  if (absIndex < 0 || absIndex >= departureCount) return;
  
  Serial.print("[MAIN] Confirming action for row ");
  Serial.print(modalPendingRow);
  Serial.print(" (abs index ");
  Serial.print(absIndex);
  Serial.println(")");
  
  if (watchedDepartureIndex == absIndex) {
    // Stop watching
    stopWatching();
  } else {
    // Start watching (stop previous if any)
    if (watchedDepartureIndex >= 0) {
      stopWatching();
    }
    startWatching(absIndex);
  }
  modalShowing = false;
  modalPendingRow = -1;
  ui.setModalShowing(false);
  renderScreen();
}

void handleWatchSelect(int rowIndex) {
  if (!notifier.isEnabled()) {
    Serial.println("[MAIN] Watch selection ignored: ntfy disabled");
    return;
  }

  int absIndex = (currentPage - 1) * ROWS_PER_PAGE + rowIndex;
  
  if (absIndex < 0 || absIndex >= departureCount) return;
  
  Serial.print("[MAIN] Selected row ");
  Serial.print(rowIndex);
  Serial.print(" (abs index ");
  Serial.print(absIndex);
  Serial.println(")");
  
  // Show modal for confirmation
  modalPendingRow = rowIndex;
  modalShowing = true;
  modalShowTime = millis();
  renderScreen();
}

bool fetchDepartures(bool resetToFirstPage = true) {
  if (!boardSetup.isWiFiConnected()) {
    Serial.println("[MAIN] Cannot fetch - WiFi not connected");
    return false;
  }

  int rawCount = -1;
  departureCount = api.fetchDepartures(STOPS[currentStopIndex], departures, MAX_DEPARTURES,
                                       currentApiOffset, &rawCount);
  if (rawCount < 0) {
    Serial.println("[MAIN] Fetch failed, keeping current departures");
    return false;
  }
  lastRawDepartureCount = rawCount;
  
  if (resetToFirstPage) {
    currentPage = 1;
  } else {
    currentPage = min(currentPage, getTotalPages());
  }
  lastFetchMs = millis();
  applyDepartureDataSource(false, boardSetup.hasValidTime() ? time(nullptr) : 0);
  offlineCache.saveDepartures(STOPS[currentStopIndex], departures, departureCount, currentDataSavedAt);

  syncWatchedDepartureToCurrentBatch();
  
  Serial.print("Fetched ");
  Serial.print(departureCount);
  Serial.print(" departures (raw ");
  Serial.print(lastRawDepartureCount);
  Serial.print(", offset ");
  Serial.print(currentApiOffset);
  Serial.println(")");
  return true;
}

bool loadPreviousDepartureBatch() {
  int previousOffset = 0;
  if (!popBatchOffset(previousOffset)) {
    return false;
  }

  currentApiOffset = previousOffset;
  if (!fetchDepartures()) {
    return false;
  }
  currentPage = getTotalPages();
  return departureCount > 0;
}

bool loadMoreDepartures() {
  if (!canLoadMoreDepartures()) {
    return false;
  }

  const int previousOffset = currentApiOffset;
  int previousPage = currentPage;
  const int previousMinutesAfter = api.getMinutesAfter();
  int nextOffset = currentApiOffset + lastRawDepartureCount;
  bool expandedWindow = false;

  while (true) {
    if (nextOffset > previousOffset || expandedWindow) {
      currentApiOffset = nextOffset;
      if (!fetchDepartures()) {
        break;
      }
      expandedWindow = false;

      if (departureCount > 0) {
        pushBatchOffset(previousOffset);
        return true;
      }

      if (lastRawDepartureCount == MAX_DEPARTURES) {
        nextOffset = currentApiOffset + lastRawDepartureCount;
        continue;
      }
    }

    if (api.getMinutesAfter() >= MAX_MINUTES_AFTER) {
      break;
    }

    int nextMinutesAfter = min(api.getMinutesAfter() + LOAD_MORE_MINUTES_STEP, MAX_MINUTES_AFTER);
    api.setMinutesAfter(nextMinutesAfter);
    expandedWindow = true;

    Serial.print("[MAIN] Expanding fetch window to ");
    Serial.print(nextMinutesAfter);
    Serial.println(" minutes");
  }

  api.setMinutesAfter(previousMinutesAfter);
  currentApiOffset = previousOffset;
  fetchDepartures(false);
  currentPage = previousPage;
  return false;
}

void resetAndFetchDepartures() {
  resetDeparturePaging();
  fetchDepartures();
}

void switchToNextStop() {
  currentStopIndex = (currentStopIndex + 1) % STOP_COUNT;
  currentPage = 1;
  api.setMinutesAfter(INITIAL_MINUTES_AFTER);
  resetDeparturePaging();

  if (boardSetup.isWiFiConnected()) {
    renderScreen(true);
    delay(100);
    if (fetchDepartures()) {
      return;
    }
  }

  if (!loadCachedDeparturesForCurrentStop()) {
    departureCount = 0;
    currentDataFromCache = false;
    currentDataSavedAt = 0;
    lastRawDepartureCount = 0;
  }
}

void handlePrevPageAction() {
  if (currentPage > 1) {
    currentPage--;
    return;
  }

  if (!canGoToPreviousBatch()) {
    return;
  }

  renderScreen(true);
  delay(100);
  loadPreviousDepartureBatch();
}

void handleNextPageAction() {
  if (currentPage < getTotalPages()) {
    currentPage++;
    return;
  }

  if (departureCount == 0 || !canLoadMoreDepartures()) {
    return;
  }

  renderScreen(true);
  delay(100);
  loadMoreDepartures();
}

void handleAction(TouchAction action) {
  switch (action) {
    case TouchAction::PREV_PAGE:
      handlePrevPageAction();
      break;
    case TouchAction::NEXT_PAGE:
      handleNextPageAction();
      break;
    case TouchAction::SWITCH_STOP:
      switchToNextStop();
      break;
    case TouchAction::WATCH_SELECT:
      // Handled separately
      break;
    default:
      return;
  }
  
  // Re-render after action
  renderScreen();
}

void handleTouch() {
  TouchAction action = display.pollTouch();
  if (action == TouchAction::NONE) return;
  
  Serial.print("[TOUCH] Action received: ");
  Serial.print((int)action);
  Serial.print(" modalShowing: ");
  Serial.println(modalShowing);
  
  // Handle modal mode
  if (modalShowing) {
    // Debounce
    if (millis() - modalShowTime <= 300) {
      Serial.println("[TOUCH] Debouncing modal touch");
      delay(200);
      return;
    }
    
    Serial.println("[TOUCH] Processing modal button");
    
    // Check if Confirm (right side) or Cancel (left side)
    if (action == TouchAction::NEXT_PAGE) {
      Serial.println("[TOUCH] CONFIRM button detected");
      confirmWatchAction();
    } else if (action == TouchAction::PREV_PAGE) {
      Serial.println("[TOUCH] CANCEL button detected");
      modalShowing = false;
      modalPendingRow = -1;
      ui.setModalShowing(false);
      renderScreen();
    } else {
      // Any other touch cancels modal
      Serial.println("[TOUCH] Modal dismissed");
      modalShowing = false;
      modalPendingRow = -1;
      ui.setModalShowing(false);
      renderScreen();
    }
    delay(200);
    return;
  }
  
  // Handle row selection (WATCH_SELECT with row info)
  if (action == TouchAction::WATCH_SELECT) {
    int rowIndex = display.getLastTouchedRow();
    if (rowIndex >= 0) {
      handleWatchSelect(rowIndex);
    }
    delay(200);
    return;
  }
  
  // Flash button for visual feedback
  if (action == TouchAction::PREV_PAGE) {
    ui.flashButton(0);
  } else if (action == TouchAction::SWITCH_STOP) {
    ui.flashButton(1);
  } else if (action == TouchAction::NEXT_PAGE) {
    ui.flashButton(2);
  }
  
  handleAction(action);
  delay(200); // Debounce
}

void checkSerialInput() {
  if (!Serial.available()) return;
  
  char c = Serial.read();
  switch (c) {
    case 'p': case 'P': handleAction(TouchAction::PREV_PAGE); renderScreen(); break;
    case 'n': case 'N': handleAction(TouchAction::NEXT_PAGE); renderScreen(); break;
    case 'h': case 'H': handleAction(TouchAction::SWITCH_STOP); renderScreen(); break;
    case 's': case 'S': {
      // Status command
      BoardSetupStatus status = boardSetup.getStatus();
      Serial.println("\n=== Status ===");
      Serial.print("WiFi: ");
      Serial.println(status.wifiConnected ? "Connected" : "Disconnected");
      Serial.print("IP: ");
      Serial.println(status.wifiIP);
      Serial.print("RSSI: ");
      Serial.print(status.wifiRSSI);
      Serial.println(" dBm");
      Serial.print("Time synced: ");
      Serial.println(status.timeSynced ? "Yes" : "No");
      Serial.print("Current time: ");
      Serial.println(status.currentTime);
      Serial.print("Health: ");
      Serial.println(boardSetup.healthcheck() ? "OK" : "Issues");
      Serial.print("Watching: ");
      if (notifier.isWatching()) {
        const WatchedConnection& watched = notifier.getWatchedConnection();
        if (watchedDepartureIndex >= 0 && watchedDepartureIndex < departureCount) {
          Serial.print(departures[watchedDepartureIndex].line);
          Serial.print(" to ");
          Serial.println(departures[watchedDepartureIndex].headsign);
        } else {
          Serial.print(watched.line);
          Serial.print(" to ");
          Serial.println(watched.headsign);
        }
      } else {
        Serial.println("None");
      }
      Serial.print("Offset: ");
      Serial.println(currentApiOffset);
      Serial.print("Minutes after: ");
      Serial.println(api.getMinutesAfter());
      Serial.println("==============\n");
      break;
    }
    case 'w': case 'W': {
      // Debug: watch first departure
      if (notifier.isEnabled() && departureCount > 0) {
        startWatching(0);
        renderScreen();
      }
      break;
    }
    case 'c': case 'C': {
      // Debug: cancel watching
      stopWatching();
      renderScreen();
      break;
    }
    case 'r': case 'R': {
      api.setMinutesAfter(INITIAL_MINUTES_AFTER);
      resetAndFetchDepartures();
      renderScreen();
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== Transit Stop Board ===");
  
  display.begin();
  offlineCache.begin();
  
  // Show boot animation
  ui.showBootScreen();
  delay(1000);

  loadCachedDeparturesForCurrentStop();
  if (departureCount > 0) {
    renderScreen(false);
  }
  
  // Initialize board setup (WiFi + Time)
  boardSetup.begin();
  notifier.begin();
  api.setMinutesAfter(INITIAL_MINUTES_AFTER);
  resetDeparturePaging();
  
  // Wait for WiFi with animation
  int wifiAttempt = 0;
  while (!boardSetup.isWiFiConnected() && wifiAttempt < 30 && departureCount == 0) {
    ui.showWiFiConnecting(wifiAttempt + 1);
    delay(500);
    wifiAttempt++;
  }
  
  // Debug time sync status
  Serial.print("[MAIN] Time synced: ");
  Serial.println(boardSetup.isTimeSynced() ? "YES" : "NO");
  if (boardSetup.isTimeSynced()) {
    Serial.print("[MAIN] Current time: ");
    Serial.println(boardSetup.getCurrentTimeStr());
  } else {
    Serial.println("[MAIN] Time sync failed - API may not work correctly");
  }
  
  // Show loading and fetch data
  renderScreen(boardSetup.isWiFiConnected());
  fetchDepartures();
  renderScreen(false);
}

void loop() {
  // Maintain WiFi and time sync
  boardSetup.maintain();
  
  bool isWifiConnected = boardSetup.isWiFiConnected();
  
  // Handle WiFi state change (reconnect detection)
  if (isWifiConnected && !wasWifiConnected) {
    // WiFi just reconnected
    Serial.println("[MAIN] WiFi reconnected");
    notifier.notifyWiFiReconnected();
    
    // Fetch data if time is synced
    if (boardSetup.isTimeSynced()) {
      Serial.println("[MAIN] WiFi reconnected, fetching data...");
      renderScreen(true);
      fetchDepartures();
      renderScreen(false);
    }
  }
  wasWifiConnected = isWifiConnected;

  // Periodic data refresh
  if (millis() - lastFetchMs > FETCH_INTERVAL_MS) {
    renderScreen(boardSetup.isWiFiConnected());
    fetchDepartures();
    renderScreen(false);
  }
  
  // Periodic ntfy notifier update (every minute)
  if (millis() - lastNtfyUpdateMs > NTFY_UPDATE_INTERVAL_MS) {
    if (notifier.isWatching()) {
      time_t now = time(nullptr);
      int currentDelaySeconds = notifier.getWatchedConnection().lastDelaySeconds;
      if (watchedDepartureIndex >= 0 && watchedDepartureIndex < departureCount) {
        currentDelaySeconds = departures[watchedDepartureIndex].delaySeconds;
      }
      notifier.update(now, currentDelaySeconds);
    }
    lastNtfyUpdateMs = millis();
  }

  // Handle touch input
  handleTouch();

  // Handle serial input
  checkSerialInput();
  
  // Auto-dismiss modal after 5 seconds
  if (modalShowing && millis() - modalShowTime > 5000) {
    modalShowing = false;
    modalPendingRow = -1;
    ui.setModalShowing(false);
    renderScreen();
  }

  delay(30);
}
