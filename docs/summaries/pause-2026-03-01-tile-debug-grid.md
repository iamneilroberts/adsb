# Session Pause: Tile Debug → Vector Grid + Detail Card TODO
**Date:** 2026-03-01 late evening
**Repo:** /home/neil/dev/adsb
**Branch:** master
**Uncommitted changes:** yes

## What Was Accomplished
1. **WiFi confirmed working** — pioarduino 55.03.37 upgrade from last session fixed the netif crash. Board fetches 25-41 aircraft per poll cycle consistently.
2. **Tile rendering debugged extensively** — lv_draw_image on ESP32-P4/LVGL 9.5 produces corrupted output regardless of approach:
   - Manual `lv_image_dsc_t` with stride set → static
   - `lv_draw_buf_create` (proper LVGL API) → static
   - `esp_cache_msync` C2M flush → still static
   - Same-core execution (core 0) → still static
   - Manually constructed `lv_draw_buf_t` with aligned alloc → still static
   - Even solid color fill (memset) → still shows noise patterns
   - Conclusion: `lv_draw_image` is fundamentally broken on this ESP32-P4 + MIPI DSI + LVGL 9.5 combo, likely PPA (Pixel Processing Accelerator) related
3. **Replaced tiles with vector grid** — `draw_tiles()` → `draw_grid()` in map_view.cpp. Draws lat/lon gridlines using `lv_draw_line` (which works fine). Grid spacing auto-adjusts with zoom level.
4. **Disabled tile_cache_init** — commented out in main.cpp to save PSRAM and background task
5. **Tile cache still exists** in source for future revisit

## Key Technical Finding
`lv_draw_image` on ESP32-P4 with MIPI DSI display produces corrupted output for ANY pixel data passed via `lv_draw_buf_t` or `lv_image_dsc_t`. The corruption pattern (horizontal bands of correct data mixed with noise) persists regardless of:
- Cache coherency (same core, cache flush)
- Buffer allocation method (lv_draw_buf_create, heap_caps_malloc, aligned alloc)
- Pixel content (decoded PNG, solid color, memset zeros)
- Stride settings (auto, manual 512)

This is likely an ESP32-P4 PPA hardware accelerator issue with LVGL 9.5's image drawing pipeline. All other LVGL draw primitives (rect, line, arc, label) work perfectly. Future fix: investigate ESP32-P4 PPA configuration, or use lv_canvas widgets instead of lv_draw_image.

## Files Modified This Session
| File Path | Action | Description |
|-----------|--------|-------------|
| `src/ui/map_view.cpp` | Modified | Replaced `draw_tiles()` with `draw_grid()` vector lat/lon grid |
| `src/ui/tile_cache.cpp` | Modified | Various debug attempts, 48 cache slots, manual draw buf, cache flush |
| `src/ui/tile_cache.h` | Modified | Changed return type to `lv_draw_buf_t*` |
| `src/main.cpp` | Modified | Commented out `tile_cache_init()` |

## Git State
```
M  lv_conf.h
M  platformio.ini
M  src/config.h
M  src/data/fetcher.cpp
M  src/data/storage.cpp
M  src/main.cpp
M  src/ui/alerts.cpp
M  src/ui/map_view.cpp
M  src/ui/tile_cache.cpp
M  src/ui/tile_cache.h
?? .claude-sessions/
?? docs/summaries/
?? src/hal/lv_mem_psram.c
?? src/main.cpp.bak
?? src/main.cpp.full
```

## Remaining Work
1. **Verify vector grid looks good** — firmware just flashed, needs visual check
2. **Improve detail card** — User requested: "detail display should show as much information as possible, with option to fetch more detail from an internet source". Current card shows callsign, reg, type, altitude, speed, heading, vert rate, distance, squawk. Enrichment fetch from adsb.lol + planespotters.net exists but card is sparse. Need richer layout.
3. **Detail card keeps popping up** — User reported the detail card auto-shows on the arrivals board. May be a touch event issue (detail_card_show triggered unintentionally). Investigate touch handling.
4. **Commit all working changes** — 10+ modified files uncommitted
5. **Clean up temp files** — `src/main.cpp.bak`, `src/main.cpp.full`, `.claude-sessions/`
6. **Future: revisit tile rendering** — Try `lv_canvas` widget approach, or check if LVGL 9.6+ fixes ESP32-P4 PPA image rendering

## Open Questions
- [ ] Does the vector grid look OK on the display?
- [ ] What specific info should the detail card show? (currently: callsign, reg, type_code, altitude, speed, heading, vert_rate, distance, squawk, airline, route via enrichment)
- [ ] Touch sensitivity: is the detail card triggering too easily from the map?

## Instructions
Continue with remaining work. Start by verifying the vector grid display, then focus on improving the detail card with richer aircraft information and internet enrichment.
