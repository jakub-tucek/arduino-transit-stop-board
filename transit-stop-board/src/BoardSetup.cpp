#include "BoardSetup.h"

namespace {
constexpr const char* kDefaultTimezoneRule = "CET-1CEST,M3.5.0/2,M10.5.0/3";

const char* wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

const char* authModeToString(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-ent";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK: return "wapi";
    default: return "other";
  }
}

const char* disconnectReasonToString(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
    case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
    case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "DISASSOC_PWRCAP_BAD";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "DISASSOC_SUPCHAN_BAD";
    case WIFI_REASON_IE_INVALID: return "IE_INVALID";
    case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE_IN_4WAY_DIFFERS";
    case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
    case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
    case WIFI_REASON_INVALID_RSN_IE_CAP: return "INVALID_RSN_IE_CAP";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
    case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    default: return "UNKNOWN";
  }
}

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
  
  // Start WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
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

  WiFi.disconnect(false, false);
  delay(100);
  
  WiFi.begin(wifiSsid, wifiPassword);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECTION_TIMEOUT_MS) {
    delay(500);
    wl_status_t status = WiFi.status();
    if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
      break;
    }
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
    Serial.println(wifiStatusToString(status));
    debugWiFiEnvironment(status);
    return false;
  }
}

void BoardSetup::debugWiFiEnvironment(wl_status_t failedStatus) {
  Serial.println("[WIFI] Starting diagnostic scan...");
  int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.println("[WIFI] Scan failed");
    return;
  }

  Serial.print("[WIFI] Visible networks: ");
  Serial.println(networkCount);

  bool foundConfiguredSsid = false;
  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid != wifiSsid) {
      continue;
    }

    foundConfiguredSsid = true;
    Serial.print("[WIFI] Found configured SSID on channel ");
    Serial.print(WiFi.channel(i));
    Serial.print(", RSSI ");
    Serial.print(WiFi.RSSI(i));
    Serial.print(" dBm, auth ");
    Serial.println(authModeToString(static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i))));
  }

  if (!foundConfiguredSsid) {
    Serial.println("[WIFI] Configured SSID not visible in scan.");
    Serial.println("[WIFI] Hint: ESP32 supports only 2.4 GHz WiFi.");
    int previewCount = networkCount < 5 ? networkCount : 5;
    for (int i = 0; i < previewCount; i++) {
      Serial.print("[WIFI] Nearby[");
      Serial.print(i);
      Serial.print("]: ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" | RSSI ");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm | ch ");
      Serial.print(WiFi.channel(i));
      Serial.print(" | ");
      Serial.println(authModeToString(static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i))));
    }
  } else if (failedStatus == WL_CONNECT_FAILED) {
    Serial.println("[WIFI] SSID is visible, so re-check password/security mode compatibility.");
  }

  WiFi.scanDelete();
}

void BoardSetup::handleWiFiDisconnect() {
  // Keep the last successfully synced system clock running while offline.
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
    default: return wifiStatusToString(WiFi.status());
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

void BoardSetup::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] Event: Connected to AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      static uint8_t lastReason = 255;
      static unsigned long lastReasonLogMs = 0;
      uint8_t reason = info.wifi_sta_disconnected.reason;
      unsigned long now = millis();
      if (reason != lastReason || now - lastReasonLogMs > 5000) {
        Serial.print("[WIFI] Event: Disconnected, reason ");
        Serial.print(reason);
        Serial.print(" = ");
        Serial.println(disconnectReasonToString(reason));
        lastReason = reason;
        lastReasonLogMs = now;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WIFI] Event: Got IP - ");
      Serial.println(WiFi.localIP());
      break;
    default:
      break;
  }
}
