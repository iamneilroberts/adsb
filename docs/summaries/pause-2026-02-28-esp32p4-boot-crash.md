# Session Pause: ESP32-P4 Boot Crash Fix
**Date:** 2026-02-28 at 20:15
**Branch:** master
**Uncommitted changes:** yes (5 modified files)

## What Was Accomplished
1. Fixed `strlcpy` calls in `src/data/fetcher.cpp` and `src/ui/alerts.cpp` — replaced with `strncpy` + null termination (RISC-V newlib doesn't have `strlcpy`)
2. Guarded `SD_MMC.begin()` in `src/ui/tile_cache.cpp` with `#if 0` — prevents SDIO pin conflicts on JC1060
3. Increased FreeRTOS task stack sizes from 16384 to 32768 in both `src/data/fetcher.cpp` and `src/ui/tile_cache.cpp`
4. Reordered init in `src/main.cpp` — moved `fetcher_init()` and `tile_cache_init()` AFTER all LVGL UI setup
5. Changed LVGL memory allocator in `lv_conf.h` from `malloc`/`free` to `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` — internal RAM (500KB) exhausted by MIPI DSI driver
6. All builds succeeded (pio run)

## What Did NOT Work
- **None of these fixes resolved the crash.** The device still shows the exact same `lv_realloc: couldn't reallocate memory lv_mem.c:162` followed by `Guru Meditation Error: Core 1 panic'ed (Store access fault)`.
- The ELF SHA on the device stayed `2811fe2d7` despite successful esptool flashing — suggesting the flash writes were not taking effect OR the bootloader was loading from a different location.
- Attempted full erase (`erase_flash`) + merged binary flash (bootloader + partitions + firmware at 0x0). **This bricked the device** — it no longer boots at all (no serial output, no cyan screen).

## Root Cause Still Unknown
The `lv_realloc` failure happens at LVGL timestamp `(0.000, +0)` — the very first LVGL allocation after `lv_init()`. The PSRAM redirect should have fixed this but the new firmware never actually ran on the device. The core mystery is **why esptool reports successful flash + verification but the device runs old firmware**.

Possible explanations:
- ESP32-P4 bootloader offset may not be 0x0 (we guessed; might need 0x2000 or other offset)
- The partition table format or offsets may differ for ESP32-P4 vs standard ESP32
- The JC1060 board may have a different flash layout than the generic esp32-p4-evboard

## Decisions Made
- LVGL memory → PSRAM: changed `malloc` to `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` BECAUSE internal RAM is only 500KB and the MIPI DSI DPI driver consumes most of it
- SD_MMC disabled: `#if 0` guard BECAUSE failed `SD_MMC.begin()` on boards without SD card can corrupt SDIO/memory state
- Init reorder: background tasks after UI BECAUSE tasks on Core 1 were triggering LVGL allocations before UI objects existed

## Files Created or Modified
| File Path | Action | Description |
|-----------|--------|-------------|
| `lv_conf.h` | Modified | LVGL allocator → PSRAM via heap_caps_malloc |
| `src/data/fetcher.cpp` | Modified | strlcpy→strncpy fix, stack 16K→32K |
| `src/ui/alerts.cpp` | Modified | strlcpy→strncpy fix |
| `src/ui/tile_cache.cpp` | Modified | SD_MMC guarded, stack 16K→32K |
| `src/main.cpp` | Modified | Reordered init: UI before background tasks |

## Git State
```
 M lv_conf.h
 M src/data/fetcher.cpp
 M src/main.cpp
 M src/ui/alerts.cpp
 M src/ui/tile_cache.cpp
?? .claude-sessions/
?? docs/schema/
?? docs/summaries/
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

### Immediate: Recover the Bricked Device
1. **Find correct ESP32-P4 flash layout** — research the Guition JC1060 or ESP32-P4-EVBoard bootloader offset (may be 0x2000 not 0x0). Check PlatformIO board JSON at `~/.platformio/platforms/*/boards/esp32-p4-evboard.json`
2. **Re-flash with correct offsets** — need bootloader.bin, partitions.bin, and firmware.bin at their correct offsets, OR find the original factory firmware for the JC1060
3. **Consider flashing factory demo first** — if available from Guition, this proves the board hardware is fine

### Then: Minimal Boot Test (User's Suggestion)
4. **Write a bare minimum main.cpp** — just LCD init + `lv_init()` + single label "Hello" + `lv_timer_handler()` loop. No WiFi, no fetcher, no tile cache, no alerts. If THIS crashes, the problem is in the LCD/LVGL init itself.
5. **Add features one at a time** — once minimal boots, add back: status bar → views → fetcher → tile cache → alerts → settings

### Debug the lv_realloc Mystery
6. **Add heap diagnostics before lv_init()** — print `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` and `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` to confirm PSRAM is actually accessible
7. **Check if LVGL 9.5 macros work as expected** — verify `LV_MEM_CUSTOM_ALLOC(size)` macro form is compatible with LVGL 9.5's `lv_conf_internal.h` wiring (might need function wrappers instead of macros)

## Open Questions
- [ ] What are the correct flash offsets for ESP32-P4 bootloader? — impacts ability to flash at all
- [ ] Does the JC1060 have factory firmware that can be restored? — impacts recovery path
- [ ] Is PSRAM actually initialized before lv_init()? — impacts whether PSRAM allocator can work
- [ ] Should we use PlatformIO's `pio run -t upload` workflow instead of manual esptool? — the mklittlefs error needs fixing (`mkdir -p ~/.espressif/tools/tool-mklittlefs` might work)

## Flashing Notes
- Device uses `/dev/ttyACM0` (USB-JTAG/Serial, not UART bridge)
- Enter download mode: unplug USB, hold BOOT button, plug in, release BOOT
- The crash loop makes auto-reset unreliable — always use `--before no_reset` with manual BOOT mode
- esptool baud: 460800 works for flashing
- Serial monitor: 115200 baud

## Instructions
Continue the work from this session. Start with the Remaining Work section.
Review git state to confirm nothing has changed since the handoff.
**Priority 1 is recovering the device from the bricked state.**
The user wants a minimal-first approach: get SOMETHING booting, then add features incrementally.
