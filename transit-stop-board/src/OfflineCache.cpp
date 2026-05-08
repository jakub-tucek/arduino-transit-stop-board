#include "OfflineCache.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>

namespace {
constexpr const char* kCacheDir = "/cache";

uint32_t fnv1aUpdate(uint32_t hash, const char* value) {
  if (!value) return hash;
  while (*value) {
    hash ^= static_cast<uint8_t>(*value++);
    hash *= 16777619UL;
  }
  return hash;
}

String getStopCacheSuffix(const StopConfig& stop) {
  uint32_t hash = 2166136261UL;
  hash = fnv1aUpdate(hash, stop.label);
  hash = fnv1aUpdate(hash, "|");
  hash = fnv1aUpdate(hash, stop.aswId);

  for (size_t i = 0; i < sizeof(stop.routes) / sizeof(stop.routes[0]); i++) {
    const StopRoute& route = stop.routes[i];
    hash = fnv1aUpdate(hash, "|");
    hash = fnv1aUpdate(hash, route.platform);
    hash = fnv1aUpdate(hash, ":");
    hash = fnv1aUpdate(hash, route.line);
    hash = fnv1aUpdate(hash, ":");
    hash = fnv1aUpdate(hash, route.headsignMatch);
    hash = fnv1aUpdate(hash, ":");
    hash = fnv1aUpdate(hash, route.headsignDisplay);
  }

  return String(hash, HEX);
}
}

bool OfflineCache::begin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[CACHE] Failed to mount SPIFFS");
    return false;
  }

  if (!SPIFFS.exists(kCacheDir) && !SPIFFS.mkdir(kCacheDir)) {
    Serial.println("[CACHE] Failed to create cache directory");
    return false;
  }

  return true;
}

bool OfflineCache::saveDepartures(const StopConfig& stop, const Departure* departures, int count, time_t savedAt) {
  JsonDocument doc;
  doc["label"] = stop.label;
  doc["saved_at"] = static_cast<int64_t>(savedAt);
  JsonArray items = doc["departures"].to<JsonArray>();

  for (int i = 0; i < count; i++) {
    JsonObject item = items.add<JsonObject>();
    item["line"] = departures[i].line;
    item["route_type"] = departures[i].routeType;
    item["headsign"] = departures[i].headsign;
    item["minutes"] = departures[i].minutes;
    item["departure_time"] = departures[i].departureTime;
    item["departure_epoch"] = static_cast<int64_t>(departures[i].departureEpoch);
    item["platform"] = departures[i].platform;
    item["trip_id"] = departures[i].tripId;
    item["delay_seconds"] = departures[i].delaySeconds;
    item["wheelchair"] = departures[i].wheelchairAccessible;
  }

  File file = SPIFFS.open(getCachePath(stop), FILE_WRITE);
  if (!file) {
    Serial.println("[CACHE] Failed to open cache file for writing");
    return false;
  }

  const size_t written = serializeJson(doc, file);
  file.close();
  if (written == 0) {
    Serial.println("[CACHE] Failed to write cache file");
    return false;
  }

  Serial.print("[CACHE] Saved ");
  Serial.print(count);
  Serial.print(" departures to ");
  Serial.println(getCachePath(stop));
  return true;
}

bool OfflineCache::loadDepartures(const StopConfig& stop, Departure* departures, int maxCount, int& count, time_t& savedAt) {
  count = 0;
  savedAt = 0;

  File file = SPIFFS.open(getCachePath(stop), FILE_READ);
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.print("[CACHE] Failed to parse cache: ");
    Serial.println(error.c_str());
    return false;
  }

  savedAt = static_cast<time_t>(doc["saved_at"] | 0);
  JsonArray items = doc["departures"].as<JsonArray>();
  for (JsonObject item : items) {
    if (count >= maxCount) {
      break;
    }

    Departure& departure = departures[count++];
    departure.line = item["line"] | "";
    departure.routeType = item["route_type"] | 0;
    departure.headsign = item["headsign"] | "";
    departure.minutes = item["minutes"] | "";
    departure.departureTime = item["departure_time"] | "--:--";
    departure.departureEpoch = static_cast<time_t>(item["departure_epoch"] | 0);
    departure.platform = item["platform"] | "";
    departure.tripId = item["trip_id"] | "";
    departure.delaySeconds = item["delay_seconds"] | 0;
    departure.wheelchairAccessible = item["wheelchair"] | false;
  }

  Serial.print("[CACHE] Loaded ");
  Serial.print(count);
  Serial.print(" departures from ");
  Serial.println(getCachePath(stop));
  return count > 0;
}

String OfflineCache::getCachePath(const StopConfig& stop) const {
  String suffix = "_" + getStopCacheSuffix(stop);
  if (stop.cisId > 0) {
    return String(kCacheDir) + "/cis_" + String(stop.cisId) + suffix + ".json";
  }

  String aswId = stop.aswId ? String(stop.aswId) : String("unknown");
  aswId.replace("/", "_");
  return String(kCacheDir) + "/asw_" + aswId + suffix + ".json";
}
