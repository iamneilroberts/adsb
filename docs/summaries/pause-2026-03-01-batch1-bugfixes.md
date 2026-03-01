# Session Pause: Batch 1 + Bugfixes
**Date:** 2026-03-01 at ~13:00
**Repo:** /home/neil/dev/adsb
**Branch:** master
**Uncommitted changes:** yes (17 modified, 5 untracked)

## What Was Accomplished

### Phase 1 Complete (3 tasks)
1. **Detail card auto-popup bug fix** — Added `views_get_active_index()` to track active tile; guarded map click handler so it only fires on MAP view -> `src/ui/views.cpp`, `src/ui/views.h`, `src/ui/map_view.cpp`
2. **NVS settings persistence** — Replaced SD card stub with `Preferences.h` NVS wrapper. Extended `UserConfig` with cycle, trail, and zoom fields -> `src/data/storage.cpp`, `src/data/storage.h`
3. **Ghost aircraft persistence** — Rewrote `parse_aircraft_json` to merge in-place instead of replace. Added `stale_since` field with 30s linear fade. Applied opacity across all three views -> `src/data/aircraft.h`, `src/data/fetcher.cpp`, `src/ui/map_view.cpp`, `src/ui/radar_view.cpp`, `src/ui/arrivals_view.cpp`

### Bugfixes After Hardware Testing (2 rounds)

**Round 1:**
4. **Radar blips vanishing** — `PHOSPHOR_FADE_MS` (4s) < fetch interval (5s) caused all blips to go invisible between API calls. Replaced time-based phosphor with **sweep-angle-based** brightness. Blips always visible; brightest when just swept, dim as sweep moves away -> `src/ui/radar_view.cpp`
5. **Speed/heading all zeros** — ArduinoJson `obj["gs"] | 0` didn't auto-convert float→int16_t. Fixed with `(int16_t)(obj["gs"] | 0.0f)`. Same for `track` and `baro_rate` -> `src/data/fetcher.cpp`
6. **Arrivals title overlapping column headers** — Split HEADER_H into TITLE_H (30) + COL_HEADER_H (18). Column headers now below title -> `src/ui/arrivals_view.cpp`
7. **Distance column too narrow** — Widened DIST from 4→5 chars, added decimal precision (`%5.1f`). Shifted HDG/STATUS right -> `src/ui/arrivals_view.cpp`
8. **Laggy swipes** — Gated all three view timers (map 1s, radar 33ms, arrivals 60ms) with `views_get_active_index()` check -> all three view files

**Round 2:**
9. **Arrivals garbled characters** — Flip animation rolls (3-6 random chars at 60ms each) caused persistent garbage because data updates every 2s shift digits constantly. Fix: incremental digit changes now update **instantly** (no roll), full flip animation only for new aircraft appearing in a row -> `src/ui/arrivals_view.cpp`
10. **Map altitude color legend** — Added bottom-left altitude key: GND/<5k/<15k/<25k/<35k/35k+ with color swatches -> `src/ui/map_view.cpp`

## Decisions Made
- **NVS over SD**: SD_MMC disabled due to SDIO pin conflicts. NVS is built-in, zero dependencies
- **Sweep-angle phosphor**: Radar blip brightness based on angular distance from sweep line, not time since API call
- **View timer gating**: Each view's periodic timer checks active index before invalidating to save frame budget during swipes
- **Instant vs animated flips**: Only new aircraft get flip animation; incremental data changes (speed/dist/hdg ticking) update instantly to avoid persistent garbled display
- **No historical tracks**: adsb.lol API is real-time only. Trails build from system start (1 point per 5s fetch, 60-point max)

## Files Created or Modified
| File Path | Action | Description |
|-----------|--------|-------------|
| `src/ui/views.h` | Modified | Added `views_get_active_index()`, `views_get_tileview()` |
| `src/ui/views.cpp` | Modified | Active index tracking, new getters |
| `src/data/storage.h` | Modified | Extended UserConfig with cycle/trail/zoom fields |
| `src/data/storage.cpp` | Modified | Full NVS implementation replacing SD stub |
| `src/data/aircraft.h` | Modified | `stale_since` field, `compute_aircraft_opacity()`, `GHOST_TIMEOUT_MS` |
| `src/data/fetcher.cpp` | Modified | Merge-based parsing, float casts for gs/track/baro_rate |
| `src/ui/map_view.cpp` | Modified | View guard, ghost opacity, timer gating, altitude legend |
| `src/ui/radar_view.cpp` | Modified | Sweep-angle phosphor, ghost opacity, timer gating |
| `src/ui/arrivals_view.cpp` | Modified | Layout fix, DIST widened, ghost dimming, instant digit updates |
| `src/ui/settings.cpp` | Modified | "NVS" log message |

## Git State
```
 M lv_conf.h
 M platformio.ini
 M src/config.h
 M src/data/fetcher.cpp
 M src/data/storage.cpp
 M src/main.cpp
 M src/ui/alerts.cpp
 M src/ui/map_view.cpp
 M src/ui/tile_cache.cpp
 M src/ui/tile_cache.h
 (plus aircraft.h, storage.h, views.cpp, views.h, radar_view.cpp, arrivals_view.cpp, settings.cpp)
?? .claude-sessions/
?? docs/summaries/
?? src/hal/lv_mem_psram.c
?? src/main.cpp.bak
?? src/main.cpp.full
```

## Recent Commits
```
cd5382d fix: resolve build errors for LVGL 9.5 and RISC-V toolchain
88f6693 feat: OpenStreetMap tile cache and rendering
b71801e feat: settings screen with SD card persistence
f03bd56 feat: alert system with toast notifications
12394a1 feat: detail card with enrichment data and slide animation
```

## Remaining Work — User Requested
These came from live testing feedback and should be done BEFORE continuing with the feature plan:

1. **Arrivals sort options** — Add tap-to-cycle sort on column headers (by distance, altitude, callsign, speed). Currently rows are in API response order. Need a sort function + a sort mode indicator.
2. **Radar contacts persist longer** — Currently all blips appear and disappear at the same rate on each sweep. The sweep-angle phosphor makes them ALL bright when swept and ALL dim at the same angle behind the sweep. Need to make blips persist at a visible brightness regardless of sweep position, with just a brightness *boost* when the sweep passes. Think: always-visible dots with a bright flash as sweep crosses them.

### Feature Plan (Phases 2-4) — After Bugfixes Above
3. **Phase 2.1**: View cycle mode — `src/ui/view_cycle.h/.cpp`
4. **Phase 2.2**: Military highlighting — diamond shapes, expanded ICAO ranges
5. **Phase 2.3**: Range/scale control — 5 map zooms, radar tap-zoom, arrivals distance filter
6. **Phase 3.1**: Vector aircraft icons by type — `src/ui/aircraft_icons.h/.cpp`
7. **Phase 3.2**: Track display config — settings for trail on/off, length, style
8. **Phase 3.3**: Improved detail card — 3-row layout, bearing, flight status, "seen Xs ago"
9. **Phase 4.1**: Statistics dashboard — 4th tile with charts and session stats

## Open Questions
- [ ] All work is uncommitted (17 files). Should commit before continuing.
- [ ] Upload port: `/dev/ttyACM0` (not ttyUSB0)

## Instructions
Continue the work from this session. Start with the two user-requested fixes (arrivals sort, radar persistence) in Remaining Work, then proceed to the feature plan phases.
The full implementation plan is in the user's first message of the prior conversation.
Upload port for flashing: `--upload-port /dev/ttyACM0`
