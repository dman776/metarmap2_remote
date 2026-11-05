# METARMap Remote

METARMap Remote is the idea for a companion interface to monitor and control a
METARMap2 installation from another device. It connects to the map's built-in
HTTP API, so the Raspberry Pi can continue to fetch weather data and run the
LEDs and optional OLED display locally.

> **Project status:** Firmware is set up for the ES3C28P touchscreen (see
> [Hardware](#hardware)) and can reach a map. The features below describe the
> intended direction; they are not yet implemented.

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
Arduino framework and [LovyanGFX](https://github.com/lovyan03/LovyanGFX) for
the display and touch.

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

The current firmware is a bring-up check: it draws colour bars, reports flash
and PSRAM, joins Wi-Fi, requests the map's `/api/status`, and plots touches.
If it can't see the configured network, it lists the networks it can see on
the serial console.
