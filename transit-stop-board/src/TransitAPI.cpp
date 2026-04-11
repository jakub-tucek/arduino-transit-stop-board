#include "TransitAPI.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

TransitAPI::TransitAPI(const char* token) : apiToken(token), minutesAfter(180) {
}

bool TransitAPI::begin() {
  return true;
}

void TransitAPI::setMinutesAfter(int minutes) {
  // Clamp to valid range: -4350 to 4320
  if (minutes < -4350) minutes = -4350;
  if (minutes > 4320) minutes = 4320;
  // Must be > -minutesBefore (minutesBefore is 0, so just ensure > 0 effectively)
  // But API allows negative values for past data, so we allow anything in range
  minutesAfter = minutes;
}

int TransitAPI::getMinutesAfter() const {
  return minutesAfter;
}

bool TransitAPI::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String TransitAPI::formatTimeAgo(unsigned long fetchMillis) {
  if (fetchMillis == 0) return "nikdy";
  unsigned long ago = (millis() - fetchMillis) / 60000;
  if (ago < 1) return "ted";
  if (ago == 1) return "pred 1 min";
  return "pred " + String(ago) + " min";
}

String TransitAPI::isoToHHMM(const String& iso) {
  int tPos = iso.indexOf('T');
  if (tPos < 0 || iso.length() < tPos + 6) return "--:--";
  return iso.substring(tPos + 1, tPos + 6);
}

String TransitAPI::normalizeText(const String& input) {
  String out = input;
  out.toLowerCase();
  // Czech diacritics
  out.replace("\xC3\xA1", "a"); out.replace("\xC3\xA9", "e"); out.replace("\xC3\xAD", "i");
  out.replace("\xC3\xB3", "o"); out.replace("\xC3\xBA", "u"); out.replace("\xC3\xBD", "y");
  out.replace("\xC4\x8D", "c"); out.replace("\xC4\x8F", "d"); out.replace("\xC4\x9B", "e");
  out.replace("\xC5\x88", "n"); out.replace("\xC5\x99", "r"); out.replace("\xC5\xA1", "s");
  out.replace("\xC5\xA5", "t"); out.replace("\xC5\xAF", "u"); out.replace("\xC5\xBE", "z");
  out.replace("\xC3\x81", "a"); out.replace("\xC3\x89", "e"); out.replace("\xC3\x8D", "i");
  out.replace("\xC3\x93", "o"); out.replace("\xC3\x9A", "u"); out.replace("\xC3\x9D", "y");
  out.replace("\xC4\x8C", "c"); out.replace("\xC4\x8E", "d"); out.replace("\xC4\x9A", "e");
  out.replace("\xC5\x87", "n"); out.replace("\xC5\x98", "r"); out.replace("\xC5\xA0", "s");
  out.replace("\xC5\xA4", "t"); out.replace("\xC5\xAE", "u"); out.replace("\xC5\xBD", "z");
  return out;
}

bool TransitAPI::headsignMatches(const String& headsign, const char* expected) {
  if (expected == nullptr || expected[0] == '\0') return false;
  String normalizedHeadsign = normalizeText(headsign);
  String normalizedExpected = normalizeText(String(expected));
  return normalizedHeadsign.indexOf(normalizedExpected) >= 0;
}

bool TransitAPI::routeMatches(const String& line, const char* expectedLine) {
  return expectedLine != nullptr && expectedLine[0] != '\0' && line.equalsIgnoreCase(expectedLine);
}

bool TransitAPI::hasRouteFilter(const StopRoute& route) {
  return (route.line != nullptr && route.line[0] != '\0') ||
         (route.headsignMatch != nullptr && route.headsignMatch[0] != '\0');
}

bool TransitAPI::stopUsesRouteFilters(const StopConfig& stop) {
  for (const StopRoute& route : stop.routes) {
    if (hasRouteFilter(route)) {
      return true;
    }
  }
  return false;
}

bool TransitAPI::matchesRoute(const Departure& item, const StopRoute& route) {
  return routeMatches(item.line, route.line) && headsignMatches(item.headsign, route.headsignMatch);
}

// Calculate minutes until departure from ISO timestamp
String TransitAPI::calculateMinutes(const String& isoTimestamp) {
  if (isoTimestamp.length() == 0) return "?";
  
  // Parse ISO timestamp: 2025-03-29T19:30:00+01:00 or 2025-03-29T19:30:00Z
  int year, month, day, hour, minute, second;
  
  if (sscanf(isoTimestamp.c_str(), "%d-%d-%dT%d:%d:%d", 
             &year, &month, &day, &hour, &minute, &second) != 6) {
    return "?";
  }
  
  // Get current time
  time_t now;
  time(&now);
  
  // Create departure time struct
  struct tm depTm = {
    .tm_sec = second,
    .tm_min = minute,
    .tm_hour = hour,
    .tm_mday = day,
    .tm_mon = month - 1,
    .tm_year = year - 1900
  };
  
  time_t depTime = mktime(&depTm);
  
  // Calculate difference in minutes
  int diffMinutes = (int)((depTime - now) / 60);
  
  if (diffMinutes < 1) return "<1";
  return String(diffMinutes);
}

int TransitAPI::fetchDepartures(const StopConfig& stop, Departure* outList, int maxCount) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] No WiFi");
    return 0;
  }

  // v2 departureboards with cisIds[]
  String url = "https://api.golemio.cz/v2/pid/departureboards?";
  
  // Use cisIds[] or aswIds[]
  if (stop.cisId > 0) {
    url += "cisIds[]=";
    url += String(stop.cisId);
  } else if (stop.aswId && stop.aswId[0] != '\0') {
    url += "aswIds[]=";
    url += stop.aswId;
  } else {
    Serial.println("[API] Error: No stop ID configured");
    return 0;
  }
  
  url += "&limit=" + String(maxCount);
  url += "&minutesBefore=0";  // Start from now
  url += "&minutesAfter=" + String(minutesAfter);  // Configurable window (default 180)
  url += "&mode=departures";
  url += "&order=real";

  Serial.print("[API] GET ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("X-Access-Token", apiToken);

  int code = http.GET();
  if (code != 200) {
    Serial.print("[API] HTTP error: ");
    Serial.println(code);
    http.end();
    return 0;
  }

  // Get payload size for info
  int payloadSize = http.getSize();
  Serial.print("[API] Payload size: ");
  Serial.println(payloadSize);
  
  // Read response into String (more reliable than streaming with filter)
  String payload = http.getString();
  http.end();
  
  Serial.print("[API] Read ");
  Serial.print(payload.length());
  Serial.println(" bytes");
  
  // Parse JSON - departureboards v2 returns object with "departures" array
  JsonDocument filter;
  filter["departures"][0]["route"]["short_name"] = true;
  filter["departures"][0]["route"]["type"] = true;
  filter["departures"][0]["trip"]["headsign"] = true;
  filter["departures"][0]["trip"]["id"] = true;
  filter["departures"][0]["trip"]["is_wheelchair_accessible"] = true;
  filter["departures"][0]["departure_timestamp"]["scheduled"] = true;
  filter["departures"][0]["departure_timestamp"]["predicted"] = true;
  filter["departures"][0]["departure_timestamp"]["minutes"] = true;
  filter["departures"][0]["stop"]["platform_code"] = true;
  filter["departures"][0]["delay"]["seconds"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  
  if (err) {
    Serial.print("[API] JSON parse failed: ");
    Serial.println(err.c_str());
    Serial.print("[API] First 200 chars: ");
    Serial.println(payload.substring(0, 200));
    return 0;
  }
  
  Serial.print("[API] JSON parsed successfully");

  int count = 0;
  JsonArray arr = doc["departures"].as<JsonArray>();
  
  Serial.print("[API] Total departures: ");
  Serial.println(arr.size());

  const bool useRouteFilters = stopUsesRouteFilters(stop);

  for (JsonObject d : arr) {
    if (count >= maxCount) break;

    Departure item;
    item.line = d["route"]["short_name"] | "";
    item.routeType = d["route"]["type"] | 0;
    item.headsign = d["trip"]["headsign"] | "";
    item.platform = d["stop"]["platform_code"] | "";
    item.tripId = d["trip"]["id"] | "";
    item.wheelchairAccessible = d["trip"]["is_wheelchair_accessible"] | false;
    item.delaySeconds = d["delay"]["seconds"] | 0;
    
    // Try minutes field first, otherwise calculate from predicted/scheduled
    item.minutes = d["departure_timestamp"]["minutes"] | "";
    String predicted = d["departure_timestamp"]["predicted"] | "";
    String scheduled = d["departure_timestamp"]["scheduled"] | "";
    
    // Extract departure time (HH:MM) from predicted or scheduled timestamp
    String timestamp = predicted.length() ? predicted : scheduled;
    item.departureTime = isoToHHMM(timestamp);
    
    if (item.minutes.length() == 0) {
      item.minutes = calculateMinutes(timestamp);
    }

    if (stop.routeType >= 0 && item.routeType != stop.routeType) {
      continue;
    }

    if (!useRouteFilters) {
      outList[count++] = item;
      continue;
    }

    // Check both routes
    for (int i = 0; i < 2; i++) {
      if (matchesRoute(item, stop.routes[i])) {
        if (stop.routes[i].headsignDisplay && stop.routes[i].headsignDisplay[0] != '\0') {
          item.headsign = stop.routes[i].headsignDisplay;
        }
        outList[count++] = item;
        break;
      }
    }
  }

  Serial.print("[API] Filtered: ");
  Serial.println(count);

  return count;
}
