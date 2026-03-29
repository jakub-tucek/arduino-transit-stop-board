#pragma once

#include "../config.h"
#include "DisplayManager.h"
#include "Departure.h"

class UIManager {
public:
  UIManager(DisplayManager& display);
  
  void render(const Departure* departures, int count, 
              const StopConfig& currentStop, int currentPage, int totalPages,
              bool wifiOk, bool dataOk, int watchedIndex = -1, bool isLoading = false);
  
  void clear();
  void flashButton(int buttonIndex);
  
  // Loading animations
  void showBootScreen();
  void showWiFiConnecting(int attempt);
  void showStopSwitching(const String& stopName);
  void drawSpinner(int x, int y, int frame, uint16_t color);
  void drawProgressBar(int x, int y, int width, int progress, int total, uint16_t color);
  
  // Modal dialogs
  void showWatchModal(const Departure& departure);
  void showUnwatchModal(const Departure& departure);
  void hideModal();
  bool isModalShowing() const { return modalShowing; }
  void setModalShowing(bool showing) { modalShowing = showing; }
  
  // Modal button handling
  void drawModalButtons();
  bool isModalConfirmButton(int x, int y) const;
  bool isModalCancelButton(int x, int y) const;
  
private:
  DisplayManager& display;
  
  static constexpr int ROW_HEIGHT = 23;
  static constexpr int ROWS_PER_PAGE = 7;
  
  bool modalShowing = false;
  
  void drawHeader(const StopConfig& stop, bool wifiOk, bool dataOk, bool isLoading = false);
  void drawStopBar();
  void drawDepartures(const Departure* departures, int count, int pageOffset, int watchedIndex, bool isLoading = false);
  void drawDepartureRow(int index, const Departure& item, bool isWatched);
  void drawButtons(int currentPage, int totalPages, int departureCount);
  void drawWatchedIndicator(int row);
  void drawModal(const char* title, const char* line1, const char* line2, uint16_t accentColor);

  String trimToLength(const String& input, size_t maxLen);
  String isoToHHMM(const String& iso);
};