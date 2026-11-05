#pragma once

// Seeed SenseCAP Indicator (D1 / D1S / D1Pro / D1L)
// ESP32-S3 (8MB flash, 8MB octal PSRAM), 4" 480x480 ST7701S RGB panel, FT6336 touch.
// Pinout from Seeed's indicator-esp-idf-template (sensecap_indicator_board.c).
//
// LCD_H_RES / LCD_V_RES come from the build flags (shared with ui_s3 via pins_s3.h).

// RGB LCD control pins
#define LCD_HSYNC  16
#define LCD_VSYNC  17
#define LCD_DE     18
#define LCD_PCLK   21
#define LCD_BL     45   // backlight, active high

// RGB pixel clock. Higher = faster refresh but more PSRAM bandwidth; too high
// causes underruns (image jumps by a few pixels with garbage at top/bottom).
#ifndef INDICATOR_LCD_PCLK_HZ
#define INDICATOR_LCD_PCLK_HZ (8 * 1000 * 1000)
#endif

// RGB565 data bus, ordered B[4:0] G[5:0] R[4:0] for esp_lcd_rgb_panel
#define LCD_D0   15   // B0
#define LCD_D1   14   // B1
#define LCD_D2   13   // B2
#define LCD_D3   12   // B3
#define LCD_D4   11   // B4
#define LCD_D5   10   // G0
#define LCD_D6   9    // G1
#define LCD_D7   8    // G2
#define LCD_D8   7    // G3
#define LCD_D9   6    // G4
#define LCD_D10  5    // G5
#define LCD_D11  4    // R0
#define LCD_D12  3    // R1
#define LCD_D13  2    // R2
#define LCD_D14  1    // R3
#define LCD_D15  0    // R4

// ST7701S 3-wire SPI (9-bit) for the init sequence: SCK/MOSI are GPIOs,
// CS and RESET are on the IO expander
#define LCD_SPI_SCK   41
#define LCD_SPI_MOSI  48

// I2C bus (IO expander + touch)
#define I2C_SDA  39
#define I2C_SCL  40

// TCA9535 IO expander (some units answer on 0x39 instead)
#define IO_EXP_ADDR      0x20
#define IO_EXP_ADDR_ALT  0x39
#define EXIO_LCD_CS      4
#define EXIO_LCD_RST     5
#define EXIO_TP_RST      7
#define EXIO_RP2040_RST  8

// FT6336U touch controller
#define TP_ADDR  0x48

// User button (top right, active low)
#define BTN_USER 38
