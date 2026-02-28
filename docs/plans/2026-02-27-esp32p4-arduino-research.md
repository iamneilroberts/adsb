# GUITION JC1060P470C ESP32-P4 Board: Arduino Setup Research

**Date:** 2026-02-27
**Status:** CONFIRMED from actual source code and working examples

---

## 1. Board Overview

- **MCU:** ESP32-P4 dual-core RISC-V @ 400MHz (configurable to 360MHz in Arduino)
- **Display:** 7.0" IPS, 1024x600, MIPI-DSI, JD9165 display driver IC
- **Touch:** GT911 capacitive touch controller via I2C
- **WiFi/BT:** ESP32-C6 co-processor via SDIO (ESP-Hosted)
- **Memory:** 32MB PSRAM, 16MB Flash
- **Backlight:** PWM on GPIO23
- **SD Card:** TF card slot (FAT32, max 32GB)
- **USB:** CH340 USB-to-UART on USB1 port
- **Power:** Requires 600mA+, lithium battery interface circuit

---

## 2. Arduino IDE Setup

### Board Manager URL (REQUIRED: Development/alpha version)

```
https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
```

**Required:** arduino-esp32 >= v3.1.0 (the stable URL may not have ESP32-P4 support yet)

### Board Selection

- **Board:** `ESP32P4 Dev Module`
- **Programmer:** `esptool`

### Arduino IDE Tool Settings (from vendor screenshots and ESPHome config)

| Setting | Value |
|---------|-------|
| Board | ESP32P4 Dev Module |
| PSRAM | Enabled (Hex mode, 200MHz) |
| Flash Size | 16MB |
| Flash Mode | QIO |
| CPU Frequency | 360MHz |
| Upload Port | USB1 |
| USB CDC On Boot | Enable if using USB port, Disable for UART |

**IMPORTANT:** After uploading, you must power-cycle the device to enter flashing mode again for subsequent uploads.

---

## 3. Pin Definitions (CONFIRMED from vendor source code)

### Display (MIPI-DSI - no user-assignable GPIO pins)

MIPI-DSI uses dedicated internal lanes on ESP32-P4 -- not GPIO-mapped. Only the reset and backlight are GPIO.

```c
#define LCD_H_RES         1024
#define LCD_V_RES         600
#define LCD_RST           5       // GPIO5 - display reset
#define LCD_LED           -1      // Not used directly; backlight via LEDC on GPIO23
#define EXAMPLE_PIN_NUM_BK_LIGHT  GPIO_NUM_23  // Backlight PWM
```

### Touch (GT911 via I2C)

```c
#define TP_I2C_SDA        7       // GPIO7
#define TP_I2C_SCL        8       // GPIO8
#define TP_RST            -1      // Not connected in basic demos; GPIO22 in ESPHome config
#define TP_INT            -1      // Not connected in basic demos; GPIO21 in ESPHome config
```

The ESPHome device config reveals additional touch pins:
- **Touch Reset:** GPIO22
- **Touch Interrupt:** GPIO21

### ESP32-C6 WiFi Co-processor (SDIO)

From ESPHome device.yaml (CONFIRMED):
```yaml
esp32_hosted:
  variant: esp32c6
  reset_pin: GPIO54
  cmd_pin:   GPIO19
  clk_pin:   GPIO18
  d0_pin:    GPIO14
  d1_pin:    GPIO15
  d2_pin:    GPIO16
  d3_pin:    GPIO17
  active_high: true
```

### MIPI-DSI PHY Power (Internal LDO)

```c
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN        3     // LDO_VO3 -> VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV  2500  // 2.5V
```

### SD Card Power (from ESPHome)

```yaml
esp_ldo:
  - channel: 4
    voltage: 2.7V    # SD card power via LDO channel 4
```

SD card GPIO pins are NOT documented in the Arduino demos. The SD card uses the SDMMC peripheral. Exact GPIOs need to come from the schematic (available at: https://cdn.static.spotpear.cn/uploads/picture/learn/ESP32/ESP32-P4-7inch/Schematic.rar)

### I2C Devices on the Bus (from ESPHome)

```yaml
# Address 0x14 or 0x5D = GT911 touch (alternates based on reset timing)
# Address 0x18, 0x32, 0x36, 0x40 = other I2C devices on bus
```

---

## 4. Display Initialization (JD9165 via MIPI-DSI)

The display uses a **custom JD9165 driver** that wraps ESP-IDF's MIPI-DSI APIs. The vendor provides the driver as source files bundled with the sketch (NOT as an installable library).

### Source Files Required (copy into your sketch's `src/lcd/` directory)

```
src/lcd/esp_lcd_jd9165.h     - JD9165 panel header with config macros
src/lcd/esp_lcd_jd9165.c     - JD9165 panel driver (vendor init sequence)
src/lcd/jd9165_lcd.h         - C++ wrapper class header
src/lcd/jd9165_lcd.cpp       - C++ wrapper class implementation
```

### Key Configuration Macros (from esp_lcd_jd9165.h)

```c
// MIPI-DSI bus: 2 data lanes @ 750 Mbps
#define JD9165_PANEL_BUS_DSI_2CH_CONFIG()                \
    {                                                    \
        .bus_id = 0,                                     \
        .num_data_lanes = 2,                             \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,    \
        .lane_bit_rate_mbps = 750,                       \
    }

// DBI command interface
#define JD9165_PANEL_IO_DBI_CONFIG()  \
    {                                 \
        .virtual_channel = 0,         \
        .lcd_cmd_bits = 8,            \
        .lcd_param_bits = 8,          \
    }

// DPI pixel interface: 1024x600 @ ~60Hz, 52MHz pixel clock, RGB565
#define JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(px_format) \
    {                                                    \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,    \
        .dpi_clock_freq_mhz = 52,                        \
        .pixel_format = px_format,                       \
        .num_fbs = 1,                                    \
        .video_timing = {                                \
            .h_size = 1024,                              \
            .v_size = 600,                               \
            .hsync_pulse_width = 20,                     \
            .hsync_back_porch = 160,                     \
            .hsync_front_porch = 160,                    \
            .vsync_pulse_width = 10,                     \
            .vsync_back_porch = 23,                      \
            .vsync_front_porch = 12,                     \
        },                                               \
        .flags = { .use_dma2d = true },                  \
    }
```

### LCD Initialization Sequence (from jd9165_lcd.cpp)

```cpp
jd9165_lcd lcd = jd9165_lcd(LCD_RST);  // LCD_RST = GPIO5

void setup() {
    lcd.begin();  // This does:
    // 1. Enable MIPI DSI PHY power (LDO channel 3, 2.5V)
    // 2. Init backlight PWM (LEDC on GPIO23, 20kHz, 10-bit)
    // 3. Turn backlight OFF
    // 4. Create MIPI-DSI bus (2 lanes, 750 Mbps)
    // 5. Create DBI command IO
    // 6. Create JD9165 panel with vendor init commands
    // 7. Reset panel, init panel
    // 8. Turn backlight ON (100%)
}
```

---

## 5. Touch Initialization (GT911 via I2C)

### Source Files Required (copy into your sketch's `src/touch/` directory)

```
src/touch/esp_lcd_touch.h         - Touch abstraction header
src/touch/esp_lcd_touch.c         - Touch abstraction implementation
src/touch/esp_lcd_touch_gt911.h   - GT911 specific header
src/touch/esp_lcd_touch_gt911.c   - GT911 specific driver
src/touch/gt911_touch.h           - C++ wrapper class header
src/touch/gt911_touch.cpp         - C++ wrapper class implementation
```

### GT911 Initialization (from gt911_touch.cpp)

```cpp
gt911_touch touch = gt911_touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);
// SDA=GPIO7, SCL=GPIO8, RST=-1, INT=-1

void setup() {
    // I2C bus MUST be initialized BEFORE touch.begin()
    i2c_master_bus_config_t i2c_bus_conf = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = (gpio_num_t)TP_I2C_SDA,   // GPIO7
        .scl_io_num = (gpio_num_t)TP_I2C_SCL,    // GPIO8
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = 1 },
    };
    i2c_new_master_bus(&i2c_bus_conf, &i2c_handle);

    touch.begin();  // This does:
    // 1. Gets I2C bus handle (I2C_NUM_1)
    // 2. Creates I2C panel IO with GT911 config @ 100kHz
    // 3. Configures touch: x_max=1024, y_max=600
    // 4. Creates GT911 touch driver
}
```

**CRITICAL NOTE:** The I2C bus must be initialized manually with `i2c_new_master_bus()` BEFORE calling `touch.begin()`. The touch driver retrieves the bus handle internally via `i2c_master_get_bus_handle(1, &i2c_handle)`.

---

## 6. LVGL Configuration

### Two Working Versions Available

| Version | Demo | Notes |
|---------|------|-------|
| **LVGL v8.4.0** (v8.3+ compatible) | `lvgl_demo_v8` | LVGL v8 API (lv_disp_drv_t), simpler |
| **LVGL v9.2.2** | `lvgl_v9_sw_rotation` | LVGL v9 API (lv_display_t), supports PPA rotation |

### lv_conf.h Essential Settings for LVGL v8

```c
#define LV_COLOR_DEPTH          16        // RGB565
#define LV_COLOR_16_SWAP        0         // No byte swap for MIPI-DSI
#define LV_MEM_CUSTOM           1         // Use system malloc (for PSRAM)
#define LV_TICK_CUSTOM           1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_MEMCPY_MEMSET_STD    1
#define LV_ATTRIBUTE_FAST_MEM   IRAM_ATTR
#define LV_DPI_DEF              130
#define LV_DISP_DEF_REFR_PERIOD 15        // ms
#define LV_FONT_MONTSERRAT_14   1         // Default font
// Enable demos:
#define LV_USE_DEMO_WIDGETS     1
#define LV_USE_DEMO_BENCHMARK   1
```

**IMPORTANT:** You must move the `demos` folder from inside the lvgl library into `lvgl/src/demos` for the demo includes to work.

### lv_conf.h Essential Settings for LVGL v9

```c
#define LV_COLOR_DEPTH          16
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (64 * 1024U)
#define LV_DEF_REFR_PERIOD      33
#define LV_DPI_DEF              130
#define LV_USE_OS               LV_OS_NONE
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#define LV_USE_DRAW_SW          1
#define LV_DRAW_SW_COMPLEX      1
// Enable demos:
#define LV_USE_DEMO_WIDGETS     1
```

### lv_conf.h Placement

The `lv_conf.h` file must be placed **in the same root directory as the `lvgl` library folder** -- NOT inside the lvgl folder.

```
Arduino/libraries/
  lvgl/           <-- the library
  lv_conf.h       <-- NEXT TO lvgl, not inside it
```

---

## 7. Working Minimal LVGL v8 Example (Complete)

```cpp
#pragma GCC push_options
#pragma GCC optimize("O3")

#include <Arduino.h>
#include "lvgl.h"
#include "driver/i2c_master.h"
#include "demos/lv_demos.h"
#include "pins_config.h"
#include "src/lcd/jd9165_lcd.h"
#include "src/touch/gt911_touch.h"

bsp_lcd_handles_t lcd_panels;
jd9165_lcd lcd = jd9165_lcd(LCD_RST);          // LCD_RST = 5
gt911_touch touch = gt911_touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf, *buf1;

static bool flush_ready_cb(esp_lcd_panel_handle_t panel,
    esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
    lv_disp_flush_ready((lv_disp_drv_t *)user_ctx);
    return false;
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    lcd.lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, &color_p->full);
}

void my_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t x, y;
    if (touch.getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setup() {
    Serial.begin(115200);

    // Init I2C bus (MUST be before touch.begin())
    i2c_master_bus_handle_t i2c_handle = NULL;
    i2c_master_bus_config_t i2c_bus_conf = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = (gpio_num_t)TP_I2C_SDA,
        .scl_io_num = (gpio_num_t)TP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = 1 },
    };
    i2c_new_master_bus(&i2c_bus_conf, &i2c_handle);

    lcd.begin();
    touch.begin();
    lcd.get_handle(&lcd_panels);

    // LVGL init
    lv_init();
    size_t buf_size = sizeof(int16_t) * LCD_H_RES * LCD_V_RES;
    buf  = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * LCD_V_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = true;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Register vsync callback for DPI panel
    esp_lcd_dpi_panel_event_callbacks_t cbs = {0};
    cbs.on_color_trans_done = flush_ready_cb;
    esp_lcd_dpi_panel_register_event_callbacks(lcd_panels.panel, &cbs, &disp_drv);

    lv_demo_widgets();
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

---

## 8. WiFi (ESP-Hosted ESP32-C6) in Arduino

### How It Works

The ESP32-C6 is connected via SDIO to the ESP32-P4. In arduino-esp32 v3.1+, calling `WiFi.begin()` or `WiFi.mode(WIFI_STA)` triggers the ESP-Hosted SDIO initialization automatically via the `esp_wifi_remote` + `esp_hosted` components that are built into the Arduino core.

### Working WiFi Scan Example (from vendor demo)

```cpp
#include <WiFi.h>

WiFi.mode(WIFI_STA);
WiFi.disconnect();
delay(100);

int n = WiFi.scanNetworks();
for (int i = 0; i < n; i++) {
    Serial.printf("%s (%ld dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
}
```

### Known Issues

- **SDIO init failures are common** -- reported in arduino-esp32 issue #11404
- Error: `sdmmc_init_ocr: send_op_cond returned 0x107` / `sdio card init failed`
- **Workaround:** Ensure proper power supply (600mA+), reset ESP32-C6 before WiFi init
- The WiFi initialization on ESP32-P4 is significantly different from other ESP32 variants; it relies on the ESP-Hosted framework with the C6 co-processor
- SDIO uses 4-bit data lines at 40MHz

### No Extra Code Needed

WiFi "just works" with the standard `WiFi.h` library -- the ESP-Hosted layer is transparent to the application. The vendor Wifi_scan demo calls `WiFi.scanNetworks()` directly.

---

## 9. PlatformIO Configuration

### CRITICAL: Official PlatformIO does NOT support ESP32-P4

You **must** use the **pioarduino** community fork.

### Working platformio.ini (from tyeth's adapted demo)

```ini
[platformio]
default_envs = p4_16mb

[common]
platform = https://github.com/pioarduino/platform-espressif32.git#53.03.13+github
framework = arduino

[env:p4_16mb]
extends = common
board = esp32-p4-evboard
board_build.psram = enabled
monitor_speed = 115200
lib_extra_dirs = lvgl
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

### Alternative Board Definitions

```ini
# Option 1: Using the EV board definition (has 16MB flash built-in)
board = esp32-p4-evboard

# Option 2: Using generic esp32-p4 (need to specify flash size)
board = esp32-p4
board_build.flash_size = 16MB
board_build.f_cpu = 360000000L
board_build.flash_mode = qio
board_build.psram = enabled
```

### Project Structure for PlatformIO

```
project/
  platformio.ini
  lv_conf.h                          <-- LVGL config (root level)
  lvgl/                              <-- LVGL library as local lib
  src/
    main.cpp                         <-- Your sketch
    lcd/
      esp_lcd_jd9165.h
      esp_lcd_jd9165.c
      jd9165_lcd.h
      jd9165_lcd.cpp
    touch/
      esp_lcd_touch.h
      esp_lcd_touch.c
      esp_lcd_touch_gt911.h
      esp_lcd_touch_gt911.c
      gt911_touch.h
      gt911_touch.cpp
    pins_config.h
```

---

## 10. Library Architecture -- What They Use

The vendor demos use a **direct / bare-metal approach**:
- **NOT** using `ESP32_Display_Panel` library (Espressif's high-level board library)
- **NOT** using `esp_lvgl_port` (Espressif's LVGL port component)
- **NOT** using `esp_brookesia` (Espressif's phone-like UI framework)
- **USING** direct ESP-IDF MIPI-DSI APIs wrapped in thin C++ classes
- **USING** direct ESP-IDF I2C + esp_lcd_touch APIs wrapped in thin C++ classes
- **USING** LVGL directly with manual flush/input callbacks

### ESP32_Display_Panel Status

The Espressif `ESP32_Display_Panel` library **does** support:
- JD9165 LCD controller
- GT911 touch controller
- ESP32-P4 target

But the JC1060P470C board is **NOT** in the supported boards list (only Jingcai ESP32-4848S040C is listed, which is an ESP32-S3 board with a different display). You would need to create a custom board definition if using this library.

---

## 11. Source Repositories

| Resource | URL |
|----------|-----|
| Vendor data pack (wegi1 mirror) | https://github.com/wegi1/ESP32P4-JC1060P470C-I_W_Y |
| Sukesh-AK data pack | https://github.com/sukesh-ak/JC1060P470C_I_W-GUITION-ESP32-P4_ESP32-C6 |
| Tyeth's PlatformIO-adapted demo | https://github.com/tyeth/Guition_JC1060P470C_I_W_LvGL_Arduino_Touch_Demo_ESP32P4 |
| ESPHome LVGL config | https://github.com/jtenniswood/esphome-lvgl/tree/main/guition-esp32-p4-jc1060p470 |
| SpotPear wiki | https://spotpear.com/wiki/ESP32-P4-Display-7inch-TouchScreen-JC1060P470C.html |
| ESP32_Display_Panel | https://github.com/esp-arduino-libs/ESP32_Display_Panel |
| pioarduino platform | https://github.com/pioarduino/platform-espressif32 |
| ESP-Hosted MCU | https://github.com/espressif/esp-hosted-mcu |
| JD9165 component (Espressif) | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_jd9165 |
| Vendor full download | http://pan.jczn1688.com/directlink/1/HMI%20display/JC1060P470C_I_W.zip |
| Schematic | https://cdn.static.spotpear.cn/uploads/picture/learn/ESP32/ESP32-P4-7inch/Schematic.rar |

---

## 12. Gotchas and Warnings

1. **Power supply must be 600mA+** -- insufficient power causes display flickering and SDIO failures
2. **USB1 port for flashing** -- must power-cycle device between uploads
3. **LVGL demos folder must be moved** -- copy `lvgl/demos/` into `lvgl/src/demos/` for includes to resolve
4. **lv_conf.h placement** -- must be in same directory AS (not inside) the lvgl library folder
5. **I2C must be initialized before touch** -- use `i2c_new_master_bus()` before `touch.begin()`
6. **WiFi SDIO init can fail** -- known issue, may need power cycle or proper C6 reset sequencing
7. **No SD card Arduino demo exists yet** -- SD card pins need to be extracted from schematic
8. **The board is NOT in ESP32_Display_Panel's supported list** -- you must either use the vendor's bundled drivers or create a custom board definition
9. **Display color format is RGB565** (`LV_COLOR_DEPTH 16`, `LV_COLOR_16_SWAP 0`)
10. **PSRAM is essential** -- frame buffers (1024*600*2 = ~1.2MB each) must be in SPIRAM
