# Arduino Transit Stop Board

ESP32 project for a `240x320` touch LCD that shows live transit departures from a departureboards API.

The repo is prepared for `PlatformIO + Arduino framework` inside the `transit-stop-board/` project folder.

Confirmed hardware:

- `ESP32-DevKitC`, `ESP32-WROOM-32D`, `38-pin`, `CP2102`, USB-C
- `2.4" TFT SPI ILI9341` touch display with SD card support

## Current Behavior

- connects ESP32 to Wi-Fi
- syncs time over NTP
- loads departures from a `departureboards` API
- filters only relevant direct directions:
  - `X -> Y`, otherwise `Z`
  - `X -> Y`
- shows `Zadny vhodny spoj.` if no matching direct departure is available
- supports temporary Serial controls:
  - `A = -30 min`
  - `S = -5 min`
  - `D = now`
  - `F = +5 min`
  - `G = +30 min`
  - `H = switch stop`

The UI is designed for landscape mode, so a `240x320` panel is treated as `320x240` during rendering.

## Project Layout

```text
transit-stop-board/
  platformio.ini
  src/main.cpp
  include/config.h
  include/config.example.h
```

- `transit-stop-board/src/main.cpp`: current PlatformIO entry point
- `transit-stop-board/include/config.h`: local secrets and stop config, not committed
- `transit-stop-board/include/config.example.h`: template config

## Configuration

Local secrets are stored in:

`transit-stop-board/include/config.h`

Example config is in:

`transit-stop-board/include/config.example.h`

The config contains:

- Wi-Fi SSID and password
- Golemio token
- optional `ntfy.sh` notification settings
- stop labels and encoded stop names
- optional route/headsign filters per stop; if omitted, all departures for that `routeType` are shown

## Wiring Example

For your confirmed `2.4" SPI ILI9341` display, the project now uses a default `ILI9341 + XPT2046` wiring setup for ESP32-DevKitC style boards.

### TFT SPI

- `VCC -> 3.3V`
- `GND -> GND`
- `CS -> GPIO5`
- `RST -> GPIO4`
- `DC -> GPIO2`
- `MOSI -> GPIO23`
- `SCK -> GPIO18`
- `LED -> 3.3V` or a PWM-capable pin through the module design
- `MISO -> GPIO19` if used by the display module

### Touch SPI

- `T_CS -> GPIO14`
- `T_IRQ -> GPIO27` optional
- `T_CLK -> GPIO18`
- `T_DIN -> GPIO23`
- `T_DO -> GPIO19`

Display and touch often share the same SPI bus and use different chip-select pins.

The current code assumes:

- `TFT_CS = GPIO5`
- `TFT_DC = GPIO2`
- `TFT_RST = GPIO4`
- `TOUCH_CS = GPIO14`
- `TOUCH_IRQ = GPIO27`

Touch calibration values are still generic defaults and may need adjustment for your exact module.

## Notes

- `transit-stop-board/include/config.h` is ignored by git
- `transit-stop-board/include/config.example.h` is safe to commit
- `board = esp32dev` is the current PlatformIO target for the ESP32-DevKitC style board
- LCD rendering now targets `ILI9341` in landscape mode
- touch input now targets `XPT2046` with default calibration values that may need tuning

## PlatformIO Usage

1. Open the repo in `VS Code`
2. Install the `PlatformIO IDE` extension if needed
3. Open the `transit-stop-board/` folder as the PlatformIO project
4. Copy `include/config.example.h` to `include/config.h`
5. Adjust Wi-Fi and Golemio token in `include/config.h`
6. Connect the ESP32 board
7. Build and upload with PlatformIO

Typical commands from `transit-stop-board/`:

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

## Recommended Next Step

Next likely work is touch calibration and visual tuning:

- verify touch controller is really `XPT2046`
- adjust `TOUCH_MIN/MAX` calibration constants if tap positions are off
- tune fonts and row density on the real panel
