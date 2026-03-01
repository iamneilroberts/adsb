# Session Pause: ESP32-P4 WiFi SDIO Fix + Framework Upgrade
**Date:** 2026-03-01 at ~evening
**Repo:** /home/neil/dev/adsb
**Branch:** master
**Uncommitted changes:** yes

## What Was Accomplished
1. **Diagnosed WiFi `netif_add` crash root cause** — The Arduino-ESP32 3.1.3 framework has a bug in `WiFiGeneric.cpp:wifiLowLevelInit()` where it creates both AP and STA netifs via `esp_netif_create_default_wifi_*()`, but on ESP32-P4 with `esp_wifi_remote` (SDIO to ESP32-C6 slave), the netif lifecycle is managed differently by the SDIO transport, causing duplicate `netif_add` assertions in lwIP.
2. **Confirmed WiFi radio works** — Through debug event logging, confirmed the ESP32-P4 successfully connects to the AP (`STA_CONNECTED` event fires). The issue is purely in the netif/lwIP layer, not the radio.
3. **Upgraded pioarduino from 53.03.13 to 55.03.37** — This upgrades Arduino-ESP32 from 3.1.3 to 3.3.x which reportedly fixes the hosted WiFi netif issue. Build succeeded. -> `platformio.ini`
4. **Reverted all framework patches** — `WiFiGeneric.cpp` is back to stock (clean framework).
5. **Reverted fetcher.cpp to clean Arduino WiFi API** — No more ESP-IDF direct calls or event handlers. Simple `WiFi.mode(WIFI_STA)` + `WiFi.begin()`. -> `src/data/fetcher.cpp`
6. **Set WiFi credentials and home coordinates** -> `src/config.h`
   - SSID: `ATTGpyJKmS`, Pass: `cw7nqpsr?sun`
   - HOME_LAT: 30.6905, HOME_LON: -88.1632

## Decisions Made
- **Framework upgrade over patching**: Tried multiple framework patches (skip netifs, lookup existing netifs, create STA-only, create after connect) — none worked because the ESP32-P4 esp_hosted SDIO transport manages netifs in a fundamentally different way than standard ESP32. Upgrading to 3.3.x is the proper fix.
- **WiFi credentials hardcoded for testing**: Baked SSID/password into `config.h` for faster iteration. Will need settings screen integration later.
- **esptool.py path**: The upgraded esptool (v5.1.2) requires `rich_click` module. Use `pio run -t upload` instead of calling esptool.py directly for flashing.

## Files Created or Modified
| File Path | Action | Description |
|-----------|--------|-------------|
| `platformio.ini` | Modified | Upgraded pioarduino from 53.03.13 to 55.03.37 |
| `src/config.h` | Modified | Set WiFi creds (ATTGpyJKmS) and home coords (30.6905, -88.1632) |
| `src/data/fetcher.cpp` | Modified | Clean Arduino WiFi API (WiFi.mode + WiFi.begin), reverted from ESP-IDF direct calls |
| `lv_conf.h` | Modified | LV_STDLIB_CUSTOM for PSRAM allocation (from previous session) |
| `src/main.cpp` | Modified | All UI init enabled, fetcher_init enabled, tile_cache_init commented out |
| `src/ui/alerts.cpp` | Modified | Null guard for _queue_mutex (from previous session) |
| `src/ui/tile_cache.cpp` | Modified | Null guards for FreeRTOS primitives (from previous session) |
| `src/data/storage.cpp` | Modified | SD_MMC disabled, returns compiled defaults (from previous session) |

## Git State
```
M  lv_conf.h
M  platformio.ini
M  src/config.h
M  src/data/fetcher.cpp
M  src/data/storage.cpp
M  src/main.cpp
M  src/ui/alerts.cpp
M  src/ui/tile_cache.cpp
?? .claude-sessions/
?? docs/summaries/
?? src/main.cpp.bak
```

## Recent Changes
```
 lv_conf.h             | 17 +++++--------
 platformio.ini        |  2 +-
 src/config.h          |  8 +++----
 src/data/fetcher.cpp  | 26 ++++++++++++--------
 src/data/storage.cpp  | 66 ++++------
 src/main.cpp          | 39 +++++++++++++++
 src/ui/alerts.cpp     |  7 ++++--
 src/ui/tile_cache.cpp | 13 ++++++++--
 8 files changed, 83 insertions(+), 95 deletions(-)
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
1. **Next: Flash and test the upgraded firmware** — The firmware with pioarduino 55.03.37 (Arduino-ESP32 3.3.x) was built successfully and flashed via `pio run -t upload`. Need to monitor serial output to see if the `netif_add` crash is resolved. Use `pio device monitor` or `timeout 60 cat /dev/ttyACM0`. The board should auto-boot after the upload (no need for BOOT button). If serial shows no output, the board may need a manual reset button press.
2. **If WiFi works**: Verify HTTP fetch succeeds — should see "Fetched N aircraft" in serial. Check that the status bar WiFi indicator turns green and aircraft count updates.
3. **If WiFi still crashes**: Try Ethernet as fallback. The board may have an Ethernet port (user said it does). Research showed ESP32-P4 uses IP101 PHY with ETH.h library. Pins: MDC=31, MDIO=52, Power=51, PHY addr 1, CLK mode EMAC_CLK_EXT_IN.
4. **Enable tile_cache_init** — Currently commented out in `src/main.cpp:163`. Enable after networking works.
5. **Commit all working changes** — 8 modified files need to be committed.
6. **Clean up**: Remove `src/main.cpp.bak`, `.claude-sessions/` directory.

## Key Technical Findings (WiFi Debug)
- `esp_wifi_set_mode()`, `set_config()`, `start()`, `connect()` all return `ESP_OK` on the SDIO path
- `WIFI_EVENT_STA_START` (id=2) fires after `esp_wifi_start()`
- `WIFI_EVENT_STA_CONNECTED` (id=4) fires — radio connects to AP successfully
- `WIFI_EVENT_STA_DISCONNECTED` (id=5) with reason 208 (timeout) and 205 (connection fail) before eventual connection
- At STA_CONNECTED time: `esp_netif_next()` returns 0 netifs — SDIO transport doesn't create any
- Creating STA netif after connect: DHCP fails, then crash in `sdio_process_rx` (null function pointer at MEPC=0x00000000)
- Framework's `wifiLowLevelInit()` creates netifs at init time, but SDIO transport later tries to add its own → `netif_add` assert
- The entire netif lifecycle on ESP32-P4 with esp_hosted is fundamentally different from standard ESP32

## Flashing Notes
- **Use PlatformIO upload**: `pio run -e jc1060 -t upload --upload-port /dev/ttyACM0` (handles esptool dependencies)
- **Old esptool path**: `/home/neil/.platformio/packages/tool-esptoolpy/esptool.py` — now v5.1.2, needs `rich_click` module (not installed system-wide)
- **Download mode**: Hold BOOT button, press RESET, release BOOT (only needed if `pio upload` can't auto-reset)

## Open Questions
- [ ] Does pioarduino 55.03.37 fix the WiFi netif crash? — Need to test (firmware already flashed)
- [ ] Does the JC1060 actually have Ethernet? — User says yes, but research suggests the standard JC1060P470 doesn't have an RJ45 port. May be a different variant or add-on.
- [ ] WiFi credentials in config.h — Should be moved to settings/NVS for production

## Instructions
Continue the work from this session. Start with the Remaining Work section.
Review git state to confirm nothing has changed since the handoff.
The firmware with the upgraded framework is already flashed — just need to monitor serial output.
