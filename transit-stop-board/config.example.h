#pragma once

// WiFi credentials
#define WIFI_SSID "your_wifi"
#define WIFI_PASSWORD "your_password"

// Golemio API token (get at https://api.golemio.cz/)
#define GOLEMIO_TOKEN "your_token_here"

// Optional ntfy.sh notifications configuration
// Omit these defines entirely or leave NTFY_TOPIC empty to disable notifications
// #define NTFY_SERVER "https://ntfy.sh"
// #define NTFY_TOPIC "your_topic_here"

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
  StopRoute routes[2];        // Optional primary and fallback route filters
};

constexpr StopConfig STOPS[] = {
  {
    .label = "Stop A",
    .cisId = 123,
    .aswId = "",
    .routeType = 3,  // Bus
    .routes = {
      // Example: leave filters empty to show all departures for this stop.
      // Leave filters empty to show all bus departures for this stop.
      { .line = "", .headsignMatch = "", .headsignDisplay = "" },
      { .line = "", .headsignMatch = "", .headsignDisplay = "" }
    }
  },
  {
    .label = "Stop B",
    .cisId = 456,
    .aswId = "",
    .routeType = 3,  // Bus
    .routes = {
      // Example stop with multiple lines/platforms.
      // Leave filters empty to show all bus departures for this stop.
      { .line = "", .headsignMatch = "", .headsignDisplay = "" },
      { .line = "", .headsignMatch = "", .headsignDisplay = "" }
    }
  }
};

constexpr int STOP_COUNT = sizeof(STOPS) / sizeof(STOPS[0]);
