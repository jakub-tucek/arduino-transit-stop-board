#pragma once

#include <Arduino.h>
#include "../config_select.h"

struct Departure {
  String line;
  int routeType;
  String headsign;
  String minutes;
  String departureTime;  // HH:MM format
  time_t departureEpoch = 0;
  String platform;
  String tripId;
  int delaySeconds;
  bool wheelchairAccessible;
};
