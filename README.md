# METARMap Remote

METARMap Remote is the idea for a companion interface to monitor and control a
METARMap2 installation from another device. It connects to the map's built-in
HTTP API, so the Raspberry Pi can continue to fetch weather data and run the
LEDs and optional OLED display locally.

> **Project status:** This repository currently contains a project concept, not
> a working application. The features below describe the intended direction;
> they are not yet implemented.

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

## Development

There is no application code or installation process in this repository yet.
Once implementation begins, this section should document the chosen runtime,
setup instructions, configuration, and tests.
