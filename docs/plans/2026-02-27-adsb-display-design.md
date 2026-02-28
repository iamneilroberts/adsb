# ADS-B Display Design: ESP32-P4 7" Touchscreen

**Date:** 2026-02-27
**Status:** Approved

## Summary

A multi-view ADS-B aircraft tracker running on the GUITION JC1060P470C (ESP32-P4 + 7" 1024x600 IPS touchscreen), built with Arduino + LVGL 9. Pulls aircraft data from the free adsb.lol API and enriches it with photos, route info, and aircraft details on-demand. Three swipeable views (Map, Radar, List) plus a Detail Card overlay and passive alert system.

## Hardware

- **Board:** GUITION JC1060P470C-I_W_Y
- **SoC:** ESP32-P4 — dual-core RISC-V @ 400MHz, 32MB PSRAM, 16MB flash
- **Display:** 7" IPS, 1024x600, MIPI-DSI (JD9165 driver), capacitive multi-touch (GT911)
- **WiFi:** ESP32-C6-MINI-1U-N4 co-processor (WiFi 6, BLE 5) via SDIO
- **Storage:** MicroSD card slot (4-bit SDMMC)
- **Other:** USB-C (debug + OTG), speaker connector, camera connector, RTC

## Framework

- **Arduino-ESP32** v3.1+ with vendor MIPI-DSI BSP
- **LVGL 9.2** for all UI rendering
- **ArduinoJson 7.x** for API response parsing
- **Language:** C++ (Arduino)

## Data Layer

### Primary API: adsb.lol

- Endpoint: `https://api.adsb.lol/v2/point/{lat}/{lon}/{radius}`
- Free, no API key, no rate limits, unfiltered (includes military/government)
- Poll interval: 5 seconds
- Returns: ICAO hex, callsign, registration, aircraft type, lat/lon, altitude, speed, heading, vertical rate, squawk, ground status, and more

### Enrichment APIs (on-demand, when user taps an aircraft)

- **Aircraft photos:** Planespotters.net API — lookup by ICAO hex or registration
- **Route/flight info:** adsb.lol `/api/0/routeset` — origin/destination airports
- **Aircraft type database:** Embedded lookup table in flash (type code to manufacturer, model, engine info)

### Local Storage (SD Card)

- Map tile cache: `/tiles/{z}/{x}/{y}.png`
- Aircraft type database: ~5KB JSON
- User config: home lat/lon, radius, WiFi credentials, watchlist, preferences
- Optional: pre-downloaded airline logo sprites

### Aircraft Data Structure (RAM)

```
Max ~200 aircraft entries, each containing:
  - icao_hex, callsign, registration
  - lat, lon, altitude, speed, heading, vert_rate
  - squawk, aircraft_type, on_ground
  - trail[60] — last 5 min of positions (one per 5s poll)
  - last_seen timestamp
  - flags: military, emergency, watched
```

## Views

All views share a top status bar (WiFi signal, aircraft count, last update time, view indicator dots) and support swipe left/right to switch views.

### View 1: Map View (MVP)

- Tile map background from OpenStreetMap, cached to SD card
- Aircraft as arrow icons colored by altitude (green -> yellow -> red)
- Trail lines (last 5 min) behind each aircraft, altitude-colored
- Toggleable callsign labels
- Tap aircraft -> Detail Card slides up from bottom
- Pinch to zoom: 3 levels (50nm, 20nm, 5nm radius)
- Drag to pan away from home center
- Double-tap empty space -> re-center on home
- Home position marker (crosshair/house icon)
- Range rings (subtle concentric circles)

### View 2: Radar View

- Black background with green/amber phosphor aesthetic
- Rotating sweep line (one rotation per 5 seconds, synced to data refresh)
- Aircraft as bright dots with phosphor persistence (fade over time)
- Range rings and compass cardinal points (N/S/E/W)
- Monospace radar-style font for callsigns
- Tap aircraft -> Detail Card
- Minimal UI — ambient/art mode

### View 3: Arrivals Board View

Split-flap / Solari airport departures board aesthetic:

- Black background with rows of "flip tiles" — each character in its own rounded-rect cell
- Monospace typeface (bold, condensed, white/yellow on dark grey tiles)
- Columns: FLIGHT (callsign), TYPE, ALT, SPD, DIST, HEADING, STATUS
- STATUS column: "CLIMB" / "DESCEND" / "CRUISE" / "GROUND" derived from vertical rate
- New aircraft animate in with a split-flap "rolling" effect (characters cycle rapidly then settle)
- Updated values also flip-animate when they change
- Subtle mechanical click sound effect on character changes (optional, via speaker)
- Color accents: yellow text on dark tiles (classic Solari), red for emergency squawk, amber for military
- Tap any row -> Detail Card
- Touch-scrollable with momentum
- Header bar styled as airport terminal signage: "OVERHEAD TRAFFIC" with location code

### Detail Card (bottom sheet overlay, any view)

Slides up covering ~60% of screen on aircraft tap:

- Header: callsign + registration + country flag
- Aircraft photo (fetched from Planespotters.net, cached)
- Aircraft info: type, manufacturer, engine type, airline/operator logo
- Flight info: origin -> destination airports (if route available)
- Live data: altitude, speed, vertical rate, heading, squawk, distance
- Trail mini-map: small rendering of this aircraft's recent path
- Swipe down or tap outside to dismiss

### Alert System (passive, all views)

- Military aircraft detection: ICAO hex prefix matching against known military ranges
- Emergency squawk: 7500 (hijack), 7600 (comms failure), 7700 (emergency) -> red banner
- Watchlist: user-configured tail numbers / ICAO hex codes -> blue highlight + notification
- Interesting aircraft: flag unusual types (A380, 747, C-17, etc.)
- Toast banner notifications (auto-dismiss 10s, or tap to open Detail Card)

## Technical Architecture

### FreeRTOS Task Model

| Task | Core | Priority | Purpose |
|------|------|----------|---------|
| LVGL UI | Core 0 | High | Render views, handle touch, animations |
| Data Fetcher | Core 1 | Medium | Poll adsb.lol every 5s, parse JSON, update aircraft array |
| Enrichment Fetcher | Core 1 | Low | Fetch photos, route info on user tap |
| Tile Fetcher | Core 1 | Low | Download and cache map tiles as needed |

Shared aircraft array protected by mutex (brief lock for updates/reads).

### Map Tile Strategy

- Source: OpenStreetMap `tile.openstreetmap.org/{z}/{x}/{y}.png` (256x256 PNG)
- Zoom levels: 3 cached levels matching views (~z8, z10, z13)
- Cache: SD card at `/tiles/{z}/{x}/{y}.png`
- Pre-seed option: bulk download tiles for your area, copy to SD
- Fallback: simple vector outlines (coastlines, runways) from embedded data
- RAM: LRU cache of ~20 decoded tiles in PSRAM (~2.5MB)

### LVGL Configuration

- LVGL 9.2 via Arduino library
- Display: MIPI-DSI via vendor BSP
- Touch: GT911 via I2C (SDA=GPIO7, SCL=GPIO8)
- Color depth: RGB565 (16-bit)
- Double buffered: two framebuffers in PSRAM (~1.2MB each)
- Target: ~30fps
- Theme: dark base, custom-styled per view

### WiFi

- ESP32-C6 co-processor manages WiFi over SDIO (ESP-Hosted)
- Hardcoded SSID/password initially
- Future: WiFi Manager captive portal for setup
- Auto-reconnect with backoff

### Settings Screen

Accessible via gear icon on status bar:

- Home lat/lon
- WiFi SSID/password
- Default radius
- Units (imperial/metric)
- Watchlist management
- Alert preferences

## Build Order (incremental)

1. **Board bring-up:** Display init, touch working, LVGL hello world
2. **Data fetcher:** WiFi connect, poll adsb.lol, parse JSON, populate aircraft array
3. **Map View (basic):** Render aircraft positions on solid background with arrows and labels
4. **Map tiles:** Fetch, cache, and render OSM tiles as background
5. **Map interaction:** Touch to select aircraft, zoom levels, pan, re-center
6. **Detail Card:** Bottom sheet with aircraft info, photo fetch
7. **Radar View:** Sweep animation, phosphor persistence, radar aesthetic
8. **List View:** Sortable table, row selection
9. **Alert system:** Military detection, emergency squawk, watchlist, toast notifications
10. **Settings screen:** Configuration UI
11. **Polish:** Animations, transitions, error handling, offline graceful degradation

## Inspirations / References

- ThomDyson/ESP32_adsb_display — original reference (4.3" WaveShare, basic map + sidebar)
- tar1090 — de facto web frontend (map, trails, altitude colors, filtering)
- skies-adsb — 3D aircraft visualization ("virtual aquarium" aesthetic)
- retro-adsb-radar — radar sweep with phosphor persistence
- rzeldent/esp32-flightradar24-ttgo — LVGL-based ESP32 with airline logos and photos
- viz1090 — SDL2-based map with Natural Earth + airport data

## What Makes This Different

No existing project combines:
- 7" touchscreen with multi-view swipeable UI
- LVGL on ESP32-P4 hardware for ADS-B
- Map + Radar + List views in one device
- On-demand aircraft photo/route enrichment
- Passive military/emergency/watchlist alerts
- Cached tile maps with vector fallback on a microcontroller
