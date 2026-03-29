#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "Departure.h"

// Notification thresholds in minutes before departure (sane, not spammy)
constexpr int WATCH_THRESHOLDS[] = {45, 30, 20, 15, 10, 8};
constexpr int THRESHOLD_COUNT = sizeof(WATCH_THRESHOLDS) / sizeof(WATCH_THRESHOLDS[0]);

enum class NotificationType {
  ERROR_WIFI,
  ERROR_API,
  ERROR_GENERIC,
  WATCH_STARTED,
  WATCH_DEPARTURE_REMINDER,
  WATCH_DELAY_UPDATED,
  WATCH_CANCELLED
};

struct WatchedConnection {
  bool active = false;
  String tripId;
  String line;
  String headsign;
  String platform;
  String stopName;  // Added stop name
  time_t departureTime;
  int lastDelaySeconds;
  bool thresholdsSent[THRESHOLD_COUNT];
};

class NtfyNotifier {
public:
  NtfyNotifier(const char* serverUrl, const char* topic);
  
  void begin();
  bool isEnabled() const;
  
  // Error notifications
  void notifyError(NotificationType type, const char* details = nullptr);
  void notifyWiFiDisconnected();
  void notifyWiFiReconnected();
  void notifyApiError(int httpCode);
  void notifyGenericError(const char* message);
  
  // Watch management
  bool startWatching(const Departure& departure, time_t departureTime, const String& stopName);
  void stopWatching();
  bool isWatching() const;
  const WatchedConnection& getWatchedConnection() const;
  
  // Update check - call periodically (e.g., every minute)
  void update(time_t now, int currentDelaySeconds);
  
  // Force refresh watched departure data (call after API fetch)
  void refreshWatchedDeparture(const Departure& updatedDeparture);

private:
  const char* serverUrl;
  const char* topic;
  bool enabled;
  WatchedConnection watched;
  
  bool sendNotification(const char* title, const char* message, 
                        const char* priority = "default",
                        const char* tags = nullptr);
  int calculateMinutesUntil(time_t now) const;
  void sendThresholdNotification(int threshold, int minutesUntil, int delayMinutes);
  void sendDelayUpdateNotification(int oldDelayMinutes, int newDelayMinutes);
  
  // Flood protection
  static constexpr unsigned long MIN_NOTIFICATION_INTERVAL_MS = 5000; // 5 seconds
  static constexpr int MAX_NOTIFICATIONS_PER_HOUR = 30;
  unsigned long lastNotificationMs = 0;
  int notificationsThisHour = 0;
  time_t hourStartTime = 0;
  String lastSentTitle;
  String lastSentMessage;
  
  bool canSendNotification(const char* title, const char* message);
};
