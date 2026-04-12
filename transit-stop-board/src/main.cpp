#include <Arduino.h>
#include <time.h>
#include "../config_select.h"
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
Departure fetchBuffer[MAX_DEPARTURES];
Departure reloadBuffer[MAX_DEPARTURES];
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
void renderScreen(bool isLoading = false, bool showLoadingOverlay = true);

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

void clearCurrentDepartureData() {
  departureCount = 0;
  currentDataFromCache = false;
  currentDataSavedAt = 0;
  lastRawDepartureCount = 0;
  watchedDepartureIndex = -1;
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

String getHeaderStatusText() {
  if (boardSetup.isWiFiConnected()) {
    return String("");
  }

  if (currentDataFromCache && currentDataSavedAt > 0 && boardSetup.hasValidTime()) {
    long ageMinutes = static_cast<long>((time(nullptr) - currentDataSavedAt) / 60);
    if (ageMinutes < 1) {
      return "offline cache ted";
    }
    return "offline cache " + String(ageMinutes) + "m";
  }

  if (currentDataFromCache) {
    return "offline cache";
  }

  return departureCount > 0 ? "offline" : "cekam na wifi";
}

int getTotalPages() {
  if (departureCount == 0) return 1;
  return (departureCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

bool canGoToPreviousBatch() {
  return false;
}

bool canLoadMoreDepartures() {
  return lastRawDepartureCount == MAX_DEPARTURES || api.getMinutesAfter() < MAX_MINUTES_AFTER;
}

int targetVisibleCountForPage(int page) {
  return min(page * ROWS_PER_PAGE, MAX_DEPARTURES);
}

bool isCurrentPageIncomplete() {
  if (departureCount == 0) return false;
  int pageStart = (currentPage - 1) * ROWS_PER_PAGE;
  return currentPage == getTotalPages() && (departureCount - pageStart) < ROWS_PER_PAGE;
}

void finalizeLiveDepartureState(bool resetToFirstPage) {
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
  Serial.print(" departures (next offset ");
  Serial.print(currentApiOffset);
  Serial.print(", raw ");
  Serial.print(lastRawDepartureCount);
  Serial.println(")");
}

bool appendVisibleDeparturesUntil(int minVisibleCount) {
  if (!boardSetup.isWiFiConnected()) {
    lastFetchMs = millis();
    Serial.println("[MAIN] Cannot fetch - WiFi not connected");
    return false;
  }

  bool requestSucceeded = false;
  bool appendedAny = false;
  minVisibleCount = min(minVisibleCount, MAX_DEPARTURES);

  while (departureCount < minVisibleCount && departureCount < MAX_DEPARTURES) {
    int requestLimit = MAX_DEPARTURES - departureCount;
    int rawCount = -1;
    int fetchedCount = api.fetchDepartures(STOPS[currentStopIndex], fetchBuffer, requestLimit,
                                           currentApiOffset, &rawCount);
    if (rawCount < 0) {
      lastFetchMs = millis();
      Serial.println("[MAIN] Fetch failed while appending departures");
      break;
    }

    requestSucceeded = true;
    lastRawDepartureCount = rawCount;

    for (int i = 0; i < fetchedCount && departureCount < MAX_DEPARTURES; i++) {
      departures[departureCount++] = fetchBuffer[i];
      appendedAny = true;
    }

    currentApiOffset += rawCount;

    if (departureCount >= minVisibleCount || departureCount >= MAX_DEPARTURES) {
      break;
    }

    if (rawCount == requestLimit) {
      continue;
    }

    if (api.getMinutesAfter() >= MAX_MINUTES_AFTER) {
      break;
    }

    int nextMinutesAfter = min(api.getMinutesAfter() + LOAD_MORE_MINUTES_STEP, MAX_MINUTES_AFTER);
    api.setMinutesAfter(nextMinutesAfter);
    Serial.print("[MAIN] Expanding fetch window to ");
    Serial.print(nextMinutesAfter);
    Serial.println(" minutes");
  }

  if (requestSucceeded) {
    finalizeLiveDepartureState(false);
  }

  return appendedAny;
}

bool reloadVisibleDepartures(bool resetToFirstPage = true, int minVisibleCount = ROWS_PER_PAGE) {
  if (!boardSetup.isWiFiConnected()) {
    lastFetchMs = millis();
    Serial.println("[MAIN] Cannot fetch - WiFi not connected");
    return false;
  }

  int nextDepartureCount = 0;
  int nextApiOffset = 0;
  int nextLastRawCount = 0;
  bool requestSucceeded = false;
  minVisibleCount = min(minVisibleCount, MAX_DEPARTURES);

  while (nextDepartureCount < minVisibleCount && nextDepartureCount < MAX_DEPARTURES) {
    int requestLimit = MAX_DEPARTURES - nextDepartureCount;
    int rawCount = -1;
    int fetchedCount = api.fetchDepartures(STOPS[currentStopIndex], fetchBuffer, requestLimit,
                                           nextApiOffset, &rawCount);
    if (rawCount < 0) {
      lastFetchMs = millis();
      Serial.println("[MAIN] Fetch failed, keeping current departures");
      return false;
    }

    requestSucceeded = true;
    nextLastRawCount = rawCount;

    for (int i = 0; i < fetchedCount && nextDepartureCount < MAX_DEPARTURES; i++) {
      reloadBuffer[nextDepartureCount++] = fetchBuffer[i];
    }

    nextApiOffset += rawCount;

    if (nextDepartureCount >= minVisibleCount || nextDepartureCount >= MAX_DEPARTURES) {
      break;
    }

    if (rawCount == requestLimit) {
      continue;
    }

    if (api.getMinutesAfter() >= MAX_MINUTES_AFTER) {
      break;
    }

    int nextMinutesAfter = min(api.getMinutesAfter() + LOAD_MORE_MINUTES_STEP, MAX_MINUTES_AFTER);
    api.setMinutesAfter(nextMinutesAfter);
    Serial.print("[MAIN] Expanding fetch window to ");
    Serial.print(nextMinutesAfter);
    Serial.println(" minutes");
  }

  if (!requestSucceeded) {
    return false;
  }

  departureCount = nextDepartureCount;
  for (int i = 0; i < departureCount; i++) {
    departures[i] = reloadBuffer[i];
  }
  currentApiOffset = nextApiOffset;
  lastRawDepartureCount = nextLastRawCount;
  finalizeLiveDepartureState(resetToFirstPage);
  return true;
}

void prefetchCurrentPageIfNeeded() {
  if (!boardSetup.isWiFiConnected() || !canLoadMoreDepartures() || !isCurrentPageIncomplete()) {
    return;
  }

  appendVisibleDeparturesUntil(targetVisibleCountForPage(currentPage));
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

void renderScreen(bool isLoading, bool showLoadingOverlay) {
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
            departureCount > 0, watchedAbsIndex, isLoading, showLoadingOverlay,
            canLoadMoreDepartures(), canGoToPreviousBatch(),
            !boardSetup.isWiFiConnected(), getHeaderStatusText());
  
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
  lastFetchMs = millis();
  api.setMinutesAfter(INITIAL_MINUTES_AFTER);
  int minVisibleCount = resetToFirstPage ? ROWS_PER_PAGE : targetVisibleCountForPage(currentPage);
  return reloadVisibleDepartures(resetToFirstPage, minVisibleCount);
}

bool loadPreviousDepartureBatch() {
  return false;
}

bool loadMoreDepartures() {
  if (!canLoadMoreDepartures()) {
    return false;
  }

  return appendVisibleDeparturesUntil(targetVisibleCountForPage(currentPage + 1));
}

void resetAndFetchDepartures() {
  resetDeparturePaging();
  fetchDepartures();
  prefetchCurrentPageIfNeeded();
}

void switchToNextStop() {
  currentStopIndex = (currentStopIndex + 1) % STOP_COUNT;
  currentPage = 1;
  api.setMinutesAfter(INITIAL_MINUTES_AFTER);
  resetDeparturePaging();

  bool hasCachedData = loadCachedDeparturesForCurrentStop();
  if (!hasCachedData) {
    clearCurrentDepartureData();
  }

  if (boardSetup.isWiFiConnected()) {
    renderScreen(true, !hasCachedData);
    delay(100);
    if (fetchDepartures()) {
      return;
    }
  }
}

void handlePrevPageAction() {
  if (currentPage > 1) {
    currentPage--;
    return;
  }
}

void handleNextPageAction() {
  if (currentPage < getTotalPages()) {
    currentPage++;
    prefetchCurrentPageIfNeeded();
    return;
  }

  if (departureCount == 0 || !canLoadMoreDepartures()) {
    return;
  }

  renderScreen(true, false);
  if (loadMoreDepartures() && currentPage < getTotalPages()) {
    currentPage++;
    prefetchCurrentPageIfNeeded();
  }
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
  if (boardSetup.isWiFiConnected()) {
    renderScreen(true, departureCount == 0);
    fetchDepartures();
  }
  renderScreen(false);
}

void loop() {
  // Maintain WiFi and time sync
  boardSetup.maintain();
  
  bool isWifiConnected = boardSetup.isWiFiConnected();

  if (!isWifiConnected && wasWifiConnected) {
    renderScreen(false);
  }
  
  // Handle WiFi state change (reconnect detection)
  if (isWifiConnected && !wasWifiConnected) {
    // WiFi just reconnected
    Serial.println("[MAIN] WiFi reconnected");
    notifier.notifyWiFiReconnected();
    
    // Fetch data if time is synced
    if (boardSetup.isTimeSynced()) {
      Serial.println("[MAIN] WiFi reconnected, fetching data...");
      bool hadData = departureCount > 0;
      if (!hadData) {
        renderScreen(true, true);
      }
      if (fetchDepartures(false) || !hadData) {
        renderScreen(false);
      }
    }
  }
  wasWifiConnected = isWifiConnected;

  // Periodic data refresh
  if (millis() - lastFetchMs > FETCH_INTERVAL_MS) {
    if (isWifiConnected) {
      bool hadData = departureCount > 0;
      if (!hadData) {
        renderScreen(true, true);
      }
      if (fetchDepartures(false) || !hadData) {
        renderScreen(false);
      }
    } else {
      lastFetchMs = millis();
      if (currentDataFromCache) {
        applyDepartureDataSource(true, currentDataSavedAt);
      }
      renderScreen(false);
    }
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
