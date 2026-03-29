#pragma once

#include <Arduino.h>
#include "../config.h"

struct Departure {
  String line;
  int routeType;
  String headsign;
  String minutes;
  String departureTime;  // HH:MM format
  String platform;
  String tripId;
  int delaySeconds;
  bool wheelchairAccessible;
};