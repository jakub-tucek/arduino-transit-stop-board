#pragma once

#include "../config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#ifndef RTC_ENABLED
#define RTC_ENABLED 0
#endif

#if RTC_ENABLED
#include <RTClib.h>
#endif

struct BoardSetupStatus {
  bool wifiConnected;
  bool timeSynced;
  String wifiIP;
  int wifiRSSI;
  String currentTime;
  unsigned long uptimeMs;
};

class BoardSetup {
public:
  BoardSetup(const char* wifiSsid, const char* wifiPassword, const char* timeServerUrl);
  
  bool begin();
  void maintain();
  
  // WiFi
  bool isWiFiConnected() const;
  String getWiFiStatus() const;
  String getWiFiIP() const;
  int getWiFiRSSI() const;
  
  // Time
  bool isTimeSynced() const;
  bool hasValidTime() const;
  String getCurrentTimeStr() const;
  unsigned long getLastTimeSync() const { return lastTimeSyncMs; }
  bool isRtcAvailable() const;
  
  // Healthcheck
  BoardSetupStatus getStatus() const;
  bool healthcheck();  // Returns true if all systems OK
  
  static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

private:
  const char* wifiSsid;
  const char* wifiPassword;
  const char* timeServerUrl;
  
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 20000;
  static constexpr unsigned long RECONNECT_INTERVAL_MS = 30000;
  static constexpr unsigned long TIME_SYNC_INTERVAL_MS = 300000; // 5 min
  static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
  static constexpr time_t MIN_VALID_TIME = 1609459200; // Jan 1, 2021
  
  unsigned long lastReconnectAttempt = 0;
  unsigned long lastTimeSyncMs = 0;
  int reconnectAttempts = 0;
  bool wasConnected = false;
  bool timeSynced = false;
  
  bool tryConnectWiFi();
  void handleWiFiDisconnect();
  bool syncTimeFromServer();
  bool syncTimeFromNTP();
  bool doSyncTime();
  bool isSystemTimeValid() const;
  void applySystemTime(time_t timestamp);
#if RTC_ENABLED
  bool beginRtc();
  bool syncTimeFromRtc();
  void syncRtcFromSystemTime();
  RTC_DS3231 rtc;
  bool rtcAvailable = false;
#endif
};
