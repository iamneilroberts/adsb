// ADS-B display for the Seeed SenseCAP Indicator (ESP32-S3, 480x480 RGB, FT6336 touch).
// Uses the compact ui_s3 interface, sized to the panel via LCD_H_RES / LCD_V_RES.
#include <Arduino.h>
#include "lvgl.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "pins.h"
#include "pins_s3.h"
#include "config.h"
#include "indicator_board.h"
#include "data/aircraft.h"
#include "data/fetcher.h"
#include "data/storage.h"
#include "data/error_log.h"
#include "data/enrichment.h"
#include "ui_s3/status_bar.h"
#include "ui_s3/views.h"
#include "ui_s3/detail_card.h"
#include "ui_s3/alerts.h"
#include "ui_s3/settings.h"

// Global touch state, read by view timers to defer heavy rendering during interaction
volatile bool touch_active = false;

AircraftList aircraft_list;

static lv_display_t *disp;
static uint16_t *buf0;
static uint16_t *buf1;
static i2c_master_bus_handle_t i2c_handle = NULL;

// Copy the rendered area into the RGB panel framebuffer (synchronous)
static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *color_map) {
    indicator::lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_display_flush_ready(d);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x, y;
    if (indicator::touch_read(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        touch_active = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        touch_active = false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("ADS-B Display (SenseCAP Indicator) starting...");
    Serial.printf("Heap free: %lu  PSRAM free: %lu\n",
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // I2C bus shared by the IO expander and the touch controller
    i2c_master_bus_config_t i2c_bus_conf = {};
    i2c_bus_conf.i2c_port = I2C_NUM_0;
    i2c_bus_conf.sda_io_num = (gpio_num_t)I2C_SDA;
    i2c_bus_conf.scl_io_num = (gpio_num_t)I2C_SCL;
    i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_conf.glitch_ignore_cnt = 7;
    i2c_bus_conf.flags.enable_internal_pullup = 1;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle));

    if (!indicator::expander_init(i2c_handle)) {
        Serial.println("ERROR: IO expander missing, display cannot be initialized");
    }

    indicator::lcd_init();
    Serial.println("LCD initialized");

    if (indicator::touch_init(i2c_handle)) {
        Serial.println("Touch initialized");
    } else {
        Serial.println("WARNING: touch init failed, continuing without touch");
    }

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    // Render buffers: 40 lines each, double-buffered, PARTIAL mode
    uint32_t buf_size = LCD_H_RES * 40;
    buf0 = (uint16_t *)heap_caps_malloc(buf_size * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    buf1 = (uint16_t *)heap_caps_malloc(buf_size * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(buf0 && buf1);

    disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf0, buf1, buf_size * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);
    lv_indev_set_scroll_limit(indev, 10);
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);

    aircraft_list.init();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Draw the background before the backlight comes on (avoids a flash of garbage)
    lv_refr_now(disp);
    indicator::backlight_on();

    Serial.println("Creating UI...");
    status_bar_create(screen);
    views_init(screen, &aircraft_list);
    detail_card_init(screen);
    alerts_init(screen);
    settings_init(screen);

    g_config = storage_load_config();

    status_bar_set_gear_callback([](lv_event_t *e) {
        settings_show();
    });

    settings_set_change_callback([](const UserConfig *cfg) {
        g_config = *cfg;
    });

    lv_timer_create([](lv_timer_t *timer) {
        status_bar_update(fetcher_wifi_connected(), aircraft_list.count, fetcher_last_update());
    }, 1000, nullptr);

    Serial.println("LVGL initialized - UI ready");
    Serial.printf("Heap free: %lu  PSRAM free: %lu\n",
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    error_log_init();
    enrichment_init();
    fetcher_init(&aircraft_list);
}

void loop() {
    lv_timer_handler();

    static uint32_t last_heap_log = 0;
    uint32_t now = millis();
    if (now - last_heap_log >= 10000) {
        last_heap_log = now;
        Serial.printf("HEAP int=%lu min=%lu  PSRAM=%lu\n",
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    vTaskDelay(pdMS_TO_TICKS(1));
}
