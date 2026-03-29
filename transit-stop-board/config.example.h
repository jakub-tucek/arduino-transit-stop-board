#pragma once

// WiFi credentials
#define WIFI_SSID "your_wifi"
#define WIFI_PASSWORD "your_password"

// Golemio API token (get at https://api.golemio.cz/)
#define GOLEMIO_TOKEN "your_token_here"

// Custom time server (set to empty string "" to use NTP instead)
#define TIME_SERVER_URL ""

// Stop configuration for transferboards API (v4)
// Route types: 0=Tram, 1=Metro, 2=Train, 3=Bus, 4=Ferry, 7=Funicular, 11=Trolleybus
// Find CIS IDs at: https://data.gov.cz/datov%C3%A1-sada?iri=https%3A%2F%2Fdata.gov.cz%2Fzdroj%2Fdatov%C3%A9-sady%2Fhttps---api.golemio.cz-api-v2-gtfs-rt-trip-updates%2Fdump_from_datov%C3%A9_sady

struct StopRoute {
  const char* line;           // Line number (e.g., "197", "B")
  const char* headsignMatch;  // Substring to match in headsign (optional)
  const char* headsignDisplay;// Override display text (optional)
};

struct StopConfig {
  const char* label;          // Display label
  int cisId;                  // CIS stop ID (use if available, 0 otherwise)
  const char* aswId;          // ASW stop ID format: "nodeId_stopId" (use if cisId is 0)
  int routeType;              // Route type (-1 for any)
  StopRoute routes[2];        // Primary and fallback routes
};

constexpr StopConfig STOPS[] = {
  {
    .label = "Andel",
    .cisId = 58759,  // Example CIS ID for Anděl
    .aswId = "",
    .routeType = -1,  // Any type
    .routes = {
      { .line = "B", .headsignMatch = "Zlicin", .headsignDisplay = "Zličín" },
      { .line = "", .headsignMatch = "", .headsignDisplay = "" }
    }
  },
  {
    .label = "Na Knizeci",
    .cisId = 0,
    .aswId = "1040_1",  // Example ASW ID
    .routeType = 3,  // Bus
    .routes = {
      { .line = "197", .headsignMatch = "", .headsignDisplay = "Chodov" },
      { .line = "", .headsignMatch = "", .headsignDisplay = "" }
    }
  }
};

constexpr int STOP_COUNT = sizeof(STOPS) / sizeof(STOPS[0]);