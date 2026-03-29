# Arduino Transit Stop Board

ESP32 Arduino projects collection for IoT and display applications.

## Projects

| Project | Description |
|---------|-------------|
| **[transit-stop-board](transit-stop-board/)** | 240x320 touch LCD showing live transit departures via a departureboards API |
| **[sensor](sensor/)** | ESP32 temperature/humidity sensor posting data to HTTP endpoint |

## Getting Started

Each project is a standalone PlatformIO project. Navigate to the project folder for specific setup instructions.

### Prerequisites

- [PlatformIO](https://platformio.org/)
- ESP32-DevKitC board (ESP32-WROOM-32D)

### Common Setup

```bash
# Copy config example and edit with your settings
cp src/config.example.h src/config.h

# Build
pio run

# Upload
pio run --target upload

# Monitor
pio device monitor -b 115200 -f direct
```
