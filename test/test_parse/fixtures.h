// /api/live and /api/status from a real map (metarmap2 26.11.04), trimmed to a
// few LEDs and airports. Regenerate by hand if the API changes.
#pragma once

static const char* kLiveJson = R"json({
 "now": 1793913194.7729845,
 "brightness": 0.51,
 "visualizer": {
  "active": 10,
  "id": "dewpoint_spread",
  "name": "Dewpoint Spread",
  "exclusive": false,
  "areas": null,
  "autoRotate": {
   "enabled": true,
   "seconds": 30,
   "next": 1793913204.834544
  }
 },
 "strip": null,
 "leds": [
  {
   "code": "KVCT",
   "pixel": 0,
   "name": "Victoria Rgnl",
   "virtual": false,
   "mock": false,
   "lat": 28.8624,
   "lon": -96.9301,
   "flightCategory": "VFR",
   "raw": "METAR KVCT 052051Z 12008KT 10SM FEW034 28/19 A2987 RMK AO2 SLP112 T02780189 56022",
   "lightning": false,
   "look": {
    "effect": "solid",
    "colors": [
     "#ffffff"
    ]
   },
   "wind": null
  },
  {
   "code": "KELA",
   "pixel": 11,
   "name": "Eagle Lake Arpt",
   "virtual": false,
   "mock": false,
   "lat": 29.602,
   "lon": -96.3239,
   "flightCategory": "VFR",
   "raw": "METAR KELA 052015Z AUTO 18005KT 10SM CLR 28/15 A2987 RMK AO2 LTG DSNT N T02760151",
   "lightning": true,
   "look": {
    "effect": "cycle",
    "colors": [
     "#ffff00",
     "#ffff00",
     "#ffff00",
     "#ffffff",
     "#ffffff",
     "#ffff00",
     "#ffff00",
     "#ffff00",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffff00",
     "#ffff00",
     "#ffff00",
     "#ffffff",
     "#ffffff",
     "#ffff00",
     "#ffff00",
     "#ffff00",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff",
     "#ffffff"
    ],
    "step": 0.05
   },
   "wind": null
  },
  {
   "code": "KDKR",
   "pixel": 17,
   "name": "Crockett",
   "virtual": false,
   "mock": false,
   "lat": 31.3097,
   "lon": -95.40391,
   "flightCategory": null,
   "raw": null,
   "lightning": false,
   "look": {
    "effect": "off",
    "colors": []
   },
   "wind": null
  },
  {
   "code": "KAUS",
   "pixel": 50,
   "name": "Austin/Bergstrom Intl",
   "virtual": true,
   "mock": false,
   "lat": 30.1831,
   "lon": -97.6806,
   "flightCategory": "VFR",
   "raw": "METAR KAUS 052053Z 23008G16KT 10SM FEW060 FEW250 28/13 A2986 RMK AO2 SLP105 T02830133 56025",
   "lightning": false,
   "look": {
    "effect": "solid",
    "colors": [
     "#ffffff"
    ]
   },
   "wind": null
  }
 ],
 "locate": {
  "pixel": 5,
  "at": 1793913184.0057988
 },
 "display": {
  "enabled": true,
  "on": true,
  "screen": {
   "kind": "airport",
   "airport": "KGLS",
   "title": "KGLS",
   "virtual": false,
   "badge": "VFR",
   "page": 2,
   "lines": [
    {
     "icon": "temp",
     "text": "24\u00b0C / 75\u00b0F"
    },
    {
     "icon": "dewpoint",
     "text": "17\u00b0C / 63\u00b0F"
    },
    {
     "icon": "date",
     "text": "14:52C 20:52Z"
    }
   ]
  }
 }
})json";

static const char* kStatusJson = R"json({
 "version": "26.11.04",
 "ip": "192.168.10.76",
 "brightness": 0.51,
 "visualizer": {
  "active": 10,
  "id": "dewpoint_spread",
  "name": "Dewpoint Spread",
  "exclusive": false,
  "description": "Display the temperature-dewpoint spread (fog risk): white when it's dry, red when fog is likely.",
  "legend": [
   {
    "label": "1\u00b0C or less: fog or low clouds likely",
    "detail": "Red",
    "colors": [
     "#ff0000"
    ],
    "effect": "solid",
    "period": null,
    "low": null
   },
   {
    "label": "2\u00b0C: fog or low clouds can form",
    "detail": "Orange",
    "colors": [
     "#ff7e00"
    ],
    "effect": "solid",
    "period": null,
    "low": null
   },
   {
    "label": "3-4\u00b0C: humid",
    "detail": "Yellow",
    "colors": [
     "#ffff00"
    ],
    "effect": "solid",
    "period": null,
    "low": null
   },
   {
    "label": "More than 4\u00b0C: dry",
    "detail": "White",
    "colors": [
     "#ffffff"
    ],
    "effect": "solid",
    "period": null,
    "low": null
   },
   {
    "label": "No temperature or dewpoint in the report",
    "detail": "Slow red blink",
    "colors": [
     "#ff0000"
    ],
    "effect": "blink",
    "period": 2.0,
    "low": null
   },
   {
    "label": "Lightning or a thunderstorm",
    "detail": "Two quick yellow flashes every 2 s over the airport's color",
    "colors": [
     "#5a5a5a",
     "#ffff00"
    ],
    "effect": "flash",
    "period": 2.0,
    "low": null
   },
   {
    "label": "METAR not available",
    "detail": "Off",
    "colors": [],
    "effect": "off",
    "period": null,
    "low": null
   }
  ]
 },
 "visualizers": [
  {
   "index": 0,
   "id": "flight_category",
   "name": "Flight Category",
   "exclusive": false
  },
  {
   "index": 1,
   "id": "wind",
   "name": "Wind",
   "exclusive": false
  },
  {
   "index": 2,
   "id": "wind_gusts",
   "name": "Wind Gusts",
   "exclusive": false
  },
  {
   "index": 3,
   "id": "wind_direction",
   "name": "Wind Direction",
   "exclusive": false
  },
  {
   "index": 4,
   "id": "pressure",
   "name": "Pressure",
   "exclusive": false
  },
  {
   "index": 5,
   "id": "precipitation",
   "name": "Precipitation",
   "exclusive": false
  },
  {
   "index": 6,
   "id": "temperature",
   "name": "Temperature",
   "exclusive": false
  },
  {
   "index": 7,
   "id": "visibility",
   "name": "Visibility",
   "exclusive": false
  },
  {
   "index": 8,
   "id": "density_altitude",
   "name": "Density Altitude",
   "exclusive": false
  },
  {
   "index": 9,
   "id": "ceiling",
   "name": "Ceiling",
   "exclusive": false
  },
  {
   "index": 10,
   "id": "dewpoint_spread",
   "name": "Dewpoint Spread",
   "exclusive": false
  },
  {
   "index": 11,
   "id": "advisories",
   "name": "Advisories",
   "exclusive": false
  },
  {
   "index": 12,
   "id": "tfr",
   "name": "TFR",
   "exclusive": false
  },
  {
   "index": 13,
   "id": "rainbow",
   "name": "Rainbow",
   "exclusive": true
  },
  {
   "index": 14,
   "id": "chase",
   "name": "Chase",
   "exclusive": true
  }
 ],
 "config": {
  "visualizer.active": "flight_category",
  "visualizer.auto_rotate.enabled": true,
  "visualizer.auto_rotate.delay_seconds": 30
 },
 "airports": [
  {
   "code": "KVCT",
   "pixel": 0,
   "text": "Victoria Rgnl",
   "display": false,
   "virtual": false,
   "mock": false,
   "flightCategory": "VFR",
   "raw": "METAR KVCT 052051Z 12008KT 10SM FEW034 28/19 A2987 RMK AO2 SLP112 T02780189 56022",
   "windDir": "120",
   "windSpeed": 8,
   "windGustSpeed": 0,
   "vis": 10,
   "obs": "",
   "tempC": 28,
   "dewpointC": 19,
   "altimHg": 29.87,
   "lightning": false,
   "obsTime": "2026-11-05T20:51:00+00:00"
  },
  {
   "code": "KPKV",
   "pixel": 1,
   "text": "Port Lavaca/Calhoun Cnty",
   "display": false,
   "virtual": false,
   "mock": false,
   "flightCategory": "VFR",
   "raw": "METAR KPKV 052055Z AUTO 10010KT 10SM FEW070 SCT130 26/21 A2985 RMK AO2 T02580210 56025",
   "windDir": "100",
   "windSpeed": 10,
   "windGustSpeed": 0,
   "vis": 10,
   "obs": "",
   "tempC": 26,
   "dewpointC": 21,
   "altimHg": 29.85,
   "lightning": false,
   "obsTime": "2026-11-05T20:55:00+00:00"
  }
 ]
})json";
