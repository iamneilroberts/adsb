# Session Pause: Feature Plan Batch 1 + Bugfixes
**Date:** 2026-03-01 at ~12:30
**Repo:** /home/neil/dev/adsb
**Branch:** master
**Uncommitted changes:** yes (17 modified, 5 untracked)

## What Was Accomplished

### Phase 1 Complete (3 tasks)
1. **Detail card auto-popup bug fix** — Added `views_get_active_index()` to track active tile; guarded map click handler so it only fires on MAP view -> `src/ui/views.cpp`, `src/ui/views.h`, `src/ui/map_view.cpp`
2. **NVS settings persistence** — Replaced SD card stub with `Preferences.h` NVS wrapper. Extended `UserConfig` with cycle, trail, and zoom fields -> `src/data/storage.cpp`, `src/data/storage.h`
3. **Ghost aircraft persistence** — Rewrote `parse_aircraft_json` to merge in-place instead of replace. Added `stale_since` field with 30s linear fade. Applied opacity across all three views -> `src/data/aircraft.h`, `src/data/fetcher.cpp`, `src/ui/map_view.cpp`, `src/ui/radar_view.cpp`, `src/ui/arrivals_view.cpp`

### Bugfixes After Testing
4. **Radar blips vanishing** — Root cause: `PHOSPHOR_FADE_MS` (4s) < fetch interval (5s), so all blips went invisible between API calls. Fix: replaced time-based phosphor with **sweep-angle-based** brightness. Blips are always visible; brightest when just swept, dim as sweep moves away -> `src/ui/radar_view.cpp`
5. **Speed/heading all zeros** — ArduinoJson `obj["gs"] | 0` didn't auto-convert float→int16_t. Fixed by casting through float: `(int16_t)(obj["gs"] | 0.0f)`. Same for `track` and `baro_rate` -> `src/data/fetcher.cpp`
6. **Arrivals title overlapping column headers** — Title bar (HEADER_H=36) and column headers (y=22) shared the same space. Split into separate TITLE_H=30 and COL_HEADER_H=18. Column headers now positioned below title -> `src/ui/arrivals_view.cpp`
7. **Distance column too narrow** — Widened DIST from 4→5 chars, added decimal precision (`%5.1f` for <100nm). Shifted HDG and STATUS columns right -> `src/ui/arrivals_view.cpp`
8. **Laggy swipes** — All three view timers (map 1s, radar 33ms, arrivals 60ms) were redrawing even when not visible. Gated each timer with `views_get_active_index()` check -> all three view files

## Decisions Made
- **NVS over SD**: SD_MMC disabled due to SDIO pin conflicts. NVS (Preferences.h) is built into framework, zero dependencies, persists across reboots BECAUSE it's the standard ESP32 approach when SD is unavailable
- **Sweep-angle phosphor**: Radar blip brightness now based on angular distance from sweep line rather than time since last API call BECAUSE time-based approach fails when fetch interval exceeds fade duration
- **View timer gating**: Each view's periodic timer checks active index before invalidating BECAUSE during tileview swipe transitions, all tiles may be partially rendered, and unnecessary invalidations eat the entire frame budget
- **Float parsing for gs/track/baro_rate**: adsb.lol API returns these as JSON floats. ArduinoJson's `| 0` operator infers int default type and doesn't auto-convert BECAUSE the default value type drives the return type

## Files Created or Modified
| File Path | Action | Description |
|-----------|--------|-------------|
| `src/ui/views.h` | Modified | Added `views_get_active_index()`, `views_get_tileview()` declarations |
| `src/ui/views.cpp` | Modified | Added `_active_index` tracking in tileview_changed_cb, new getters |
| `src/data/storage.h` | Modified | Extended UserConfig with cycle, trail, zoom fields |
| `src/data/storage.cpp` | Modified | Full NVS implementation replacing SD card stub |
| `src/data/aircraft.h` | Modified | Added `stale_since` field, `compute_aircraft_opacity()` helper, `GHOST_TIMEOUT_MS` |
| `src/data/fetcher.cpp` | Modified | Merge-based parse_aircraft_json, float parsing for gs/track/baro_rate |
| `src/ui/map_view.cpp` | Modified | View guard on click handler, ghost opacity, timer gating |
| `src/ui/radar_view.cpp` | Modified | Sweep-angle phosphor, ghost opacity, timer gating |
| `src/ui/arrivals_view.cpp` | Modified | Ghost dimming, title/header layout fix, DIST column widened, timer gating |
| `src/ui/settings.cpp` | Modified | "NVS" log message instead of "SD card" |

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

## Remaining Work

### Awaiting User Test Feedback
- User is testing the bugfix flash. May need further tweaks before proceeding.

### Phase 2: Essential Features (Tasks 4-6)
1. **View cycle mode** (Task 4): New `src/ui/view_cycle.h/.cpp`. State machine CYCLING→PAUSED→CYCLING. LVGL timers for auto-advance (default 30s) and inactivity return (60s). Status bar icon. Settings panel controls. Uses `views_get_tileview()` already added.
2. **Military highlighting** (Task 5): Override color to orange for `is_military`, diamond shape (4 lines) in map `draw_aircraft()`. Expand ICAO military ranges in `check_military()` (currently only US + UK).
3. **Range/scale control** (Task 6): Map 5 zoom levels {100,50,20,10,5}nm with range badge. Radar tap-to-cycle zoom. Arrivals distance filter badge. Persist via NVS (fields already in UserConfig).

### Phase 3: Visual Polish (Tasks 7-9)
4. **Vector aircraft icons** (Task 7): New `src/ui/aircraft_icons.h/.cpp`. Category-based shapes (jet, prop, heli, military diamond, ground square). Replace dot rendering.
5. **Track display config** (Task 8): Settings controls for trail on/off, length, style (line vs dots). Persist via NVS.
6. **Improved detail card** (Task 9): 3-row layout with bearing, flight status, "seen Xs ago" timer, engine info from enrichment.

### Phase 4: New Visualization (Task 10)
7. **Statistics dashboard** (Task 10): 4th tile with altitude distribution, aircraft by type, hourly activity, session stats. New `src/ui/stats_view.h/.cpp`.

### Flash Port Note
- `/dev/ttyACM0` is the correct upload port (ESP32-P4 USB JTAG), not `/dev/ttyUSB0` (CP210x serial)

## Open Questions
- [ ] User testing bugfixes — may need further radar/arrivals tweaks
- [ ] No commits made yet — all work is uncommitted. Should commit Phase 1 + bugfixes before starting Phase 2?

## Instructions
Continue the work from this session. Start with the Remaining Work section.
Review git state to confirm nothing has changed since the handoff.
The full implementation plan is in the user's first message of the conversation.
Upload port for flashing: `--upload-port /dev/ttyACM0`
