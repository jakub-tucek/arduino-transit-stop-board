#include "BoardSetup.h"

#if RTC_ENABLED
#include <Wire.h>
#endif

namespace {
constexpr const char* kDefaultTimezoneRule = "CET-1CEST,M3.5.0/2,M10.5.0/3";

void configureLocalTimezone() {
  setenv("TZ", kDefaultTimezoneRule, 1);
  tzset();
}
}

BoardSetup::BoardSetup(const char* wifiSsid, const char* wifiPassword, const char* timeServerUrl)
  : wifiSsid(wifiSsid), wifiPassword(wifiPassword), timeServerUrl(timeServerUrl) {
}

bool BoardSetup::begin() {
  configureLocalTimezone();
  Serial.println("[SETUP] Initializing...");

#if RTC_ENABLED
  beginRtc();
  if (!isSystemTimeValid()) {
    syncTimeFromRtc();
  }
#endif
  
  // Start WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.onEvent(onWiFiEvent);
  
  bool wifiOk = tryConnectWiFi();
  
  if (wifiOk) {
    // Try to sync time
    doSyncTime();
  }
  
  return wifiOk;
}

void BoardSetup::maintain() {
  // Maintain WiFi
  bool currentlyConnected = isWiFiConnected();
  
  if (currentlyConnected) {
    // WiFi just connected - trigger immediate time sync
    if (!wasConnected) {
      Serial.println("[WIFI] Connection established, syncing time...");
      doSyncTime();
    }
    wasConnected = true;
    reconnectAttempts = 0;
  } else {
    if (wasConnected) {
      Serial.println("[WIFI] Connection lost!");
      wasConnected = false;
      handleWiFiDisconnect();
    }
    
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
      lastReconnectAttempt = millis();
      
      if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        reconnectAttempts++;
        Serial.print("[WIFI] Reconnection attempt ");
        Serial.print(reconnectAttempts);
        Serial.print("/");
        Serial.println(MAX_RECONNECT_ATTEMPTS);
        tryConnectWiFi();
      } else {
        Serial.println("[WIFI] Max reconnection attempts reached.");
        reconnectAttempts = 0;
      }
    }
  }
  
  // Periodic time sync (even if already synced)
  if (currentlyConnected && (millis() - lastTimeSyncMs > TIME_SYNC_INTERVAL_MS)) {
    doSyncTime();
  }
}

bool BoardSetup::tryConnectWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.print(wifiSsid);
  
  WiFi.begin(wifiSsid, wifiPassword);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECTION_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] Connected! IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    wl_status_t status = WiFi.status();
    Serial.print("[WIFI] Connection failed! Status code: ");
    Serial.print(status);
    Serial.print(" = ");
    switch (status) {
      case WL_IDLE_STATUS: Serial.println("IDLE"); break;
      case WL_NO_SSID_AVAIL: Serial.println("NO_SSID_AVAIL"); break;
      case WL_SCAN_COMPLETED: Serial.println("SCAN_COMPLETED"); break;
      case WL_CONNECT_FAILED: Serial.println("CONNECT_FAILED"); break;
      case WL_CONNECTION_LOST: Serial.println("CONNECTION_LOST"); break;
      case WL_DISCONNECTED: Serial.println("DISCONNECTED"); break;
      default: Serial.println("UNKNOWN"); break;
    }
    return false;
  }
}

void BoardSetup::handleWiFiDisconnect() {
  // Keep the last successfully synced clock running while offline.
}

bool BoardSetup::doSyncTime() {
  bool ok = false;
  
  // Try custom server first if configured
  if (timeServerUrl && timeServerUrl[0] != '\0') {
    ok = syncTimeFromServer();
  }
  
  // Fall back to NTP
  if (!ok) {
    ok = syncTimeFromNTP();
  }
  
  if (ok) {
    timeSynced = true;
    lastTimeSyncMs = millis();
#if RTC_ENABLED
    syncRtcFromSystemTime();
#endif
  }
  
  return ok;
}

bool BoardSetup::syncTimeFromServer() {
  Serial.print("[TIME] Fetching from server...");
  
  String url = "http://";
  url += timeServerUrl;
  Serial.print(" URL: ");
  Serial.print(url);
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  
  int code = http.GET();
  Serial.print(" HTTP=");
  Serial.print(code);
  
  if (code != 200) {
    Serial.print(" failed (HTTP ");
    Serial.print(code);
    Serial.println(")");
    http.end();
    return false;
  }
  
  String payload = http.getString();
  http.end();
  
  Serial.print(" Payload: ");
  Serial.print(payload.substring(0, 50));
  Serial.print("...");
  
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print(" JSON error: ");
    Serial.println(err.c_str());
    return false;
  }
  
  // Extract timestamp - handle both integer and string formats
  long long timestamp_ms = 0;
  if (doc["timestamp"].is<int64_t>()) {
    timestamp_ms = doc["timestamp"].as<int64_t>();
  } else if (doc["timestamp"].is<const char*>()) {
    const char* ts_str = doc["timestamp"];
    timestamp_ms = atoll(ts_str);
  }
  
  Serial.print(" ts_ms=");
  Serial.print((long)timestamp_ms);
  Serial.print(" raw=");
  Serial.print(doc["timestamp"].as<const char*>());
  
  if (timestamp_ms == 0) {
    Serial.println(" failed to parse timestamp");
    return false;
  }
  
  // Detect milliseconds vs seconds (ms will be > 10 billion)
  time_t timestamp;
  if (timestamp_ms > 10000000000LL) {
    timestamp = (time_t)(timestamp_ms / 1000);  // Convert ms to seconds
    Serial.print(" (ms->s)");
  } else {
    timestamp = (time_t)timestamp_ms;
  }
  
  if (timestamp < MIN_VALID_TIME) {
    Serial.println(" invalid timestamp");
    return false;
  }
  
  applySystemTime(timestamp);
  
  Serial.print(" OK: ");
  Serial.println(getCurrentTimeStr());
  return true;
}

bool BoardSetup::syncTimeFromNTP() {
  Serial.print("[TIME] Syncing via NTP...");
  
  configureLocalTimezone();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  
  time_t now = 0;
  unsigned long start = millis();
  bool synced = false;
  
  while (millis() - start < 20000) {
    delay(500);
    Serial.print(".");
    time(&now);
    if (now > MIN_VALID_TIME) {
      synced = true;
      break;
    }
  }
  Serial.println();
  
  if (synced) {
    Serial.print("[TIME] NTP OK: ");
    Serial.println(getCurrentTimeStr());
    return true;
  } else {
    Serial.println("[TIME] NTP failed.");
    return false;
  }
}

// WiFi getters
bool BoardSetup::isWiFiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String BoardSetup::getWiFiStatus() const {
  switch (WiFi.status()) {
    case WL_CONNECTED: return "Connected";
    case WL_NO_SHIELD: return "No WiFi";
    case WL_IDLE_STATUS: return "Idle";
    case WL_NO_SSID_AVAIL: return "No SSID";
    case WL_SCAN_COMPLETED: return "Scan done";
    case WL_CONNECT_FAILED: return "Failed";
    case WL_CONNECTION_LOST: return "Lost";
    case WL_DISCONNECTED: return "Disconnected";
    default: return "Unknown";
  }
}

String BoardSetup::getWiFiIP() const {
  return isWiFiConnected() ? WiFi.localIP().toString() : "--";
}

int BoardSetup::getWiFiRSSI() const {
  return isWiFiConnected() ? WiFi.RSSI() : 0;
}

// Time getters
bool BoardSetup::isTimeSynced() const {
  return timeSynced && isSystemTimeValid();
}

bool BoardSetup::hasValidTime() const {
  return isSystemTimeValid();
}

bool BoardSetup::isRtcAvailable() const {
#if RTC_ENABLED
  return rtcAvailable;
#else
  return false;
#endif
}

String BoardSetup::getCurrentTimeStr() const {
  if (!isSystemTimeValid()) {
    return "--:--";
  }

  time_t now;
  time(&now);
  struct tm ti;
  localtime_r(&now, &ti);
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &ti);
  return String(buf);
}

// Healthcheck
BoardSetupStatus BoardSetup::getStatus() const {
  return {
    .wifiConnected = isWiFiConnected(),
    .timeSynced = isTimeSynced(),
    .wifiIP = getWiFiIP(),
    .wifiRSSI = getWiFiRSSI(),
    .currentTime = getCurrentTimeStr(),
    .uptimeMs = millis()
  };
}

bool BoardSetup::healthcheck() {
  return isWiFiConnected() && hasValidTime();
}

bool BoardSetup::isSystemTimeValid() const {
  time_t now;
  time(&now);
  return now >= MIN_VALID_TIME;
}

void BoardSetup::applySystemTime(time_t timestamp) {
  configureLocalTimezone();
  struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
  settimeofday(&tv, NULL);
}

#if RTC_ENABLED
bool BoardSetup::beginRtc() {
  Wire.begin();
  rtcAvailable = rtc.begin();
  if (!rtcAvailable) {
    Serial.println("[RTC] DS3231 not found");
    return false;
  }

  if (rtc.lostPower()) {
    Serial.println("[RTC] Lost power, time may be invalid");
  } else {
    Serial.println("[RTC] DS3231 ready");
  }
  return true;
}

bool BoardSetup::syncTimeFromRtc() {
  if (!rtcAvailable) {
    return false;
  }

  DateTime now = rtc.now();
  time_t timestamp = now.unixtime();
  if (timestamp < MIN_VALID_TIME) {
    Serial.println("[RTC] Invalid RTC time");
    return false;
  }

  applySystemTime(timestamp);
  Serial.print("[RTC] Time restored: ");
  Serial.println(getCurrentTimeStr());
  return true;
}

void BoardSetup::syncRtcFromSystemTime() {
  if (!rtcAvailable || !isSystemTimeValid()) {
    return;
  }

  time_t now;
  time(&now);
  rtc.adjust(DateTime(static_cast<uint32_t>(now)));
  Serial.println("[RTC] RTC updated from system time");
}
#endif

void BoardSetup::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] Event: Connected to AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI] Event: Disconnected");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WIFI] Event: Got IP - ");
      Serial.println(WiFi.localIP());
      break;
    default:
      break;
  }
}
