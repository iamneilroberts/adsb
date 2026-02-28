# ADS-B Display Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a multi-view ADS-B aircraft tracker on the GUITION JC1060P470C (ESP32-P4, 7" 1024x600 touchscreen) with Map, Radar, and Arrivals Board views plus enrichment and alerts.

**Architecture:** Arduino + LVGL 9.2 on PlatformIO (pioarduino fork). Single LVGL tileview with swipeable views. Background FreeRTOS task polls adsb.lol API every 5s. Enrichment (photos, routes) fetched on-demand. Map tiles cached to SD card.

**Tech Stack:** PlatformIO + pioarduino, Arduino-ESP32 v3.1+, LVGL 9.2, ArduinoJson 7.x, vendor MIPI-DSI/GT911 drivers (bundled source)

**Design doc:** `docs/plans/2026-02-27-adsb-display-design.md`

---

## Task 1: Project Scaffold & Board Bring-Up

**Goal:** PlatformIO project compiles, flashes, and shows LVGL "Hello World" on the 7" screen with working touch.

**Files:**
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Create: `src/pins_config.h`
- Create: `src/hal/jd9165_lcd.h` (copy from vendor)
- Create: `src/hal/jd9165_lcd.cpp` (copy from vendor)
- Create: `src/hal/esp_lcd_jd9165.h` (copy from vendor)
- Create: `src/hal/esp_lcd_jd9165.c` (copy from vendor)
- Create: `src/hal/gt911_touch.h` (copy from vendor)
- Create: `src/hal/gt911_touch.cpp` (copy from vendor)
- Create: `src/hal/esp_lcd_touch.h` (copy from vendor)
- Create: `src/hal/esp_lcd_touch.c` (copy from vendor)
- Create: `src/hal/esp_lcd_touch_gt911.h` (copy from vendor)
- Create: `src/hal/esp_lcd_touch_gt911.c` (copy from vendor)
- Create: `lv_conf.h`

**Step 1: Create platformio.ini**

```ini
[platformio]
default_envs = jc1060

[env:jc1060]
platform = https://github.com/pioarduino/platform-espressif32.git#53.03.13+github
framework = arduino
board = esp32-p4-evboard
board_build.psram = enabled
monitor_speed = 115200
lib_deps =
    lvgl/lvgl@^9.2.2
    bblanchon/ArduinoJson@^7.0.0
build_flags =
    -DLV_CONF_INCLUDE_SIMPLE
    -I src
```

**Step 2: Create `lv_conf.h` in project root**

This file must be in the project root (next to `platformio.ini`) for LVGL to find it. Key settings:

```c
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Memory */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc

/* Tick */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Display */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Fonts - enable the ones we need */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1

/* Widgets */
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_IMG 1
#define LV_USE_LINE 1
#define LV_USE_ARC 1
#define LV_USE_TABLE 1
#define LV_USE_CANVAS 1
#define LV_USE_TILEVIEW 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_LIST 1
#define LV_USE_MSGBOX 1
#define LV_USE_ROLLER 1
#define LV_USE_DROPDOWN 1
#define LV_USE_CHECKBOX 1
#define LV_USE_SPINNER 1

/* Animations */
#define LV_USE_ANIM 1

/* Drawing */
#define LV_DRAW_SW_DRAW_UNIT_CNT 2

#endif /* LV_CONF_H */
```

**Step 3: Create `src/pins_config.h`**

```cpp
#pragma once

// Display
#define LCD_H_RES 1024
#define LCD_V_RES 600
#define LCD_RST 27
#define LCD_LED 23

// Touch (GT911 via I2C)
#define TP_I2C_SDA 7
#define TP_I2C_SCL 8
#define TP_RST -1
#define TP_INT -1

// SD Card (SDMMC 4-bit)
#define SD_CMD  44
#define SD_CLK  43
#define SD_D0   39
#define SD_D1   40
#define SD_D2   41
#define SD_D3   42

// WiFi C6 (ESP-Hosted SDIO - handled by framework, listed for reference)
#define WIFI_C6_RST 54
```

**Step 4: Copy vendor HAL drivers**

Copy all files from the vendor repo's `src/lcd/` and `src/touch/` directories into `src/hal/`. These are the JD9165 MIPI-DSI display driver and GT911 capacitive touch driver. Exact files listed above.

**IMPORTANT:** The vendor touch driver file `gt911_touch.cpp` has touch x/y max swapped (600/1024 instead of 1024/600 because it was written for portrait mode with SW rotation). Fix this:

In `src/hal/gt911_touch.cpp`, change:
```c
#define CONFIG_LCD_HRES 600
#define CONFIG_LCD_VRES 1024
```
to:
```c
#define CONFIG_LCD_HRES 1024
#define CONFIG_LCD_VRES 600
```

**Step 5: Create `src/main.cpp`**

```cpp
#pragma GCC push_options
#pragma GCC optimize("O3")

#include <Arduino.h>
#include "lvgl.h"
#include "pins_config.h"
#include "hal/jd9165_lcd.h"
#include "hal/gt911_touch.h"

// Hardware drivers
static jd9165_lcd lcd(LCD_RST);
static gt911_touch touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);

// LVGL display and buffers
static lv_display_t *disp;
static uint32_t *buf0;
static uint32_t *buf1;

// Display flush callback
static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *color_map) {
    lcd.lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_display_flush_ready(d);
}

// Touch read callback
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x, y;
    if (touch.getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("ADS-B Display starting...");

    // Init display hardware
    lcd.begin();
    Serial.println("LCD initialized");

    // Init touch hardware
    touch.begin();
    Serial.println("Touch initialized");

    // Init LVGL
    lv_init();

    // Allocate framebuffers in PSRAM
    uint32_t buf_size = LCD_H_RES * LCD_V_RES;
    buf0 = (uint32_t *)heap_caps_malloc(buf_size * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    buf1 = (uint32_t *)heap_caps_malloc(buf_size * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(buf0 && buf1);

    // Create LVGL display
    disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf0, buf1, buf_size * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

    // Create LVGL touch input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    // Hello World test
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "ADS-B Display\nTouch anywhere to test");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    Serial.println("LVGL initialized - Hello World displayed");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

**Step 6: Compile and flash**

Run: `pio run -t upload && pio device monitor`

Expected: Screen shows "ADS-B Display / Touch anywhere to test" in white text on dark background. Touch coordinates print to serial monitor.

**Step 7: Commit**

```bash
git init
git add platformio.ini lv_conf.h src/ docs/
git commit -m "feat: project scaffold with display and touch working"
```

---

## Task 2: WiFi Connection & ADS-B Data Fetcher

**Goal:** Connect to WiFi via ESP32-C6, poll adsb.lol API, parse aircraft JSON, print results to serial.

**Files:**
- Create: `src/config.h`
- Create: `src/data/aircraft.h`
- Create: `src/data/fetcher.h`
- Create: `src/data/fetcher.cpp`
- Modify: `src/main.cpp`

**Step 1: Create `src/config.h`**

```cpp
#pragma once

// WiFi credentials
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

// Home location (change to your coordinates)
#define HOME_LAT 40.7128
#define HOME_LON -74.0060

// ADS-B settings
#define ADSB_RADIUS_NM 50
#define ADSB_POLL_INTERVAL_MS 5000
#define MAX_AIRCRAFT 200
#define TRAIL_LENGTH 60
```

**Step 2: Create `src/data/aircraft.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstring>

struct TrailPoint {
    float lat;
    float lon;
    int32_t alt;
    uint32_t timestamp;
};

struct Aircraft {
    char icao_hex[7];       // e.g. "A0B1C2"
    char callsign[9];       // e.g. "UAL1234"
    char registration[9];   // e.g. "N12345"
    char type_code[5];      // e.g. "B738"
    float lat;
    float lon;
    int32_t altitude;       // feet
    int16_t speed;          // knots
    int16_t heading;        // degrees 0-359
    int16_t vert_rate;      // ft/min
    uint16_t squawk;
    bool on_ground;
    bool is_military;
    bool is_emergency;
    bool is_watched;
    uint32_t last_seen;     // millis() timestamp
    TrailPoint trail[60];
    uint8_t trail_count;

    void clear() {
        memset(this, 0, sizeof(Aircraft));
    }
};

// Thread-safe aircraft list
class AircraftList {
public:
    Aircraft aircraft[200];
    int count = 0;
    SemaphoreHandle_t mutex;

    void init() {
        mutex = xSemaphoreCreateMutex();
        count = 0;
    }

    bool lock(TickType_t timeout = pdMS_TO_TICKS(100)) {
        return xSemaphoreTake(mutex, timeout) == pdTRUE;
    }

    void unlock() {
        xSemaphoreGive(mutex);
    }
};
```

**Step 3: Create `src/data/fetcher.h`**

```cpp
#pragma once
#include "aircraft.h"

// Initialize WiFi and start the background fetch task
void fetcher_init(AircraftList *list);

// Returns true if WiFi is connected
bool fetcher_wifi_connected();

// Returns the timestamp of the last successful fetch
uint32_t fetcher_last_update();
```

**Step 4: Create `src/data/fetcher.cpp`**

```cpp
#include "fetcher.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static AircraftList *_aircraft_list = nullptr;
static uint32_t _last_update = 0;
static TaskHandle_t _fetch_task_handle = nullptr;

// Check if ICAO hex is in known military ranges
static bool check_military(const char *hex) {
    // US military: AE0000-AE ffff, various others
    uint32_t h = strtoul(hex, nullptr, 16);
    // US military
    if (h >= 0xAE0000 && h <= 0xAFFFFF) return true;
    if (h >= 0xADF7C8 && h <= 0xAFFFFF) return true;
    // UK military
    if (h >= 0x43C000 && h <= 0x43CFFF) return true;
    return false;
}

static bool check_emergency(uint16_t squawk) {
    return squawk == 7500 || squawk == 7600 || squawk == 7700;
}

static void parse_aircraft_json(JsonDocument &doc) {
    if (!_aircraft_list->lock()) return;

    JsonArray ac = doc["ac"].as<JsonArray>();
    int new_count = 0;

    for (JsonObject obj : ac) {
        if (new_count >= MAX_AIRCRAFT) break;

        Aircraft &a = _aircraft_list->aircraft[new_count];

        // Check if this aircraft was already tracked (for trail continuity)
        const char *hex = obj["hex"] | "";
        int existing_idx = -1;
        for (int i = 0; i < _aircraft_list->count; i++) {
            if (strcmp(_aircraft_list->aircraft[i].icao_hex, hex) == 0) {
                existing_idx = i;
                break;
            }
        }

        if (existing_idx >= 0) {
            // Preserve trail from existing entry
            Aircraft &old = _aircraft_list->aircraft[existing_idx];
            memcpy(a.trail, old.trail, sizeof(a.trail));
            a.trail_count = old.trail_count;
        } else {
            a.trail_count = 0;
        }

        // Parse fields
        strlcpy(a.icao_hex, hex, sizeof(a.icao_hex));
        strlcpy(a.callsign, obj["flight"] | "", sizeof(a.callsign));
        // Trim trailing spaces from callsign
        for (int i = strlen(a.callsign) - 1; i >= 0 && a.callsign[i] == ' '; i--)
            a.callsign[i] = '\0';

        strlcpy(a.registration, obj["r"] | "", sizeof(a.registration));
        strlcpy(a.type_code, obj["t"] | "", sizeof(a.type_code));

        a.lat = obj["lat"] | 0.0f;
        a.lon = obj["lon"] | 0.0f;
        a.altitude = obj["alt_baro"].is<int>() ? obj["alt_baro"].as<int>() : 0;
        a.speed = obj["gs"] | 0;
        a.heading = obj["track"] | 0;
        a.vert_rate = obj["baro_rate"] | 0;
        a.squawk = strtoul(obj["squawk"] | "0", nullptr, 10);
        a.on_ground = obj["alt_baro"] == "ground";

        // Only include aircraft with valid position
        if (a.lat == 0.0f && a.lon == 0.0f) continue;

        // Flags
        a.is_military = check_military(a.icao_hex);
        a.is_emergency = check_emergency(a.squawk);
        a.is_watched = false; // TODO: check watchlist
        a.last_seen = millis();

        // Append to trail
        if (a.trail_count < TRAIL_LENGTH) {
            a.trail[a.trail_count] = {a.lat, a.lon, a.altitude, a.last_seen};
            a.trail_count++;
        } else {
            // Shift trail left, append new point
            memmove(&a.trail[0], &a.trail[1], (TRAIL_LENGTH - 1) * sizeof(TrailPoint));
            a.trail[TRAIL_LENGTH - 1] = {a.lat, a.lon, a.altitude, a.last_seen};
        }

        new_count++;
    }

    _aircraft_list->count = new_count;
    _aircraft_list->unlock();
    _last_update = millis();
}

static void fetch_task(void *param) {
    // Connect WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nWiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());

    // Build API URL
    char url[128];
    snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
             HOME_LAT, HOME_LON, ADSB_RADIUS_NM);
    Serial.printf("ADS-B API URL: %s\n", url);

    // Main fetch loop
    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.begin(url);
            http.setTimeout(10000);
            int httpCode = http.GET();

            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload);
                if (!err) {
                    parse_aircraft_json(doc);
                    Serial.printf("Fetched %d aircraft\n", _aircraft_list->count);
                } else {
                    Serial.printf("JSON parse error: %s\n", err.c_str());
                }
            } else {
                Serial.printf("HTTP error: %d\n", httpCode);
            }
            http.end();
        } else {
            Serial.println("WiFi disconnected, reconnecting...");
            WiFi.reconnect();
        }
        vTaskDelay(pdMS_TO_TICKS(ADSB_POLL_INTERVAL_MS));
    }
}

void fetcher_init(AircraftList *list) {
    _aircraft_list = list;
    xTaskCreatePinnedToCore(fetch_task, "adsb_fetch", 16384, nullptr, 1, &_fetch_task_handle, 1);
}

bool fetcher_wifi_connected() {
    return WiFi.status() == WL_CONNECTED;
}

uint32_t fetcher_last_update() {
    return _last_update;
}
```

**Step 5: Update `src/main.cpp`**

Add to includes:
```cpp
#include "config.h"
#include "data/aircraft.h"
#include "data/fetcher.h"
```

Add global:
```cpp
AircraftList aircraft_list;
```

In `setup()`, after LVGL init, replace the Hello World label section with:
```cpp
    // Init aircraft data
    aircraft_list.init();

    // Start data fetcher on core 1
    fetcher_init(&aircraft_list);

    // Temporary: show status label
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    lv_obj_t *status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(status_label, "Connecting to WiFi...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_center(status_label);

    // Timer to update status label
    lv_timer_create([](lv_timer_t *timer) {
        lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(timer);
        if (fetcher_wifi_connected()) {
            lv_label_set_text_fmt(label, "WiFi: Connected\nAircraft: %d\nLast update: %lus ago",
                                  aircraft_list.count,
                                  (millis() - fetcher_last_update()) / 1000);
        } else {
            lv_label_set_text(label, "Connecting to WiFi...");
        }
    }, 1000, status_label);
```

**Step 6: Compile, flash, verify**

Run: `pio run -t upload && pio device monitor`

Expected: Serial shows WiFi connection, then "Fetched N aircraft" every 5 seconds. Screen shows aircraft count updating.

**Step 7: Commit**

```bash
git add src/config.h src/data/
git commit -m "feat: WiFi connection and adsb.lol data fetcher"
```

---

## Task 3: Status Bar & Tileview Shell

**Goal:** Create the persistent top status bar and the swipeable tileview container that all views will live in.

**Files:**
- Create: `src/ui/status_bar.h`
- Create: `src/ui/status_bar.cpp`
- Create: `src/ui/views.h`
- Create: `src/ui/views.cpp`
- Modify: `src/main.cpp`

**Step 1: Create `src/ui/status_bar.h`**

```cpp
#pragma once
#include "lvgl.h"
#include "../data/aircraft.h"

// Create the status bar at the top of the screen (30px tall)
lv_obj_t *status_bar_create(lv_obj_t *parent);

// Update status bar with current data
void status_bar_update(bool wifi_connected, int aircraft_count, uint32_t last_update_ms);
```

**Step 2: Create `src/ui/status_bar.cpp`**

```cpp
#include "status_bar.h"

static lv_obj_t *wifi_icon;
static lv_obj_t *ac_count_label;
static lv_obj_t *update_label;
static lv_obj_t *view_dots[3]; // one per view

#define STATUS_BAR_HEIGHT 30
#define STATUS_BG_COLOR lv_color_hex(0x0d0d1a)
#define STATUS_TEXT_COLOR lv_color_hex(0x888899)
#define STATUS_ACCENT_COLOR lv_color_hex(0x00cc66)

lv_obj_t *status_bar_create(lv_obj_t *parent) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LCD_H_RES, STATUS_BAR_HEIGHT);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, STATUS_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi indicator (left side)
    wifi_icon = lv_label_create(bar);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, STATUS_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 8, 0);

    // Aircraft count
    ac_count_label = lv_label_create(bar);
    lv_label_set_text(ac_count_label, "0 aircraft");
    lv_obj_set_style_text_color(ac_count_label, STATUS_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(ac_count_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ac_count_label, LV_ALIGN_LEFT_MID, 36, 0);

    // View indicator dots (center)
    for (int i = 0; i < 3; i++) {
        view_dots[i] = lv_obj_create(bar);
        lv_obj_set_size(view_dots[i], 8, 8);
        lv_obj_set_style_radius(view_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(view_dots[i], STATUS_TEXT_COLOR, 0);
        lv_obj_set_style_bg_opa(view_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(view_dots[i], 0, 0);
        lv_obj_align(view_dots[i], LV_ALIGN_CENTER, (i - 1) * 16, 0);
    }
    // First dot active by default
    lv_obj_set_style_bg_color(view_dots[0], STATUS_ACCENT_COLOR, 0);

    // Last update (right side)
    update_label = lv_label_create(bar);
    lv_label_set_text(update_label, "--");
    lv_obj_set_style_text_color(update_label, STATUS_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(update_label, &lv_font_montserrat_14, 0);
    lv_obj_align(update_label, LV_ALIGN_RIGHT_MID, -8, 0);

    return bar;
}

void status_bar_update(bool wifi_connected, int aircraft_count, uint32_t last_update_ms) {
    // WiFi color
    lv_obj_set_style_text_color(wifi_icon,
        wifi_connected ? STATUS_ACCENT_COLOR : lv_color_hex(0xcc3333), 0);

    // Aircraft count
    lv_label_set_text_fmt(ac_count_label, "%d aircraft", aircraft_count);

    // Last update
    if (last_update_ms == 0) {
        lv_label_set_text(update_label, "No data");
    } else {
        uint32_t ago = (millis() - last_update_ms) / 1000;
        lv_label_set_text_fmt(update_label, "%lus ago", ago);
    }
}
```

**Step 3: Create `src/ui/views.h`**

```cpp
#pragma once
#include "lvgl.h"
#include "../data/aircraft.h"

// View indices
#define VIEW_MAP 0
#define VIEW_RADAR 1
#define VIEW_ARRIVALS 2

// Initialize the tileview with all view containers
void views_init(lv_obj_t *parent, AircraftList *list);

// Get the container object for a specific view (for adding child widgets)
lv_obj_t *views_get_tile(int view_index);

// Set the active view indicator dot
void views_set_active_dot(int view_index);
```

**Step 4: Create `src/ui/views.cpp`**

```cpp
#include "views.h"
#include "status_bar.h"
#include "../pins_config.h"

static lv_obj_t *tileview;
static lv_obj_t *tiles[3];

#define CONTENT_Y STATUS_BAR_HEIGHT
#define CONTENT_H (LCD_V_RES - STATUS_BAR_HEIGHT)

static void tileview_changed_cb(lv_event_t *e) {
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *active = lv_tileview_get_tile_active(tv);
    for (int i = 0; i < 3; i++) {
        if (tiles[i] == active) {
            views_set_active_dot(i);
            break;
        }
    }
}

void views_init(lv_obj_t *parent, AircraftList *list) {
    // Tileview fills screen below status bar
    tileview = lv_tileview_create(parent);
    lv_obj_set_pos(tileview, 0, CONTENT_Y);
    lv_obj_set_size(tileview, LCD_H_RES, CONTENT_H);
    lv_obj_set_style_bg_color(tileview, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, 0);

    // Create 3 horizontal tiles
    tiles[VIEW_MAP] = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    tiles[VIEW_RADAR] = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    tiles[VIEW_ARRIVALS] = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

    lv_obj_add_event_cb(tileview, tileview_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Placeholder labels for each view
    const char *names[] = {"Map View", "Radar View", "Arrivals Board"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *label = lv_label_create(tiles[i]);
        lv_label_set_text(label, names[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x334455), 0);
        lv_obj_center(label);
    }
}

lv_obj_t *views_get_tile(int view_index) {
    return tiles[view_index];
}

void views_set_active_dot(int view_index) {
    // Implemented via external reference to status bar dots
    // This will be connected when status_bar exposes dot update function
}
```

**Step 5: Update `src/main.cpp` to use status bar and tileview**

Replace the temporary status label section in `setup()`:

```cpp
    // Create UI
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    status_bar_create(screen);
    views_init(screen, &aircraft_list);

    // Periodic status bar update
    lv_timer_create([](lv_timer_t *timer) {
        status_bar_update(fetcher_wifi_connected(), aircraft_list.count, fetcher_last_update());
    }, 1000, nullptr);
```

Add includes: `#include "ui/status_bar.h"` and `#include "ui/views.h"`

**Step 6: Compile, flash, verify**

Run: `pio run -t upload`

Expected: Status bar at top with WiFi icon, aircraft count, view dots. Three swipeable views with placeholder text. Swiping updates the active dot.

**Step 7: Commit**

```bash
git add src/ui/
git commit -m "feat: status bar and swipeable tileview shell"
```

---

## Task 4: Map View — Aircraft on Canvas

**Goal:** Render aircraft positions as colored arrows on a dark canvas with callsign labels and trail lines. No map tiles yet — just aircraft on a solid background with range rings.

**Files:**
- Create: `src/ui/map_view.h`
- Create: `src/ui/map_view.cpp`
- Create: `src/ui/geo.h`
- Modify: `src/ui/views.cpp`

**Step 1: Create `src/ui/geo.h`**

Utility functions to convert lat/lon to screen coordinates:

```cpp
#pragma once
#include <cmath>

// Convert nautical miles to approximate degrees of latitude
#define NM_TO_DEG_LAT (1.0f / 60.0f)

struct MapProjection {
    float center_lat;
    float center_lon;
    float radius_nm;    // visible radius in nautical miles
    int screen_w;
    int screen_h;
    int offset_x;       // for panning
    int offset_y;

    // Convert lat/lon to screen x,y. Returns false if off-screen.
    bool to_screen(float lat, float lon, int &sx, int &sy) const {
        float dx_nm = (lon - center_lon) * 60.0f * cosf(center_lat * M_PI / 180.0f);
        float dy_nm = (lat - center_lat) * 60.0f;

        float scale = (float)screen_h / (radius_nm * 2.0f);
        sx = (int)(screen_w / 2 + dx_nm * scale) + offset_x;
        sy = (int)(screen_h / 2 - dy_nm * scale) + offset_y;

        return (sx >= -20 && sx < screen_w + 20 && sy >= -20 && sy < screen_h + 20);
    }

    // Distance in nautical miles between two points (Haversine)
    static float distance_nm(float lat1, float lon1, float lat2, float lon2) {
        float dlat = (lat2 - lat1) * M_PI / 180.0f;
        float dlon = (lon2 - lon1) * M_PI / 180.0f;
        float a = sinf(dlat / 2) * sinf(dlat / 2) +
                  cosf(lat1 * M_PI / 180.0f) * cosf(lat2 * M_PI / 180.0f) *
                  sinf(dlon / 2) * sinf(dlon / 2);
        float c = 2.0f * atan2f(sqrtf(a), sqrtf(1 - a));
        return c * 3440.065f; // Earth radius in NM
    }
};

// Altitude to color (green=low, yellow=mid, red=high)
static inline lv_color_t altitude_color(int32_t alt_ft) {
    if (alt_ft <= 0) return lv_color_hex(0x666666);       // ground
    if (alt_ft < 5000) return lv_color_hex(0x00cc44);     // green
    if (alt_ft < 15000) return lv_color_hex(0x88cc00);    // yellow-green
    if (alt_ft < 25000) return lv_color_hex(0xcccc00);    // yellow
    if (alt_ft < 35000) return lv_color_hex(0xcc8800);    // orange
    return lv_color_hex(0xcc2200);                         // red (high altitude)
}
```

**Step 2: Create `src/ui/map_view.h`**

```cpp
#pragma once
#include "lvgl.h"
#include "../data/aircraft.h"
#include "geo.h"

void map_view_init(lv_obj_t *parent, AircraftList *list);
void map_view_update();
```

**Step 3: Create `src/ui/map_view.cpp`**

This is the largest single file. It uses an LVGL canvas to draw aircraft, trails, and range rings.

```cpp
#include "map_view.h"
#include "../config.h"
#include "../pins_config.h"

static AircraftList *_list = nullptr;
static lv_obj_t *_canvas = nullptr;
static uint16_t *_canvas_buf = nullptr;
static MapProjection _proj;

// Zoom levels: 50nm, 20nm, 5nm
static const float ZOOM_LEVELS[] = {50.0f, 20.0f, 5.0f};
static int _zoom_idx = 0;

#define CANVAS_W LCD_H_RES
#define CANVAS_H (LCD_V_RES - 30)  // minus status bar
#define BG_COLOR lv_color_hex(0x0a0a1a)

static void draw_range_rings(lv_layer_t *layer) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_hex(0x1a2a3a);
    arc_dsc.width = 1;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;

    float radius_nm = ZOOM_LEVELS[_zoom_idx];
    float scale = (float)CANVAS_H / (radius_nm * 2.0f);

    // Draw rings at regular intervals
    float ring_interval = radius_nm <= 10 ? 2.0f : (radius_nm <= 25 ? 5.0f : 10.0f);
    for (float r = ring_interval; r <= radius_nm; r += ring_interval) {
        int pixel_r = (int)(r * scale);
        lv_area_t area;
        area.x1 = CANVAS_W / 2 - pixel_r + _proj.offset_x;
        area.y1 = CANVAS_H / 2 - pixel_r + _proj.offset_y;
        area.x2 = CANVAS_W / 2 + pixel_r + _proj.offset_x;
        area.y2 = CANVAS_H / 2 + pixel_r + _proj.offset_y;
        arc_dsc.center.x = CANVAS_W / 2 + _proj.offset_x;
        arc_dsc.center.y = CANVAS_H / 2 + _proj.offset_y;
        arc_dsc.radius = pixel_r;
        lv_draw_arc(layer, &arc_dsc);
    }
}

static void draw_home_marker(lv_layer_t *layer) {
    int hx, hy;
    if (!_proj.to_screen(HOME_LAT, HOME_LON, hx, hy)) return;

    // Crosshair
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x4488ff);
    line_dsc.width = 1;

    // Horizontal line
    lv_point_precise_t p1 = {(lv_value_precise_t)(hx - 8), (lv_value_precise_t)hy};
    lv_point_precise_t p2 = {(lv_value_precise_t)(hx + 8), (lv_value_precise_t)hy};
    line_dsc.p1 = p1;
    line_dsc.p2 = p2;
    lv_draw_line(layer, &line_dsc);

    // Vertical line
    p1 = {(lv_value_precise_t)hx, (lv_value_precise_t)(hy - 8)};
    p2 = {(lv_value_precise_t)hx, (lv_value_precise_t)(hy + 8)};
    line_dsc.p1 = p1;
    line_dsc.p2 = p2;
    lv_draw_line(layer, &line_dsc);
}

static void draw_aircraft(lv_layer_t *layer) {
    if (!_list->lock(pdMS_TO_TICKS(50))) return;

    for (int i = 0; i < _list->count; i++) {
        Aircraft &ac = _list->aircraft[i];
        int sx, sy;
        if (!_proj.to_screen(ac.lat, ac.lon, sx, sy)) continue;

        lv_color_t color = altitude_color(ac.altitude);

        // Draw trail
        if (ac.trail_count > 1) {
            lv_draw_line_dsc_t trail_dsc;
            lv_draw_line_dsc_init(&trail_dsc);
            trail_dsc.width = 1;

            for (int t = 1; t < ac.trail_count; t++) {
                int tx1, ty1, tx2, ty2;
                if (_proj.to_screen(ac.trail[t - 1].lat, ac.trail[t - 1].lon, tx1, ty1) &&
                    _proj.to_screen(ac.trail[t].lat, ac.trail[t].lon, tx2, ty2)) {
                    trail_dsc.color = altitude_color(ac.trail[t].alt);
                    trail_dsc.opa = LV_OPA_50 + (t * LV_OPA_50 / ac.trail_count);
                    trail_dsc.p1 = {(lv_value_precise_t)tx1, (lv_value_precise_t)ty1};
                    trail_dsc.p2 = {(lv_value_precise_t)tx2, (lv_value_precise_t)ty2};
                    lv_draw_line(layer, &trail_dsc);
                }
            }
        }

        // Draw heading line (direction of travel)
        float heading_rad = ac.heading * M_PI / 180.0f;
        int hx = sx + (int)(14 * sinf(heading_rad));
        int hy = sy - (int)(14 * cosf(heading_rad));
        lv_draw_line_dsc_t hdg_dsc;
        lv_draw_line_dsc_init(&hdg_dsc);
        hdg_dsc.color = color;
        hdg_dsc.width = 2;
        hdg_dsc.p1 = {(lv_value_precise_t)sx, (lv_value_precise_t)sy};
        hdg_dsc.p2 = {(lv_value_precise_t)hx, (lv_value_precise_t)hy};
        lv_draw_line(layer, &hdg_dsc);

        // Draw aircraft dot
        lv_draw_rect_dsc_t dot_dsc;
        lv_draw_rect_dsc_init(&dot_dsc);
        dot_dsc.bg_color = color;
        dot_dsc.bg_opa = LV_OPA_COVER;
        dot_dsc.radius = 3;
        lv_area_t dot_area = {(lv_coord_t)(sx - 3), (lv_coord_t)(sy - 3),
                               (lv_coord_t)(sx + 3), (lv_coord_t)(sy + 3)};
        lv_draw_rect(layer, &dot_dsc, &dot_area);

        // Draw callsign label
        const char *label_text = ac.callsign[0] ? ac.callsign : ac.icao_hex;
        lv_draw_label_dsc_t lbl_dsc;
        lv_draw_label_dsc_init(&lbl_dsc);
        lbl_dsc.color = color;
        lbl_dsc.font = &lv_font_montserrat_14;
        lbl_dsc.opa = LV_OPA_80;
        lv_area_t lbl_area = {(lv_coord_t)(sx + 8), (lv_coord_t)(sy - 7),
                               (lv_coord_t)(sx + 120), (lv_coord_t)(sy + 10)};
        lbl_dsc.text = label_text;
        lv_draw_label(layer, &lbl_dsc, &lbl_area);
    }

    _list->unlock();
}

static void canvas_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    draw_range_rings(layer);
    draw_home_marker(layer);
    draw_aircraft(layer);
}

void map_view_init(lv_obj_t *parent, AircraftList *list) {
    _list = list;

    // Setup projection
    _proj.center_lat = HOME_LAT;
    _proj.center_lon = HOME_LON;
    _proj.radius_nm = ZOOM_LEVELS[_zoom_idx];
    _proj.screen_w = CANVAS_W;
    _proj.screen_h = CANVAS_H;
    _proj.offset_x = 0;
    _proj.offset_y = 0;

    // Create a container that we'll draw on using LVGL's draw events
    _canvas = lv_obj_create(parent);
    lv_obj_set_size(_canvas, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(_canvas, 0, 0);
    lv_obj_set_style_bg_color(_canvas, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_canvas, 0, 0);
    lv_obj_set_style_radius(_canvas, 0, 0);
    lv_obj_set_style_pad_all(_canvas, 0, 0);
    lv_obj_clear_flag(_canvas, LV_OBJ_FLAG_SCROLLABLE);

    // Use draw event for custom rendering
    lv_obj_add_event_cb(_canvas, canvas_draw_cb, LV_EVENT_DRAW_MAIN_END, nullptr);

    // Zoom on tap (temporary - will be replaced with aircraft selection later)
    lv_obj_add_event_cb(_canvas, [](lv_event_t *e) {
        _zoom_idx = (_zoom_idx + 1) % 3;
        _proj.radius_nm = ZOOM_LEVELS[_zoom_idx];
        lv_obj_invalidate(_canvas);
    }, LV_EVENT_CLICKED, nullptr);

    // Periodic refresh
    lv_timer_create([](lv_timer_t *t) {
        lv_obj_invalidate(_canvas);
    }, 1000, nullptr);
}

void map_view_update() {
    if (_canvas) lv_obj_invalidate(_canvas);
}
```

**Step 4: Update `src/ui/views.cpp`**

Add include and call `map_view_init` for the map tile:

```cpp
#include "map_view.h"
```

In `views_init()`, after creating tiles, replace the placeholder label for VIEW_MAP:

```cpp
    // Remove placeholder for map view, init real map
    // (keep placeholders for radar and arrivals for now)
    map_view_init(tiles[VIEW_MAP], list);
```

**Step 5: Compile, flash, verify**

Run: `pio run -t upload`

Expected: Map view shows range rings centered on your home location. Aircraft appear as colored dots with heading lines and callsign labels. Trails grow as data accumulates. Tapping the map cycles through 3 zoom levels. Swiping right shows placeholder Radar and Arrivals views.

**Step 6: Commit**

```bash
git add src/ui/map_view.* src/ui/geo.h
git commit -m "feat: map view with aircraft rendering, trails, and zoom"
```

---

## Task 5: Radar View

**Goal:** Classic green radar sweep display with phosphor persistence effect.

**Files:**
- Create: `src/ui/radar_view.h`
- Create: `src/ui/radar_view.cpp`
- Modify: `src/ui/views.cpp`

**Step 1: Create `src/ui/radar_view.h`**

```cpp
#pragma once
#include "lvgl.h"
#include "../data/aircraft.h"

void radar_view_init(lv_obj_t *parent, AircraftList *list);
void radar_view_update();
```

**Step 2: Create `src/ui/radar_view.cpp`**

```cpp
#include "radar_view.h"
#include "../config.h"
#include "../pins_config.h"
#include "geo.h"

static AircraftList *_list = nullptr;
static lv_obj_t *_radar_obj = nullptr;

static float _sweep_angle = 0.0f; // current sweep angle in degrees
static uint32_t _last_sweep_ms = 0;

#define RADAR_W LCD_H_RES
#define RADAR_H (LCD_V_RES - 30)
#define RADAR_CX (RADAR_W / 2)
#define RADAR_CY (RADAR_H / 2)
#define RADAR_R (RADAR_H / 2 - 10)  // max radius in pixels

#define SWEEP_PERIOD_MS 5000  // one full rotation = 5 seconds
#define PHOSPHOR_FADE_MS 4000 // blips fade over 4 seconds

#define COLOR_SWEEP lv_color_hex(0x00ff44)
#define COLOR_RING lv_color_hex(0x0a2a0a)
#define COLOR_TEXT lv_color_hex(0x00cc33)
#define COLOR_BG lv_color_hex(0x000800)
#define COLOR_BLIP lv_color_hex(0x00ff66)
#define COLOR_MILITARY lv_color_hex(0xffaa00)

static MapProjection _proj;

// Convert lat/lon to radar-relative polar, then to screen coords
static bool to_radar_screen(float lat, float lon, int &sx, int &sy) {
    float dx_nm = (lon - _proj.center_lon) * 60.0f * cosf(_proj.center_lat * M_PI / 180.0f);
    float dy_nm = (lat - _proj.center_lat) * 60.0f;
    float dist_nm = sqrtf(dx_nm * dx_nm + dy_nm * dy_nm);

    if (dist_nm > _proj.radius_nm) return false;

    float scale = (float)RADAR_R / _proj.radius_nm;
    sx = RADAR_CX + (int)(dx_nm * scale);
    sy = RADAR_CY - (int)(dy_nm * scale);
    return true;
}

static void draw_rings(lv_layer_t *layer) {
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = COLOR_RING;
    arc.width = 1;
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.center.x = RADAR_CX;
    arc.center.y = RADAR_CY;

    for (int i = 1; i <= 4; i++) {
        arc.radius = RADAR_R * i / 4;
        lv_draw_arc(layer, &arc);
    }

    // Cardinal direction labels
    const char *dirs[] = {"N", "E", "S", "W"};
    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};
    lv_draw_label_dsc_t lbl;
    lv_draw_label_dsc_init(&lbl);
    lbl.color = COLOR_RING;
    lbl.font = &lv_font_montserrat_14;
    for (int i = 0; i < 4; i++) {
        int lx = RADAR_CX + dx[i] * (RADAR_R + 2) - 5;
        int ly = RADAR_CY + dy[i] * (RADAR_R + 2) - 7;
        lv_area_t area = {(lv_coord_t)lx, (lv_coord_t)ly,
                          (lv_coord_t)(lx + 20), (lv_coord_t)(ly + 16)};
        lbl.text = dirs[i];
        lv_draw_label(layer, &lbl, &area);
    }

    // Cross lines
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = COLOR_RING;
    line.width = 1;
    line.opa = LV_OPA_30;
    line.p1 = {RADAR_CX, RADAR_CY - RADAR_R};
    line.p2 = {RADAR_CX, RADAR_CY + RADAR_R};
    lv_draw_line(layer, &line);
    line.p1 = {RADAR_CX - RADAR_R, RADAR_CY};
    line.p2 = {RADAR_CX + RADAR_R, RADAR_CY};
    lv_draw_line(layer, &line);
}

static void draw_sweep(lv_layer_t *layer) {
    float rad = _sweep_angle * M_PI / 180.0f;
    int ex = RADAR_CX + (int)(RADAR_R * sinf(rad));
    int ey = RADAR_CY - (int)(RADAR_R * cosf(rad));

    // Sweep line
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = COLOR_SWEEP;
    line.width = 2;
    line.opa = LV_OPA_60;
    line.p1 = {RADAR_CX, RADAR_CY};
    line.p2 = {(lv_value_precise_t)ex, (lv_value_precise_t)ey};
    lv_draw_line(layer, &line);

    // Fading trail (draw 3 faded lines behind sweep)
    for (int i = 1; i <= 6; i++) {
        float trail_rad = (_sweep_angle - i * 3.0f) * M_PI / 180.0f;
        int tx = RADAR_CX + (int)(RADAR_R * sinf(trail_rad));
        int ty = RADAR_CY - (int)(RADAR_R * cosf(trail_rad));
        line.opa = LV_OPA_40 - i * 6;
        line.width = 1;
        line.p1 = {RADAR_CX, RADAR_CY};
        line.p2 = {(lv_value_precise_t)tx, (lv_value_precise_t)ty};
        lv_draw_line(layer, &line);
    }
}

static void draw_blips(lv_layer_t *layer) {
    if (!_list->lock(pdMS_TO_TICKS(50))) return;

    uint32_t now = millis();

    for (int i = 0; i < _list->count; i++) {
        Aircraft &ac = _list->aircraft[i];
        int sx, sy;
        if (!to_radar_screen(ac.lat, ac.lon, sx, sy)) continue;

        // Calculate fade based on time since last seen
        uint32_t age_ms = now - ac.last_seen;
        if (age_ms > PHOSPHOR_FADE_MS) continue;
        uint8_t opa = LV_OPA_COVER - (age_ms * LV_OPA_COVER / PHOSPHOR_FADE_MS);

        lv_color_t color = ac.is_military ? COLOR_MILITARY : COLOR_BLIP;

        // Blip dot
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = color;
        dot.bg_opa = opa;
        dot.radius = 3;
        lv_area_t area = {(lv_coord_t)(sx - 3), (lv_coord_t)(sy - 3),
                          (lv_coord_t)(sx + 3), (lv_coord_t)(sy + 3)};
        lv_draw_rect(layer, &dot, &area);

        // Callsign
        const char *label_text = ac.callsign[0] ? ac.callsign : ac.icao_hex;
        lv_draw_label_dsc_t lbl;
        lv_draw_label_dsc_init(&lbl);
        lbl.color = color;
        lbl.font = &lv_font_montserrat_14;
        lbl.opa = opa > LV_OPA_50 ? LV_OPA_70 : opa;
        lv_area_t lbl_area = {(lv_coord_t)(sx + 6), (lv_coord_t)(sy - 6),
                               (lv_coord_t)(sx + 100), (lv_coord_t)(sy + 8)};
        lbl.text = label_text;
        lv_draw_label(layer, &lbl, &lbl_area);
    }

    _list->unlock();
}

static void radar_draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    draw_rings(layer);
    draw_sweep(layer);
    draw_blips(layer);
}

void radar_view_init(lv_obj_t *parent, AircraftList *list) {
    _list = list;

    _proj.center_lat = HOME_LAT;
    _proj.center_lon = HOME_LON;
    _proj.radius_nm = 50.0f;

    _radar_obj = lv_obj_create(parent);
    lv_obj_set_size(_radar_obj, RADAR_W, RADAR_H);
    lv_obj_set_pos(_radar_obj, 0, 0);
    lv_obj_set_style_bg_color(_radar_obj, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(_radar_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_radar_obj, 0, 0);
    lv_obj_set_style_radius(_radar_obj, 0, 0);
    lv_obj_clear_flag(_radar_obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(_radar_obj, radar_draw_cb, LV_EVENT_DRAW_MAIN_END, nullptr);

    // Animate sweep + refresh at 30fps
    _last_sweep_ms = millis();
    lv_timer_create([](lv_timer_t *t) {
        uint32_t now = millis();
        uint32_t dt = now - _last_sweep_ms;
        _last_sweep_ms = now;
        _sweep_angle += (360.0f * dt) / SWEEP_PERIOD_MS;
        if (_sweep_angle >= 360.0f) _sweep_angle -= 360.0f;
        lv_obj_invalidate(_radar_obj);
    }, 33, nullptr);  // ~30fps
}

void radar_view_update() {
    if (_radar_obj) lv_obj_invalidate(_radar_obj);
}
```

**Step 3: Update `src/ui/views.cpp`**

Add `#include "radar_view.h"` and in `views_init()`:

```cpp
    radar_view_init(tiles[VIEW_RADAR], list);
```

Remove the placeholder label for VIEW_RADAR.

**Step 4: Compile, flash, verify**

Expected: Swipe to second view → green radar display with rotating sweep, range rings, cardinal points. Aircraft appear as green blips that fade with phosphor persistence. Military aircraft show in amber.

**Step 5: Commit**

```bash
git add src/ui/radar_view.*
git commit -m "feat: radar view with sweep animation and phosphor persistence"
```

---

## Task 6: Arrivals Board View

**Goal:** Split-flap airport departures board aesthetic with flip animation.

**Files:**
- Create: `src/ui/arrivals_view.h`
- Create: `src/ui/arrivals_view.cpp`
- Modify: `src/ui/views.cpp`

**Step 1: Create `src/ui/arrivals_view.h`**

```cpp
#pragma once
#include "lvgl.h"
#include "../data/aircraft.h"

void arrivals_view_init(lv_obj_t *parent, AircraftList *list);
void arrivals_view_update();
```

**Step 2: Create `src/ui/arrivals_view.cpp`**

The split-flap effect is achieved by rendering each character in its own rounded-rect cell with a dark background. When values change, the character "rolls" through random characters before settling. We use LVGL labels with individual style updates and a timer-driven animation.

```cpp
#include "arrivals_view.h"
#include "../config.h"
#include "../pins_config.h"
#include "geo.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static AircraftList *_list = nullptr;
static lv_obj_t *_board_container = nullptr;

#define BOARD_W LCD_H_RES
#define BOARD_H (LCD_V_RES - 30)

// Split-flap cell dimensions
#define CELL_W 18
#define CELL_H 28
#define CELL_GAP 2
#define CELL_RADIUS 3
#define ROW_H (CELL_H + 4)
#define HEADER_H 36
#define MAX_ROWS 14

// Colors
#define BOARD_BG      lv_color_hex(0x0c0c0c)
#define CELL_BG       lv_color_hex(0x1a1a1a)
#define CELL_TEXT     lv_color_hex(0xffdd00)  // classic Solari yellow
#define HEADER_TEXT   lv_color_hex(0xffffff)
#define HEADER_BG     lv_color_hex(0x222222)
#define EMERGENCY_CLR lv_color_hex(0xff3333)
#define MILITARY_CLR  lv_color_hex(0xffaa44)

// Column definitions: name, character width, x offset
struct Column {
    const char *name;
    int chars;
    int x;
};

static Column columns[] = {
    {"FLIGHT",   8,  10},
    {"TYPE",     4,  180},
    {"ALT",      5,  270},
    {"SPD",      4,  380},
    {"DIST",     4,  470},
    {"HDG",      3,  550},
    {"STATUS",   7,  620},
};
#define NUM_COLS 7

// Per-cell animation state
struct FlipCell {
    lv_obj_t *label;
    char target;
    char current;
    int rolls_remaining; // 0 = settled, >0 = still flipping
};

struct BoardRow {
    FlipCell cells[40]; // max characters across all columns
    int total_cells;
    char icao_hex[7];   // to track which aircraft this row represents
    bool active;
};

static BoardRow _rows[MAX_ROWS];
static lv_obj_t *_header_labels[NUM_COLS];
static lv_obj_t *_title_label = nullptr;

static const char *status_from_vert_rate(int16_t vr, bool on_ground) {
    if (on_ground) return "GROUND ";
    if (vr > 300) return "CLIMB  ";
    if (vr < -300) return "DESCEND";
    return "CRUISE ";
}

// Create a single flip cell (character tile)
static FlipCell create_cell(lv_obj_t *parent, int x, int y) {
    FlipCell cell;
    cell.target = ' ';
    cell.current = ' ';
    cell.rolls_remaining = 0;

    // Background tile
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_set_size(bg, CELL_W, CELL_H);
    lv_obj_set_pos(bg, x, y);
    lv_obj_set_style_bg_color(bg, CELL_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, CELL_RADIUS, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    // Character label
    cell.label = lv_label_create(bg);
    lv_label_set_text(cell.label, " ");
    lv_obj_set_style_text_font(cell.label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cell.label, CELL_TEXT, 0);
    lv_obj_center(cell.label);

    return cell;
}

static void init_rows(lv_obj_t *parent) {
    for (int row = 0; row < MAX_ROWS; row++) {
        int y = HEADER_H + row * ROW_H + 4;
        int cell_idx = 0;
        _rows[row].active = false;
        memset(_rows[row].icao_hex, 0, sizeof(_rows[row].icao_hex));

        for (int col = 0; col < NUM_COLS; col++) {
            for (int ch = 0; ch < columns[col].chars; ch++) {
                int x = columns[col].x + ch * (CELL_W + CELL_GAP);
                _rows[row].cells[cell_idx] = create_cell(parent, x, y);
                cell_idx++;
            }
        }
        _rows[row].total_cells = cell_idx;
    }
}

// Set a row's target text — triggers flip animation for changed characters
static void set_row_text(int row, const char *texts[], lv_color_t color) {
    int cell_idx = 0;
    for (int col = 0; col < NUM_COLS; col++) {
        const char *text = texts[col];
        int len = strlen(text);
        for (int ch = 0; ch < columns[col].chars; ch++) {
            char target = (ch < len) ? text[ch] : ' ';
            FlipCell &fc = _rows[row].cells[cell_idx];

            // Set color
            lv_obj_set_style_text_color(fc.label, color, 0);

            if (target != fc.target) {
                fc.target = target;
                fc.rolls_remaining = 3 + (rand() % 4); // 3-6 random rolls
            }
            cell_idx++;
        }
    }
}

// Animation tick — called frequently to advance flip animations
static void flip_animation_tick(lv_timer_t *t) {
    static const char flip_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -./";
    bool any_flipping = false;

    for (int row = 0; row < MAX_ROWS; row++) {
        if (!_rows[row].active) continue;
        for (int c = 0; c < _rows[row].total_cells; c++) {
            FlipCell &fc = _rows[row].cells[c];
            if (fc.rolls_remaining > 0) {
                // Show random character
                char random_char = flip_chars[rand() % (sizeof(flip_chars) - 1)];
                char buf[2] = {random_char, 0};
                lv_label_set_text(fc.label, buf);
                fc.rolls_remaining--;
                any_flipping = true;
            } else if (fc.current != fc.target) {
                // Settle on target
                char buf[2] = {fc.target, 0};
                lv_label_set_text(fc.label, buf);
                fc.current = fc.target;
            }
        }
    }
}

// Update board data from aircraft list
static void update_board(lv_timer_t *t) {
    if (!_list->lock(pdMS_TO_TICKS(50))) return;

    int row = 0;
    for (int i = 0; i < _list->count && row < MAX_ROWS; i++) {
        Aircraft &ac = _list->aircraft[i];
        if (ac.lat == 0 && ac.lon == 0) continue;

        _rows[row].active = true;
        strlcpy(_rows[row].icao_hex, ac.icao_hex, sizeof(_rows[row].icao_hex));

        // Format each column
        char flight[9], type[5], alt[6], spd[5], dist[5], hdg[4], status[8];
        snprintf(flight, sizeof(flight), "%-8s", ac.callsign[0] ? ac.callsign : ac.icao_hex);
        snprintf(type, sizeof(type), "%-4s", ac.type_code);
        if (ac.on_ground) snprintf(alt, sizeof(alt), " GND ");
        else snprintf(alt, sizeof(alt), "%5d", ac.altitude / 100); // flight level
        snprintf(spd, sizeof(spd), "%4d", ac.speed);

        float dist_nm = MapProjection::distance_nm(HOME_LAT, HOME_LON, ac.lat, ac.lon);
        snprintf(dist, sizeof(dist), "%4.0f", dist_nm);
        snprintf(hdg, sizeof(hdg), "%03d", ac.heading);
        snprintf(status, sizeof(status), "%-7s", status_from_vert_rate(ac.vert_rate, ac.on_ground));

        const char *texts[] = {flight, type, alt, spd, dist, hdg, status};

        lv_color_t color = CELL_TEXT;
        if (ac.is_emergency) color = EMERGENCY_CLR;
        else if (ac.is_military) color = MILITARY_CLR;

        set_row_text(row, texts, color);
        row++;
    }

    // Clear remaining rows
    for (; row < MAX_ROWS; row++) {
        if (_rows[row].active) {
            const char *blanks[] = {"", "", "", "", "", "", ""};
            set_row_text(row, blanks, CELL_TEXT);
            _rows[row].active = false;
        }
    }

    // Update title with count
    lv_label_set_text_fmt(_title_label, "OVERHEAD TRAFFIC         %d", _list->count);

    _list->unlock();
}

void arrivals_view_init(lv_obj_t *parent, AircraftList *list) {
    _list = list;

    _board_container = lv_obj_create(parent);
    lv_obj_set_size(_board_container, BOARD_W, BOARD_H);
    lv_obj_set_pos(_board_container, 0, 0);
    lv_obj_set_style_bg_color(_board_container, BOARD_BG, 0);
    lv_obj_set_style_bg_opa(_board_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_board_container, 0, 0);
    lv_obj_set_style_radius(_board_container, 0, 0);
    lv_obj_set_style_pad_all(_board_container, 0, 0);
    lv_obj_clear_flag(_board_container, LV_OBJ_FLAG_SCROLLABLE);

    // Title bar
    lv_obj_t *title_bg = lv_obj_create(_board_container);
    lv_obj_set_size(title_bg, BOARD_W, HEADER_H);
    lv_obj_set_pos(title_bg, 0, 0);
    lv_obj_set_style_bg_color(title_bg, HEADER_BG, 0);
    lv_obj_set_style_bg_opa(title_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bg, 0, 0);
    lv_obj_set_style_radius(title_bg, 0, 0);
    lv_obj_clear_flag(title_bg, LV_OBJ_FLAG_SCROLLABLE);

    _title_label = lv_label_create(title_bg);
    lv_label_set_text(_title_label, "OVERHEAD TRAFFIC");
    lv_obj_set_style_text_font(_title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_title_label, HEADER_TEXT, 0);
    lv_obj_align(_title_label, LV_ALIGN_LEFT_MID, 10, 0);

    // Column headers
    for (int i = 0; i < NUM_COLS; i++) {
        lv_obj_t *lbl = lv_label_create(_board_container);
        lv_label_set_text(lbl, columns[i].name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);
        lv_obj_set_pos(lbl, columns[i].x, HEADER_H - 14);
        _header_labels[i] = lbl;
    }

    // Create all flip cells
    init_rows(_board_container);

    // Flip animation timer (runs fast for smooth rolling effect)
    lv_timer_create(flip_animation_tick, 60, nullptr);

    // Data update timer (sync with fetch interval)
    lv_timer_create(update_board, 2000, nullptr);
}

void arrivals_view_update() {
    // Triggered externally if needed
}
```

**Step 3: Update `src/ui/views.cpp`**

Add `#include "arrivals_view.h"` and in `views_init()`:

```cpp
    arrivals_view_init(tiles[VIEW_ARRIVALS], list);
```

Remove the placeholder label for VIEW_ARRIVALS.

**Step 4: Compile, flash, verify**

Expected: Swipe to third view → black background with yellow split-flap character tiles. Aircraft data appears with rolling/flipping animation as characters settle. Emergency aircraft in red, military in amber. Title reads "OVERHEAD TRAFFIC" with count.

**Step 5: Commit**

```bash
git add src/ui/arrivals_view.*
git commit -m "feat: arrivals board view with split-flap animation"
```

---

## Task 7: Detail Card (Bottom Sheet)

**Goal:** Tap an aircraft on map or radar to open a bottom sheet with aircraft photo, info, and flight details.

**Files:**
- Create: `src/ui/detail_card.h`
- Create: `src/ui/detail_card.cpp`
- Create: `src/data/enrichment.h`
- Create: `src/data/enrichment.cpp`
- Modify: `src/ui/map_view.cpp` (add tap-to-select)
- Modify: `src/ui/radar_view.cpp` (add tap-to-select)
- Modify: `src/ui/arrivals_view.cpp` (add tap-to-select)

**Step 1: Create `src/data/enrichment.h`**

```cpp
#pragma once
#include <cstdint>

struct AircraftEnrichment {
    char photo_url[256];
    char airline[32];
    char origin_airport[48];      // "JFK - John F. Kennedy Intl"
    char destination_airport[48]; // "LAX - Los Angeles Intl"
    char manufacturer[32];
    char model[32];
    char engine_type[24];
    uint8_t engine_count;
    bool loaded;
    bool loading;
};

// Fetch enrichment data in background. Calls callback when done.
void enrichment_fetch(const char *icao_hex, const char *registration,
                      void (*callback)(AircraftEnrichment *data));

// Get cached enrichment (returns nullptr if not yet fetched)
AircraftEnrichment *enrichment_get_cached(const char *icao_hex);
```

**Step 2: Create `src/data/enrichment.cpp`**

```cpp
#include "enrichment.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>

#define MAX_CACHE 20

static AircraftEnrichment _cache[MAX_CACHE];
static char _cache_keys[MAX_CACHE][7];
static int _cache_count = 0;

static void (*_pending_callback)(AircraftEnrichment *) = nullptr;
static AircraftEnrichment *_pending_result = nullptr;

AircraftEnrichment *enrichment_get_cached(const char *icao_hex) {
    for (int i = 0; i < _cache_count; i++) {
        if (strcmp(_cache_keys[i], icao_hex) == 0 && _cache[i].loaded) {
            return &_cache[i];
        }
    }
    return nullptr;
}

static AircraftEnrichment *get_or_create_cache_entry(const char *icao_hex) {
    // Check existing
    for (int i = 0; i < _cache_count; i++) {
        if (strcmp(_cache_keys[i], icao_hex) == 0) return &_cache[i];
    }
    // Create new (evict oldest if full)
    int idx = _cache_count < MAX_CACHE ? _cache_count++ : 0;
    memset(&_cache[idx], 0, sizeof(AircraftEnrichment));
    strlcpy(_cache_keys[idx], icao_hex, 7);
    return &_cache[idx];
}

static void fetch_task(void *param) {
    char *icao_hex = (char *)param;
    AircraftEnrichment *entry = get_or_create_cache_entry(icao_hex);
    entry->loading = true;

    // Fetch route info from adsb.lol
    {
        char url[128];
        snprintf(url, sizeof(url), "https://api.adsb.lol/v2/hex/%s", icao_hex);
        HTTPClient http;
        http.begin(url);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            JsonDocument doc;
            if (!deserializeJson(doc, http.getString())) {
                JsonArray ac = doc["ac"].as<JsonArray>();
                if (ac.size() > 0) {
                    JsonObject obj = ac[0];
                    strlcpy(entry->airline, obj["ownOp"] | "", sizeof(entry->airline));
                    // Route: "KJFK-KLAX" format
                    const char *route = obj["route"] | "";
                    if (strlen(route) > 0) {
                        // Parse origin-destination
                        char *dash = strchr((char *)route, '-');
                        if (dash) {
                            *dash = '\0';
                            strlcpy(entry->origin_airport, route, sizeof(entry->origin_airport));
                            strlcpy(entry->destination_airport, dash + 1, sizeof(entry->destination_airport));
                        }
                    }
                }
            }
        }
        http.end();
    }

    // Fetch photo from planespotters.net
    {
        char url[128];
        snprintf(url, sizeof(url),
                 "https://api.planespotters.net/pub/photos/hex/%s", icao_hex);
        HTTPClient http;
        http.begin(url);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            JsonDocument doc;
            if (!deserializeJson(doc, http.getString())) {
                JsonArray photos = doc["photos"].as<JsonArray>();
                if (photos.size() > 0) {
                    const char *thumb = photos[0]["thumbnail_large"]["src"] | "";
                    strlcpy(entry->photo_url, thumb, sizeof(entry->photo_url));
                }
            }
        }
        http.end();
    }

    entry->loaded = true;
    entry->loading = false;

    if (_pending_callback) {
        _pending_callback(entry);
        _pending_callback = nullptr;
    }

    free(icao_hex);
    vTaskDelete(nullptr);
}

void enrichment_fetch(const char *icao_hex, const char *registration,
                      void (*callback)(AircraftEnrichment *data)) {
    // Check cache first
    AircraftEnrichment *cached = enrichment_get_cached(icao_hex);
    if (cached) {
        callback(cached);
        return;
    }

    _pending_callback = callback;
    char *hex_copy = strdup(icao_hex);
    xTaskCreatePinnedToCore(fetch_task, "enrich", 8192, hex_copy, 0, nullptr, 1);
}
```

**Step 3: Create `src/ui/detail_card.h`**

```cpp
#pragma once
#include "lvgl.h"
#include "../data/aircraft.h"

// Show detail card for an aircraft (bottom sheet)
void detail_card_show(const Aircraft *ac);

// Hide the detail card
void detail_card_hide();

// Initialize the detail card (call once in setup)
void detail_card_init(lv_obj_t *parent);

// Returns true if card is currently visible
bool detail_card_is_visible();
```

**Step 4: Create `src/ui/detail_card.cpp`**

```cpp
#include "detail_card.h"
#include "geo.h"
#include "../config.h"
#include "../data/enrichment.h"
#include "../pins_config.h"

static lv_obj_t *_card = nullptr;
static lv_obj_t *_callsign_label = nullptr;
static lv_obj_t *_reg_label = nullptr;
static lv_obj_t *_type_label = nullptr;
static lv_obj_t *_airline_label = nullptr;
static lv_obj_t *_route_label = nullptr;
static lv_obj_t *_alt_label = nullptr;
static lv_obj_t *_spd_label = nullptr;
static lv_obj_t *_hdg_label = nullptr;
static lv_obj_t *_vrate_label = nullptr;
static lv_obj_t *_dist_label = nullptr;
static lv_obj_t *_squawk_label = nullptr;
static lv_obj_t *_photo_placeholder = nullptr;
static lv_obj_t *_loading_spinner = nullptr;

static bool _visible = false;
static Aircraft _current_ac;

#define CARD_H 350
#define CARD_BG lv_color_hex(0x141428)
#define CARD_TEXT lv_color_hex(0xccccdd)
#define CARD_ACCENT lv_color_hex(0x4488ff)
#define CARD_DIM lv_color_hex(0x666688)

static void on_enrichment_ready(AircraftEnrichment *data) {
    if (!_visible) return;

    if (data->airline[0]) {
        lv_label_set_text(_airline_label, data->airline);
    }
    if (data->origin_airport[0] && data->destination_airport[0]) {
        lv_label_set_text_fmt(_route_label, "%s  ->  %s",
                              data->origin_airport, data->destination_airport);
    }
    if (data->photo_url[0]) {
        // Photo display would require image decoding from URL
        // For now, show the URL as text placeholder
        lv_label_set_text(_photo_placeholder, "Photo available");
        lv_obj_set_style_text_color(_photo_placeholder, CARD_ACCENT, 0);
    }

    if (_loading_spinner) lv_obj_add_flag(_loading_spinner, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_data_row(lv_obj_t *parent, const char *label_text,
                                int x, int y, lv_obj_t **value_label) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, CARD_DIM, 0);
    lv_obj_set_pos(lbl, x, y);

    *value_label = lv_label_create(parent);
    lv_label_set_text(*value_label, "--");
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(*value_label, CARD_TEXT, 0);
    lv_obj_set_pos(*value_label, x, y + 16);

    return lbl;
}

void detail_card_init(lv_obj_t *parent) {
    _card = lv_obj_create(parent);
    lv_obj_set_size(_card, LCD_H_RES, CARD_H);
    lv_obj_set_pos(_card, 0, LCD_V_RES); // start off-screen (below)
    lv_obj_set_style_bg_color(_card, CARD_BG, 0);
    lv_obj_set_style_bg_opa(_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_radius(_card, 12, 0);
    lv_obj_set_style_pad_all(_card, 16, 0);
    lv_obj_clear_flag(_card, LV_OBJ_FLAG_SCROLLABLE);

    // Drag handle indicator
    lv_obj_t *handle = lv_obj_create(_card);
    lv_obj_set_size(handle, 40, 4);
    lv_obj_set_style_bg_color(handle, CARD_DIM, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, -8);

    // Callsign (large)
    _callsign_label = lv_label_create(_card);
    lv_obj_set_style_text_font(_callsign_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_callsign_label, lv_color_white(), 0);
    lv_obj_set_pos(_callsign_label, 0, 8);

    // Registration
    _reg_label = lv_label_create(_card);
    lv_obj_set_style_text_font(_reg_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_reg_label, CARD_DIM, 0);
    lv_obj_set_pos(_reg_label, 0, 40);

    // Airline
    _airline_label = lv_label_create(_card);
    lv_obj_set_style_text_font(_airline_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_airline_label, CARD_ACCENT, 0);
    lv_obj_set_pos(_airline_label, 0, 60);

    // Type
    _type_label = lv_label_create(_card);
    lv_obj_set_style_text_font(_type_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_type_label, CARD_TEXT, 0);
    lv_obj_set_pos(_type_label, 0, 82);

    // Route
    _route_label = lv_label_create(_card);
    lv_obj_set_style_text_font(_route_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_route_label, CARD_TEXT, 0);
    lv_obj_set_pos(_route_label, 0, 104);

    // Photo placeholder (right side)
    _photo_placeholder = lv_label_create(_card);
    lv_label_set_text(_photo_placeholder, "");
    lv_obj_set_style_text_font(_photo_placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_photo_placeholder, CARD_DIM, 0);
    lv_obj_set_pos(_photo_placeholder, 600, 8);

    // Loading spinner
    _loading_spinner = lv_spinner_create(_card);
    lv_obj_set_size(_loading_spinner, 30, 30);
    lv_obj_set_pos(_loading_spinner, 700, 16);
    lv_obj_add_flag(_loading_spinner, LV_OBJ_FLAG_HIDDEN);

    // Data grid (bottom section)
    int y = 140;
    make_data_row(_card, "ALTITUDE", 0, y, &_alt_label);
    make_data_row(_card, "SPEED", 160, y, &_spd_label);
    make_data_row(_card, "HEADING", 320, y, &_hdg_label);
    make_data_row(_card, "V/S", 480, y, &_vrate_label);
    make_data_row(_card, "DISTANCE", 640, y, &_dist_label);
    make_data_row(_card, "SQUAWK", 800, y, &_squawk_label);

    // Tap outside to close
    lv_obj_add_event_cb(_card, [](lv_event_t *e) {
        detail_card_hide();
    }, LV_EVENT_CLICKED, nullptr);

    _visible = false;
}

void detail_card_show(const Aircraft *ac) {
    memcpy(&_current_ac, ac, sizeof(Aircraft));

    // Populate labels
    lv_label_set_text(_callsign_label, ac->callsign[0] ? ac->callsign : ac->icao_hex);
    lv_label_set_text_fmt(_reg_label, "%s  |  %s", ac->registration, ac->icao_hex);
    lv_label_set_text(_type_label, ac->type_code);
    lv_label_set_text(_airline_label, "");
    lv_label_set_text(_route_label, "");
    lv_label_set_text(_photo_placeholder, "");

    // Live data
    if (ac->on_ground) lv_label_set_text(_alt_label, "GND");
    else lv_label_set_text_fmt(_alt_label, "%d ft", ac->altitude);
    lv_label_set_text_fmt(_spd_label, "%d kts", ac->speed);
    lv_label_set_text_fmt(_hdg_label, "%03d\xC2\xB0", ac->heading);
    lv_label_set_text_fmt(_vrate_label, "%+d fpm", ac->vert_rate);

    float dist = MapProjection::distance_nm(HOME_LAT, HOME_LON, ac->lat, ac->lon);
    lv_label_set_text_fmt(_dist_label, "%.1f nm", dist);
    lv_label_set_text_fmt(_squawk_label, "%04d", ac->squawk);

    // Slide in
    _visible = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _card);
    lv_anim_set_values(&a, LCD_V_RES, LCD_V_RES - CARD_H);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_y((lv_obj_t *)obj, v);
    });
    lv_anim_start(&a);

    // Fetch enrichment
    lv_obj_clear_flag(_loading_spinner, LV_OBJ_FLAG_HIDDEN);
    enrichment_fetch(ac->icao_hex, ac->registration, on_enrichment_ready);
}

void detail_card_hide() {
    if (!_visible) return;
    _visible = false;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _card);
    lv_anim_set_values(&a, lv_obj_get_y(_card), LCD_V_RES);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_y((lv_obj_t *)obj, v);
    });
    lv_anim_start(&a);
}

bool detail_card_is_visible() {
    return _visible;
}
```

**Step 5: Wire up aircraft tap detection in map_view.cpp**

Replace the simple click handler in `map_view_init()` with aircraft hit-testing:

```cpp
    // Replace the existing CLICKED handler with this:
    lv_obj_add_event_cb(_canvas, [](lv_event_t *e) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);

        // Adjust for canvas position
        int tx = point.x;
        int ty = point.y - 30; // status bar offset

        if (detail_card_is_visible()) {
            detail_card_hide();
            return;
        }

        // Hit test against aircraft (20px radius)
        if (!_list->lock(pdMS_TO_TICKS(50))) return;
        for (int i = 0; i < _list->count; i++) {
            int sx, sy;
            if (_proj.to_screen(_list->aircraft[i].lat, _list->aircraft[i].lon, sx, sy)) {
                int dx = tx - sx;
                int dy = ty - sy;
                if (dx * dx + dy * dy < 400) { // 20px radius
                    Aircraft ac_copy = _list->aircraft[i];
                    _list->unlock();
                    detail_card_show(&ac_copy);
                    return;
                }
            }
        }
        _list->unlock();

        // No aircraft hit — cycle zoom
        _zoom_idx = (_zoom_idx + 1) % 3;
        _proj.radius_nm = ZOOM_LEVELS[_zoom_idx];
        lv_obj_invalidate(_canvas);
    }, LV_EVENT_CLICKED, nullptr);
```

Add `#include "detail_card.h"` to map_view.cpp.

**Step 6: Initialize detail card in main.cpp**

In `setup()`, after `views_init()`:

```cpp
    detail_card_init(screen);
```

Add `#include "ui/detail_card.h"`.

**Step 7: Compile, flash, verify**

Expected: Tap an aircraft dot on the map view → bottom sheet slides up with callsign, type, live data. After a moment, airline name and route appear (fetched from API). Tap card or empty space to dismiss.

**Step 8: Commit**

```bash
git add src/data/enrichment.* src/ui/detail_card.*
git commit -m "feat: detail card with enrichment data and slide animation"
```

---

## Task 8: Alert System

**Goal:** Toast notifications for military aircraft, emergency squawk, and watchlist matches.

**Files:**
- Create: `src/ui/alerts.h`
- Create: `src/ui/alerts.cpp`
- Modify: `src/data/fetcher.cpp` (trigger alerts on new detections)
- Modify: `src/main.cpp` (init alerts)

**Step 1: Create `src/ui/alerts.h`**

```cpp
#pragma once
#include "lvgl.h"

enum AlertType {
    ALERT_MILITARY,
    ALERT_EMERGENCY,
    ALERT_WATCHLIST,
    ALERT_INTERESTING,
};

// Initialize alert system (call once)
void alerts_init(lv_obj_t *parent);

// Show a toast notification. Auto-dismisses after timeout_ms.
void alerts_show(AlertType type, const char *title, const char *detail,
                 uint32_t timeout_ms = 10000);
```

**Step 2: Create `src/ui/alerts.cpp`**

```cpp
#include "alerts.h"
#include "../pins_config.h"

static lv_obj_t *_toast = nullptr;
static lv_obj_t *_toast_title = nullptr;
static lv_obj_t *_toast_detail = nullptr;
static lv_obj_t *_toast_icon = nullptr;
static lv_timer_t *_dismiss_timer = nullptr;

#define TOAST_W 500
#define TOAST_H 50
#define TOAST_Y 35  // just below status bar

static lv_color_t alert_color(AlertType type) {
    switch (type) {
        case ALERT_EMERGENCY: return lv_color_hex(0xff2222);
        case ALERT_MILITARY:  return lv_color_hex(0xffaa00);
        case ALERT_WATCHLIST:  return lv_color_hex(0x4488ff);
        case ALERT_INTERESTING: return lv_color_hex(0x00cc88);
    }
    return lv_color_white();
}

static const char *alert_icon(AlertType type) {
    switch (type) {
        case ALERT_EMERGENCY: return LV_SYMBOL_WARNING;
        case ALERT_MILITARY:  return LV_SYMBOL_GPS;
        case ALERT_WATCHLIST:  return LV_SYMBOL_EYE_OPEN;
        case ALERT_INTERESTING: return LV_SYMBOL_OK;
    }
    return LV_SYMBOL_BELL;
}

static void dismiss_toast(lv_timer_t *t) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _toast);
    lv_anim_set_values(&a, lv_obj_get_y(_toast), -TOAST_H);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_y((lv_obj_t *)obj, v);
    });
    lv_anim_start(&a);

    if (_dismiss_timer) {
        lv_timer_delete(_dismiss_timer);
        _dismiss_timer = nullptr;
    }
}

void alerts_init(lv_obj_t *parent) {
    _toast = lv_obj_create(parent);
    lv_obj_set_size(_toast, TOAST_W, TOAST_H);
    lv_obj_set_pos(_toast, (LCD_H_RES - TOAST_W) / 2, -TOAST_H); // hidden above
    lv_obj_set_style_bg_color(_toast, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(_toast, 8, 0);
    lv_obj_set_style_border_width(_toast, 2, 0);
    lv_obj_set_style_border_color(_toast, lv_color_white(), 0);
    lv_obj_set_style_pad_all(_toast, 8, 0);
    lv_obj_clear_flag(_toast, LV_OBJ_FLAG_SCROLLABLE);

    _toast_icon = lv_label_create(_toast);
    lv_obj_set_style_text_font(_toast_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(_toast_icon, LV_ALIGN_LEFT_MID, 0, 0);

    _toast_title = lv_label_create(_toast);
    lv_obj_set_style_text_font(_toast_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_toast_title, lv_color_white(), 0);
    lv_obj_align(_toast_title, LV_ALIGN_LEFT_MID, 32, -8);

    _toast_detail = lv_label_create(_toast);
    lv_obj_set_style_text_font(_toast_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_toast_detail, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(_toast_detail, LV_ALIGN_LEFT_MID, 32, 10);

    // Tap to dismiss
    lv_obj_add_event_cb(_toast, [](lv_event_t *e) {
        dismiss_toast(nullptr);
    }, LV_EVENT_CLICKED, nullptr);
}

void alerts_show(AlertType type, const char *title, const char *detail,
                 uint32_t timeout_ms) {
    lv_color_t color = alert_color(type);

    lv_obj_set_style_border_color(_toast, color, 0);
    lv_label_set_text(_toast_icon, alert_icon(type));
    lv_obj_set_style_text_color(_toast_icon, color, 0);
    lv_label_set_text(_toast_title, title);
    lv_label_set_text(_toast_detail, detail);

    // Slide in from top
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _toast);
    lv_anim_set_values(&a, -TOAST_H, TOAST_Y);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
        lv_obj_set_y((lv_obj_t *)obj, v);
    });
    lv_anim_start(&a);

    // Auto-dismiss timer
    if (_dismiss_timer) lv_timer_delete(_dismiss_timer);
    _dismiss_timer = lv_timer_create(dismiss_toast, timeout_ms, nullptr);
    lv_timer_set_repeat_count(_dismiss_timer, 1);
}
```

**Step 3: Add alert checking in fetcher.cpp**

Add to `parse_aircraft_json()`, after the aircraft loop, before unlock:

```cpp
    // Check for new alerts (compare against previous state)
    for (int i = 0; i < new_count; i++) {
        Aircraft &a = _aircraft_list->aircraft[i];
        if (a.is_emergency) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Squawk %04d - %s", a.squawk,
                     a.squawk == 7500 ? "HIJACK" : a.squawk == 7600 ? "COMMS FAILURE" : "EMERGENCY");
            alerts_show(ALERT_EMERGENCY, a.callsign[0] ? a.callsign : a.icao_hex, msg);
        } else if (a.is_military) {
            // Only alert for newly-seen military aircraft (check if it existed before)
            // Simple approach: alert if trail_count <= 1 (first or second sighting)
            if (a.trail_count <= 1) {
                alerts_show(ALERT_MILITARY, a.callsign[0] ? a.callsign : a.icao_hex,
                           a.type_code);
            }
        }
    }
```

Note: `alerts_show()` must be called from the LVGL task context, not from the fetch task. Use `lv_msg_send()` or a flag + main loop check. Simplest approach: use a ring buffer of pending alerts checked in the LVGL timer.

**Step 4: Initialize in main.cpp**

```cpp
    alerts_init(screen);
```

Add `#include "ui/alerts.h"`.

**Step 5: Compile, flash, verify**

Expected: When a military aircraft or emergency squawk appears, a toast notification slides down from the top with colored border and icon. Tap to dismiss or auto-dismisses after 10 seconds.

**Step 6: Commit**

```bash
git add src/ui/alerts.*
git commit -m "feat: alert system with toast notifications"
```

---

## Task 9: Settings Screen

**Goal:** Gear icon on status bar opens a settings overlay for configuring home location, WiFi, radius, and units.

**Files:**
- Create: `src/ui/settings.h`
- Create: `src/ui/settings.cpp`
- Create: `src/data/storage.h`
- Create: `src/data/storage.cpp`
- Modify: `src/ui/status_bar.cpp` (add gear icon)
- Modify: `src/config.h` (make values loadable)

**Step 1: Create `src/data/storage.h`**

```cpp
#pragma once

struct UserConfig {
    char wifi_ssid[33];
    char wifi_pass[65];
    float home_lat;
    float home_lon;
    int radius_nm;
    bool use_metric;
    char watchlist[10][7]; // up to 10 ICAO hex codes
    int watchlist_count;
};

// Load config from SD card (or SPIFFS). Returns defaults if not found.
UserConfig storage_load_config();

// Save config to SD card
void storage_save_config(const UserConfig &cfg);
```

**Step 2: Create `src/data/storage.cpp`**

```cpp
#include "storage.h"
#include "../config.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>

#define CONFIG_PATH "/config.json"

UserConfig storage_load_config() {
    UserConfig cfg;
    strlcpy(cfg.wifi_ssid, WIFI_SSID, sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, WIFI_PASS, sizeof(cfg.wifi_pass));
    cfg.home_lat = HOME_LAT;
    cfg.home_lon = HOME_LON;
    cfg.radius_nm = ADSB_RADIUS_NM;
    cfg.use_metric = false;
    cfg.watchlist_count = 0;

    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD card mount failed, using defaults");
        return cfg;
    }

    File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
    if (!f) return cfg;

    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        f.close();
        return cfg;
    }
    f.close();

    strlcpy(cfg.wifi_ssid, doc["wifi_ssid"] | WIFI_SSID, sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_pass, doc["wifi_pass"] | WIFI_PASS, sizeof(cfg.wifi_pass));
    cfg.home_lat = doc["home_lat"] | HOME_LAT;
    cfg.home_lon = doc["home_lon"] | HOME_LON;
    cfg.radius_nm = doc["radius_nm"] | ADSB_RADIUS_NM;
    cfg.use_metric = doc["use_metric"] | false;

    JsonArray wl = doc["watchlist"].as<JsonArray>();
    cfg.watchlist_count = 0;
    for (JsonVariant v : wl) {
        if (cfg.watchlist_count >= 10) break;
        strlcpy(cfg.watchlist[cfg.watchlist_count++], v.as<const char *>(), 7);
    }

    return cfg;
}

void storage_save_config(const UserConfig &cfg) {
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD card mount failed, cannot save config");
        return;
    }

    File f = SD_MMC.open(CONFIG_PATH, FILE_WRITE);
    if (!f) return;

    JsonDocument doc;
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["wifi_pass"] = cfg.wifi_pass;
    doc["home_lat"] = cfg.home_lat;
    doc["home_lon"] = cfg.home_lon;
    doc["radius_nm"] = cfg.radius_nm;
    doc["use_metric"] = cfg.use_metric;

    JsonArray wl = doc["watchlist"].to<JsonArray>();
    for (int i = 0; i < cfg.watchlist_count; i++) {
        wl.add(cfg.watchlist[i]);
    }

    serializeJsonPretty(doc, f);
    f.close();
}
```

**Step 3: Create settings UI**

`src/ui/settings.h` and `src/ui/settings.cpp` — a full-screen overlay with text areas for lat/lon/SSID and toggles for units. This follows standard LVGL form patterns with `lv_textarea`, `lv_keyboard`, `lv_switch`, and `lv_slider`.

Implementation follows the same LVGL patterns used in the detail card (overlay with slide animation). Key elements:
- Text area for latitude/longitude
- Text area for WiFi SSID/password
- Slider for radius (5-100nm)
- Switch for metric/imperial
- Save button that calls `storage_save_config()` and restarts the fetcher with new coordinates

**Step 4: Add gear icon to status bar**

In `status_bar.cpp`, add a clickable gear icon on the right side of the bar that triggers the settings overlay.

**Step 5: Compile, flash, verify**

Expected: Tap gear icon → settings overlay slides in. Change values, tap save → config written to SD card. Next boot loads saved config.

**Step 6: Commit**

```bash
git add src/data/storage.* src/ui/settings.*
git commit -m "feat: settings screen with SD card persistence"
```

---

## Task 10: Map Tiles (Stretch Goal)

**Goal:** Fetch and cache OpenStreetMap tiles as the map background.

**Files:**
- Create: `src/ui/tile_cache.h`
- Create: `src/ui/tile_cache.cpp`
- Modify: `src/ui/map_view.cpp` (render tiles under aircraft)

This task involves:
1. Calculating which OSM tiles are needed for the current viewport
2. Checking SD card cache for each tile
3. Fetching missing tiles from `tile.openstreetmap.org`
4. Decoding PNG to RGB565 in PSRAM (using LVGL's PNG decoder or esp_jpeg for JPEG tiles)
5. Rendering tiles as the map background layer before drawing aircraft

This is the most complex task and may require iterating on memory management and tile decode performance. Implementation details will depend on learnings from Tasks 1-9.

**Commit:**

```bash
git commit -m "feat: OpenStreetMap tile cache and rendering"
```

---

## Summary: Build Order

| Task | What You Get | Dependencies |
|------|-------------|--------------|
| 1 | Screen + touch working, LVGL hello world | None |
| 2 | WiFi connected, aircraft data flowing | Task 1 |
| 3 | Status bar + swipeable view shell | Task 1 |
| 4 | Aircraft on map with trails and zoom | Tasks 2, 3 |
| 5 | Radar sweep with phosphor blips | Tasks 2, 3 |
| 6 | Split-flap arrivals board | Tasks 2, 3 |
| 7 | Tap aircraft → detail card with enrichment | Tasks 4, 5, 6 |
| 8 | Military/emergency toast alerts | Task 2 |
| 9 | Settings screen with SD persistence | Task 1 |
| 10 | Map tile background (stretch) | Task 4 |

Tasks 4, 5, 6 can be built in parallel (independent views). Task 7 depends on having at least one view working.
