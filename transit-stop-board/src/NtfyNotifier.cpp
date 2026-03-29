#include "NtfyNotifier.h"

NtfyNotifier::NtfyNotifier(const char* serverUrl, const char* topic) 
  : serverUrl(serverUrl), topic(topic), enabled(false) {
  memset(&watched, 0, sizeof(watched));
}

void NtfyNotifier::begin() {
  enabled = (serverUrl != nullptr && serverUrl[0] != '\0' && 
             topic != nullptr && topic[0] != '\0');
  if (enabled) {
    Serial.println("[NTFY] Notifier initialized");
  } else {
    Serial.println("[NTFY] Notifier disabled (no URL or topic configured)");
  }
}

bool NtfyNotifier::isEnabled() const {
  return enabled;
}

bool NtfyNotifier::canSendNotification(const char* title, const char* message) {
  time_t now = time(nullptr);
  unsigned long currentMs = millis();
  
  // Reset hourly counter
  if (now - hourStartTime >= 3600) {
    hourStartTime = now;
    notificationsThisHour = 0;
  }
  
  // Check hourly limit
  if (notificationsThisHour >= MAX_NOTIFICATIONS_PER_HOUR) {
    Serial.println("[NTFY] Flood protection: hourly limit reached");
    return false;
  }
  
  // Check minimum interval
  if (currentMs - lastNotificationMs < MIN_NOTIFICATION_INTERVAL_MS) {
    Serial.println("[NTFY] Flood protection: too soon since last notification");
    return false;
  }
  
  // Check for duplicate
  if (lastSentTitle.equals(title) && lastSentMessage.equals(message)) {
    Serial.println("[NTFY] Flood protection: duplicate notification");
    return false;
  }
  
  return true;
}

bool NtfyNotifier::sendNotification(const char* title, const char* message, 
                                     const char* priority, const char* tags) {
  if (!enabled || WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTFY] Cannot send - not enabled or no WiFi");
    return false;
  }
  
  // Check flood protection
  if (!canSendNotification(title, message)) {
    return false;
  }

  String url = String(serverUrl);
  if (!url.endsWith("/")) url += "/";
  url += topic;

  Serial.print("[NTFY] Sending to: ");
  Serial.println(url);
  Serial.print("[NTFY] Title: ");
  Serial.println(title);
  Serial.print("[NTFY] Message: ");
  Serial.println(message);

  HTTPClient http;
  http.setTimeout(10000); // 10 second timeout
  http.begin(url);
  
  http.addHeader("Content-Type", "text/plain");
  if (title && title[0] != '\0') {
    http.addHeader("Title", title);
  }
  if (priority && priority[0] != '\0') {
    http.addHeader("Priority", priority);
  }
  if (tags && tags[0] != '\0') {
    http.addHeader("Tags", tags);
  }

  int code = http.POST(message);
  String response = http.getString();
  http.end();

  Serial.print("[NTFY] HTTP code: ");
  Serial.println(code);
  
  if (response.length() > 0) {
    Serial.print("[NTFY] Response: ");
    Serial.println(response);
  }

  if (code == 200) {
    // Track successful notification for flood protection
    lastNotificationMs = millis();
    notificationsThisHour++;
    lastSentTitle = title;
    lastSentMessage = message;
    
    Serial.print("[NTFY] Sent successfully: ");
    Serial.println(title);
    return true;
  } else {
    Serial.print("[NTFY] Failed to send (HTTP ");
    Serial.print(code);
    Serial.println(")");
    return false;
  }
}

void NtfyNotifier::notifyError(NotificationType type, const char* details) {
  const char* title;
  const char* priority = "high";
  const char* tags = "warning";
  
  switch (type) {
    case NotificationType::ERROR_WIFI:
      title = "WiFi Disconnected";
      tags = "wifi,warning";
      break;
    case NotificationType::ERROR_API:
      title = "API Error";
      tags = "api,warning";
      break;
    case NotificationType::ERROR_GENERIC:
      title = "Transit Board Error";
      tags = "error";
      break;
    default:
      title = "Notification";
  }
  
  String message = details ? String(details) : "An error occurred on the transit board";
  sendNotification(title, message.c_str(), priority, tags);
}

void NtfyNotifier::notifyWiFiDisconnected() {
  notifyError(NotificationType::ERROR_WIFI, "WiFi connection lost. Attempting to reconnect...");
}

void NtfyNotifier::notifyWiFiReconnected() {
  if (!enabled) return;
  sendNotification("WiFi Reconnected", "Transit board back online", "low", "wifi");
}

void NtfyNotifier::notifyApiError(int httpCode) {
  String msg = "API request failed with HTTP code: " + String(httpCode);
  notifyError(NotificationType::ERROR_API, msg.c_str());
}

void NtfyNotifier::notifyGenericError(const char* message) {
  notifyError(NotificationType::ERROR_GENERIC, message);
}

bool NtfyNotifier::startWatching(const Departure& departure, time_t departureTime, const String& stopName) {
  if (!enabled) return false;
  
  watched.active = true;
  watched.tripId = departure.tripId;
  watched.line = departure.line;
  watched.headsign = departure.headsign;
  watched.platform = departure.platform;
  watched.stopName = stopName;
  watched.departureTime = departureTime;
  watched.lastDelaySeconds = departure.delaySeconds;
  
  // Reset threshold tracking
  for (int i = 0; i < THRESHOLD_COUNT; i++) {
    watched.thresholdsSent[i] = false;
  }
  
  // Send confirmation
  String title = "Watching " + watched.line + " to " + watched.headsign;
  String timeStr = String(ctime(&departureTime));
  timeStr.trim(); // Remove newline from ctime
  String message = "Departure at " + timeStr;
  if (departure.delaySeconds > 0) {
    message += " (delayed " + String(departure.delaySeconds / 60) + " min)";
  }
  if (!watched.stopName.isEmpty()) {
    message += " | Stop: " + watched.stopName;
  }
  
  sendNotification(title.c_str(), message.c_str(), "default", "bus,eye");
  
  Serial.print("[NTFY] Started watching trip ");
  Serial.println(watched.tripId);
  return true;
}

void NtfyNotifier::stopWatching() {
  if (!watched.active) return;
  
  String title = "Stopped watching " + watched.line;
  String message = "No longer monitoring " + watched.line + " to " + watched.headsign;
  sendNotification(title.c_str(), message.c_str(), "low", "bus,cancel");
  
  watched.active = false;
  watched.tripId = "";
  
  Serial.println("[NTFY] Watch stopped");
}

bool NtfyNotifier::isWatching() const {
  return watched.active;
}

const WatchedConnection& NtfyNotifier::getWatchedConnection() const {
  return watched;
}

int NtfyNotifier::calculateMinutesUntil(time_t now) const {
  return (int)((watched.departureTime - now) / 60);
}

void NtfyNotifier::sendThresholdNotification(int threshold, int minutesUntil, int delayMinutes) {
  String title = watched.line + " to " + watched.headsign;
  
  String message;
  if (delayMinutes > 0) {
    message = String(minutesUntil) + " min until departure (delayed " + 
              String(delayMinutes) + " min)";
  } else {
    message = String(minutesUntil) + " min until departure";
  }
  
  if (!watched.stopName.isEmpty()) {
    message += " | Stop: " + watched.stopName;
  }
  
  const char* priority = (threshold <= 15) ? "high" : "default";
  
  sendNotification(title.c_str(), message.c_str(), priority, "bus,clock");
}

void NtfyNotifier::sendDelayUpdateNotification(int oldDelayMinutes, int newDelayMinutes) {
  String title = "Delay Update: " + watched.line;
  
  String message;
  if (newDelayMinutes > oldDelayMinutes) {
    message = "Delay increased from " + String(oldDelayMinutes) + 
              " to " + String(newDelayMinutes) + " minutes";
  } else {
    message = "Delay decreased from " + String(oldDelayMinutes) + 
              " to " + String(newDelayMinutes) + " minutes";
  }
  
  int minutesUntil = calculateMinutesUntil(time(nullptr));
  message += " (departure in " + String(minutesUntil) + " min)";
  
  const char* priority = (newDelayMinutes > oldDelayMinutes) ? "high" : "default";
  
  sendNotification(title.c_str(), message.c_str(), priority, "bus,warning");
}

void NtfyNotifier::update(time_t now, int currentDelaySeconds) {
  if (!watched.active) return;
  
  int minutesUntil = calculateMinutesUntil(now);
  int currentDelayMinutes = currentDelaySeconds / 60;
  int lastDelayMinutes = watched.lastDelaySeconds / 60;
  
  // Check if departure has passed (with 5 min buffer for delays)
  if (minutesUntil < -5) {
    Serial.println("[NTFY] Watched departure has passed, auto-stopping watch");
    stopWatching();
    return;
  }
  
  // Send threshold notifications
  for (int i = 0; i < THRESHOLD_COUNT; i++) {
    if (!watched.thresholdsSent[i] && minutesUntil <= WATCH_THRESHOLDS[i]) {
      sendThresholdNotification(WATCH_THRESHOLDS[i], minutesUntil, currentDelayMinutes);
      watched.thresholdsSent[i] = true;
    }
  }
  
  // Notify on significant delay changes (> 2 minutes difference)
  if (abs(currentDelayMinutes - lastDelayMinutes) >= 2) {
    sendDelayUpdateNotification(lastDelayMinutes, currentDelayMinutes);
    watched.lastDelaySeconds = currentDelaySeconds;
  }
}

void NtfyNotifier::refreshWatchedDeparture(const Departure& updatedDeparture) {
  if (!watched.active) return;
  if (watched.tripId != updatedDeparture.tripId) return;
  
  // Update platform info
  watched.platform = updatedDeparture.platform;
  
  // Check for delay changes
  int newDelayMinutes = updatedDeparture.delaySeconds / 60;
  int lastDelayMinutes = watched.lastDelaySeconds / 60;
  
  if (abs(newDelayMinutes - lastDelayMinutes) >= 2) {
    sendDelayUpdateNotification(lastDelayMinutes, newDelayMinutes);
    watched.lastDelaySeconds = updatedDeparture.delaySeconds;
  }
  
  Serial.println("[NTFY] Watched departure refreshed");
}
