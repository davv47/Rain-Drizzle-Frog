# Rain Drizzle Frog

A Pebble watchface featuring a pixel-art frog that reacts to the weather
by changing outfits, and sits in increasing amounts of water based on the
day's forecasted precipitation.

## Features

- **Pixel-art frog** drawn entirely in C (no image assets needed)
- **5 outfits** mapped to live weather conditions:
  - Naked — mild / partly cloudy
  - Swimsuit + goggles — hot and sunny (WMO 0, temp >= 25 C)
  - Raincoat + boots — rain / drizzle / storms
  - Scarf + umbrella — cold / overcast / fog
  - Snowsuit — snow
- **5 water levels** based on daily precipitation forecast:
  - Dry (0 mm)
  - Ankle (< 2 mm)
  - Knee (2–10 mm)
  - Waist (10–25 mm)
  - Submerged (25+ mm)
- **Background scene** reacts to weather: clear sky, sun + rays,
  clouds, raindrops, snowflakes, lightning bolt
- **Time** (large, top), **date**, and **precipitation amount + label**
- Supports **Pebble Time Steel** (144x168) and **Pebble Time Round** (chalk)
- Full colour on Pebble Time; graceful greyscale fallback on aplite

## Weather data

Uses [Open-Meteo](https://open-meteo.com/) — free, no API key needed.
Fetches `current_weather` (WMO code + temperature) and
`daily.precipitation_sum` once per hour via the JS layer.

## AppMessage keys

| Key | Name          | Type | Description                        |
|-----|---------------|------|------------------------------------|
| 0   | WMO_CODE      | int  | Current WMO weather code           |
| 1   | TEMPERATURE   | int  | Temperature in degrees C           |
| 2   | PRECIP_MM10   | int  | Daily precip * 10 (avoids floats)  |
| 3   | FETCH_TIME    | int  | Unix timestamp of last fetch       |

## Project layout

```
rain_drizzle_frog/
  appinfo.json
  package.json
  src/
    main.c
    js/
      index.js
```

## CloudPebble setup

1. Create a new project in CloudPebble (type: Pebble C SDK)
2. Copy `appinfo.json` contents into the CloudPebble project settings
3. Add `src/main.c` as a C source file
4. Add `src/js/index.js` as the PebbleKit JS file
5. Build and install via the Pebble app

> Note: CloudPebble expects a `master` branch if syncing from GitHub.
