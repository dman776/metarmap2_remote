# METARMap Remote

METARMap Remote is the idea for a companion interface to monitor and control a
METARMap2 installation from another device. It connects to the map's built-in
HTTP API, so the Raspberry Pi can continue to fetch weather data and run the
LEDs and optional OLED display locally.

> **Project status:** The firmware runs on the ES3C28P touchscreen (see
> [Hardware](#hardware)) with the Visualizer and Locator screens of the map's
> `/remote` page (see [Screens](#screens)). Some features below are still
> ideas.

## What it could do

- Show whether a map is reachable and summarize its current state.
- Display the configured airports, their latest flight categories, and when
  METARs were last fetched.
- Change the active visualizer and LED brightness.
- Turn the OLED display on or off, when the map has one.
- Request a fresh METAR update and pulse an airport's LED to locate it.
- Work well on phones, tablets, and desktop browsers.

The map remains the source of truth: METARMap Remote would read the map's
reported status and send controls to that map, rather than fetching weather or
driving LEDs itself.

## How it would connect

METARMap2 runs a web server on the port configured by `web_server.port`
(port `80` by default). A remote client would be configured with the map's
hostname or IP address and port, then use its HTTP API:

- `GET /api/status` for map state and airport data.
- `GET /api/live` for current LED and OLED activity.
- `GET /api/metar/<code>` for an airport's decoded report.
- Control routes such as `/visualizer/<n>`, `/brightness/<level>`,
  `/display/on`, `/display/off`, `/locate/<pixel>`, and `/fetch`.
- Authenticated `/config/edit/...` routes for settings, if configuration
  editing is included.

See the [METARMap2 API documentation](https://github.com/dman776/metarmap2/blob/master/API.md)
for response formats, authentication details, and the complete endpoint list.
The [METARMap2 project](https://github.com/dman776/metarmap2) explains how to
build and set up the map.

## Network and security

The map API is intended to be used on a trusted local network. Do not expose
the Raspberry Pi's web server directly to the public internet. For access while
away from home, use a private VPN or another secure network connection.

Authenticated routes use the map's HTTP Basic credentials. Change the
default `metar` / `map` credentials before connecting a remote client, and
avoid storing credentials in browser-visible code or sharing them with
untrusted services. Any implementation should use encrypted transport when
traffic crosses an untrusted network and should make it clear which actions
change map settings.

## Intended boundaries

METARMap Remote is envisioned as a lightweight controller, not a replacement
for the METARMap2 web UI, a weather-data provider, or the
[Home Assistant integration](https://github.com/dman776/metarmap2_ha). Its
purpose is to make the map's essential status and controls convenient from a
separate device.

## Hardware

The remote runs on an LCDwiki **ES3C28P**
([wiki](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display)):

- ESP32-S3 (N16R8): 16 MB flash, 8 MB octal PSRAM, 2.4 GHz Wi-Fi only
- 2.8" ILI9341V 240×320 SPI LCD with PWM backlight
- FT6336G capacitive touch (I2C)
- RGB LED, microSD, I2S speaker/mic, Li-Po charger with battery sense

The full pin map is in [include/board_es3c28p.h](include/board_es3c28p.h).

## Development

The firmware is a [PlatformIO](https://platformio.org/) project using the
Arduino framework, [LVGL](https://lvgl.io/) 9 for the screens,
[LovyanGFX](https://github.com/lovyan03/LovyanGFX) for the display and touch,
and [ArduinoJson](https://arduinojson.org/) for the map's API.

1. Copy `include/secrets.example.h` to `include/secrets.h` (git-ignored) and
   set your Wi-Fi network and the map's host and port.
2. Connect the board's USB-C port. It enumerates as the S3's native
   USB-Serial/JTAG (`/dev/cu.usbmodem*` on macOS), so no driver is needed.
3. Build, flash and watch the console:

   ```sh
   pio run
   pio run -t upload
   pio device monitor
   ```

If an upload can't find the board, hold **BOOT**, tap **RESET**, release
**BOOT**, and upload again.

### Screens

The remote is a small version of the map's `/remote` page, in its colors:

- **Visualizer**: the active visualizer's name and description with previous
  and next buttons, a chip for every visualizer, its legend, and the
  Auto-rotate switch (with a countdown to the next visualizer), the OLED
  switch (when the OLED is enabled in Config) and LED brightness.
- **Locator**: a tile for every airport with its flight category; tap one to
  pulse its LED. The tile flashes when the map pulses it.

The `/remote` page's Map tab is left out: the live map needs a browser.

### How it talks to the map

A background task on the second core polls `/api/live` every 1.5 s and
`/api/status` every minute, and again whenever the visualizer changes (for its
description and legend). Controls send the map's GET endpoints
(`/visualizer/<id>`, `/brightness/<level>`, `/locate/<pixel>`...). As on the
`/remote` page, a control you touch keeps your value for 3 s while the map
catches up. Source layout:

| File | What it does |
|---|---|
| `src/main.cpp` | Setup, Wi-Fi, the LVGL loop |
| `src/ui.cpp` | The screens |
| `src/map_client.cpp` | HTTP polling and commands, on its own task |
| `src/map_state.h` | The parsed API state |
| `src/lvgl_port.cpp` | LVGL display flush, touch input, screenshots |
| `src/display.h` | LovyanGFX panel and touch config |
| `include/lv_conf.h` | LVGL settings and fonts |

### Screenshots

With the board connected, `tools/screenshot.py` saves the screen as a PNG
(it needs pyserial):

```sh
python3 tools/screenshot.py /dev/cu.usbmodem* screen.png      # current tab
python3 tools/screenshot.py /dev/cu.usbmodem* locator.png 2   # Locator tab
```
