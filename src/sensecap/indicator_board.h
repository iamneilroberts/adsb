#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"

// SenseCAP Indicator board support: IO expander, ST7701S panel, RGB bus, FT6336 touch.
namespace indicator {

// Set up the TCA9535 expander on an existing I2C bus. Returns false if not found.
bool expander_init(i2c_master_bus_handle_t bus);
void expander_set(uint8_t pin, bool level);

// Reset the ST7701S, send its init sequence over 3-wire SPI and start the RGB panel.
// Backlight stays off; call backlight_on() once something has been drawn.
esp_lcd_panel_handle_t lcd_init();
void backlight_on();
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *data);

// Reset and probe the FT6336 touch controller.
bool touch_init(i2c_master_bus_handle_t bus);
bool touch_read(uint16_t *x, uint16_t *y);

}  // namespace indicator
