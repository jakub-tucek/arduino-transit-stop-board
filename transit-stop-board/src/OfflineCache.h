#pragma once

#include <Arduino.h>
#include <time.h>
#include "Departure.h"

class OfflineCache {
public:
  bool begin();
  bool saveDepartures(const StopConfig& stop, const Departure* departures, int count, time_t savedAt);
  bool loadDepartures(const StopConfig& stop, Departure* departures, int maxCount, int& count, time_t& savedAt);

private:
  String getCachePath(const StopConfig& stop) const;
};
