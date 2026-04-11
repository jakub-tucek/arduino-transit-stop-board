#include <Arduino.h>
#include <time.h>
#include "../config.h"
#include "Departure.h"
#include "DisplayManager.h"
#include "TransitAPI.h"
#include "UIManager.h"
#include "BoardSetup.h"
#include "NtfyNotifier.h"

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

// Global objects
DisplayManager display;
BoardSetup boardSetup(WIFI_SSID, WIFI_PASSWORD, TIME_SERVER_URL);
TransitAPI api(GOLEMIO_TOKEN);
UIManager ui(display);
NtfyNotifier notifier(NTFY_SERVER, NTFY_TOPIC);

// State
Departure departures[MAX_DEPARTURES];
int departureCount = 0;
int currentStopIndex = 0;
int currentPage = 1;
unsigned long lastFetchMs = 0;
unsigned long lastNtfyUpdateMs = 0;

// Watch state
int watchedDepartureIndex = -1;  // -1 = not watching
int modalPendingRow = -1;        // Row waiting for confirmation
bool modalShowing = false;
unsigned long modalShowTime = 0;

// Track WiFi state to detect reconnections
static bool wasWifiConnected = false;

int getTotalPages() {
  if (departureCount == 0) return 1;
  return (departureCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

bool canLoadMoreDepartures() {
  return api.getMinutesAfter() < MAX_MINUTES_AFTER;
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
            departureCount > 0, watchedAbsIndex, isLoading, canLoadMoreDepartures());
  
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
  if (watchedDepartureIndex >= 0) {
    Serial.print("[MAIN] Stopped watching departure: ");
    Serial.print(departures[watchedDepartureIndex].line);
    Serial.print(" to ");
    Serial.println(departures[watchedDepartureIndex].headsign);
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

void fetchDepartures(bool resetToFirstPage = true) {
  if (!boardSetup.isWiFiConnected()) {
    Serial.println("[MAIN] Cannot fetch - WiFi not connected");
    return;
  }

  String watchedTripId = "";
  if (watchedDepartureIndex >= 0 && watchedDepartureIndex < departureCount) {
    watchedTripId = departures[watchedDepartureIndex].tripId;
  }

  departureCount = api.fetchDepartures(STOPS[currentStopIndex], departures, MAX_DEPARTURES);
  
  if (resetToFirstPage) {
    currentPage = 1;
  } else {
    currentPage = min(currentPage, getTotalPages());
  }
  lastFetchMs = millis();
  
  // If watching, try to refresh the watched departure data
  if (watchedTripId.length() > 0) {
    bool found = false;
    
    // Search for the same trip in new data
    for (int i = 0; i < departureCount; i++) {
      if (departures[i].tripId == watchedTripId) {
        watchedDepartureIndex = i;
        notifier.refreshWatchedDeparture(departures[i]);
        found = true;
        break;
      }
    }
    
    // Trip not found anymore (departed or cancelled)
    if (!found) {
      Serial.println("[MAIN] Watched departure no longer in results");
      stopWatching();
    }
  }
  
  Serial.print("Fetched ");
  Serial.print(departureCount);
  Serial.println(" departures");
}

void loadMoreDepartures() {
  if (!canLoadMoreDepartures()) {
    return;
  }

  int previousPage = currentPage;
  int previousCount = departureCount;
  int nextMinutesAfter = min(api.getMinutesAfter() + LOAD_MORE_MINUTES_STEP, MAX_MINUTES_AFTER);
  api.setMinutesAfter(nextMinutesAfter);

  Serial.print("[MAIN] Expanding fetch window to ");
  Serial.print(nextMinutesAfter);
  Serial.println(" minutes");

  fetchDepartures(false);

  if (departureCount > previousCount) {
    currentPage = min(previousPage + 1, getTotalPages());
  } else {
    currentPage = min(previousPage, getTotalPages());
  }
}

void handleAction(TouchAction action) {
  switch (action) {
    case TouchAction::PREV_PAGE:
      if (currentPage > 1) {
        currentPage--;
      }
      break;
    case TouchAction::NEXT_PAGE:
      if (currentPage < getTotalPages()) {
        currentPage++;
      } else if (departureCount > 0 && canLoadMoreDepartures()) {
        renderScreen(true);
        delay(100);
        loadMoreDepartures();
      }
      break;
    case TouchAction::SWITCH_STOP:
      currentStopIndex = (currentStopIndex + 1) % STOP_COUNT;
      currentPage = 1;
      api.setMinutesAfter(INITIAL_MINUTES_AFTER);
      // Show loading indicator while fetching
      renderScreen(true);
      delay(100);
      fetchDepartures();
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
      if (watchedDepartureIndex >= 0) {
        Serial.print(departures[watchedDepartureIndex].line);
        Serial.print(" to ");
        Serial.println(departures[watchedDepartureIndex].headsign);
      } else {
        Serial.println("None");
      }
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
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== Transit Stop Board ===");
  
  display.begin();
  
  // Show boot animation
  ui.showBootScreen();
  delay(1000);
  
  // Initialize board setup (WiFi + Time)
  boardSetup.begin();
  notifier.begin();
  api.setMinutesAfter(INITIAL_MINUTES_AFTER);
  
  // Wait for WiFi with animation
  int wifiAttempt = 0;
  while (!boardSetup.isWiFiConnected() && wifiAttempt < 30) {
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
  renderScreen(true);
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
    renderScreen(true);
    fetchDepartures();
    renderScreen(false);
  }
  
  // Periodic ntfy notifier update (every minute)
  if (millis() - lastNtfyUpdateMs > NTFY_UPDATE_INTERVAL_MS) {
    if (notifier.isWatching()) {
      time_t now = time(nullptr);
      if (watchedDepartureIndex >= 0 && watchedDepartureIndex < departureCount) {
        notifier.update(now, departures[watchedDepartureIndex].delaySeconds);
      }
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
