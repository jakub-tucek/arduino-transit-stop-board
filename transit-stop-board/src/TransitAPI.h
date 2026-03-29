#pragma once

#include <Arduino.h>
#include "../config.h"
#include "Departure.h"

class TransitAPI {
public:
  TransitAPI(const char* token);
  
  bool begin();
  bool isConnected() const;
  
  // Set the end of interval for departures (in minutes, default 180)
  // Positive = future, negative = past
  // Range: -4350 to 4320, must be > -minutesBefore
  void setMinutesAfter(int minutes);
  int getMinutesAfter() const;
  
  // Fetch departures from v2 departureboards API
  int fetchDepartures(const StopConfig& stop, Departure* outList, int maxCount);
  
  // Utility
  static String formatTimeAgo(unsigned long fetchMillis);
  static String calculateMinutes(const String& isoTimestamp);

private:
  const char* apiToken;
  int minutesAfter;
  
  static String isoToHHMM(const String& iso);
  static String normalizeText(const String& input);
  static bool headsignMatches(const String& headsign, const char* expected);
  static bool routeMatches(const String& line, const char* expectedLine);
  static bool matchesRoute(const Departure& item, const StopRoute& route);
};