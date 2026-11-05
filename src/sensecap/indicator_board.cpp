#include "indicator_board.h"
#include "pins.h"
#include "pins_s3.h"   // LCD_H_RES / LCD_V_RES
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "indicator";

namespace indicator {

// ---------------------------------------------------------------------------
// TCA9535 IO expander (16 bit, registers are port0/port1 pairs)
// ---------------------------------------------------------------------------
static i2c_master_dev_handle_t _exp = nullptr;
static uint16_t _exp_out = 0xFFFF;
static uint16_t _exp_cfg = 0xFFFF;  // 1 = input

#define TCA_OUTPUT_REG 0x02
#define TCA_CONFIG_REG 0x06

static void exp_write16(uint8_t reg, uint16_t v) {
    uint8_t buf[3] = { reg, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    i2c_master_transmit(_exp, buf, sizeof(buf), 100);
}

static bool exp_read16(uint8_t reg, uint16_t *v) {
    uint8_t buf[2];
    if (i2c_master_transmit_receive(_exp, &reg, 1, buf, 2, 100) != ESP_OK) return false;
    *v = buf[0] | (buf[1] << 8);
    return true;
}

bool expander_init(i2c_master_bus_handle_t bus) {
    uint8_t addr = IO_EXP_ADDR;
    if (i2c_master_probe(bus, addr, 100) != ESP_OK) {
        addr = IO_EXP_ADDR_ALT;
        if (i2c_master_probe(bus, addr, 100) != ESP_OK) {
            ESP_LOGE(TAG, "IO expander not found at 0x%02X or 0x%02X", IO_EXP_ADDR, IO_EXP_ADDR_ALT);
            return false;
        }
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &cfg, &_exp) != ESP_OK) return false;

    // Start from the chip's current state so we don't glitch pins we don't own
    exp_read16(TCA_OUTPUT_REG, &_exp_out);
    exp_read16(TCA_CONFIG_REG, &_exp_cfg);

    // Keep the RP2040 co-processor running (RUN pin high)
    expander_set(EXIO_RP2040_RST, true);
    ESP_LOGI(TAG, "IO expander at 0x%02X", addr);
    return true;
}

// Drive a pin as output at the given level
void expander_set(uint8_t pin, bool level) {
    if (!_exp) return;
    if (level) _exp_out |= (1u << pin);
    else       _exp_out &= ~(1u << pin);
    exp_write16(TCA_OUTPUT_REG, _exp_out);
    if (_exp_cfg & (1u << pin)) {
        _exp_cfg &= ~(1u << pin);
        exp_write16(TCA_CONFIG_REG, _exp_cfg);
    }
}

// ---------------------------------------------------------------------------
// ST7701S init over 3-wire 9-bit SPI (bit-banged; CS on the expander)
// ---------------------------------------------------------------------------
static inline void spi_bit(bool b) {
    gpio_set_level((gpio_num_t)LCD_SPI_MOSI, b);
    esp_rom_delay_us(1);
    gpio_set_level((gpio_num_t)LCD_SPI_SCK, 1);
    esp_rom_delay_us(1);
    gpio_set_level((gpio_num_t)LCD_SPI_SCK, 0);
}

static void spi_9bit(bool is_data, uint8_t v) {
    spi_bit(is_data);
    for (int i = 7; i >= 0; i--) spi_bit((v >> i) & 1);
}

// One command plus its parameters in a single CS frame
static void lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
    expander_set(EXIO_LCD_CS, false);
    spi_9bit(false, cmd);
    for (size_t i = 0; i < len; i++) spi_9bit(true, data[i]);
    expander_set(EXIO_LCD_CS, true);
}

struct InitCmd {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
    uint16_t delay_ms;
};

// Seeed "GX" panel init sequence (lcd_panel_st7701s_init)
static const InitCmd st7701s_init[] = {
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x10}, 0},
    {0xC0, 2,  {0x3B, 0x00}, 0},              // 480 lines
    {0xC1, 2,  {0x0D, 0x02}, 0},
    {0xC2, 2,  {0x31, 0x05}, 0},
    {0xC7, 1,  {0x04}, 0},
    {0xCD, 1,  {0x08}, 0},
    {0xB0, 16, {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
                0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 0},
    {0xB1, 16, {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
                0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 0},
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x11}, 0},
    {0xB0, 1,  {0x60}, 0},
    {0xB1, 1,  {0x32}, 0},
    {0xB2, 1,  {0x07}, 0},
    {0xB3, 1,  {0x80}, 0},
    {0xB5, 1,  {0x49}, 0},
    {0xB7, 1,  {0x85}, 0},
    {0xB8, 1,  {0x21}, 0},
    {0xC1, 1,  {0x78}, 0},
    {0xC2, 1,  {0x78}, 20},
    {0xE0, 3,  {0x00, 0x1B, 0x02}, 0},
    {0xE1, 11, {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 0},
    {0xE2, 12, {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 0},
    {0xE3, 4,  {0x00, 0x00, 0x11, 0x11}, 0},
    {0xE4, 2,  {0x44, 0x44}, 0},
    {0xE5, 16, {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
                0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 0},
    {0xE6, 4,  {0x00, 0x00, 0x11, 0x11}, 0},
    {0xE7, 2,  {0x44, 0x44}, 0},
    {0xE8, 16, {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
                0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 0},
    {0xEB, 7,  {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 0},
    {0xEC, 2,  {0x3C, 0x00}, 0},
    {0xED, 16, {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 0},
    {0x36, 1,  {0x10}, 0},
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x13}, 0},
    {0xE5, 1,  {0xE4}, 0},
    {0xFF, 5,  {0x77, 0x01, 0x00, 0x00, 0x00}, 0},
    {0x3A, 1,  {0x60}, 0},                    // RGB666 (bus wired as RGB565)
    {0x21, 0,  {}, 0},                        // display inversion on
    {0x11, 0,  {}, 120},                      // sleep out
    {0x29, 0,  {}, 120},                      // display on
};

static void st7701s_send_init() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << LCD_SPI_SCK) | (1ULL << LCD_SPI_MOSI);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)LCD_SPI_SCK, 0);
    gpio_set_level((gpio_num_t)LCD_SPI_MOSI, 0);

    // Hardware reset
    expander_set(EXIO_LCD_CS, true);
    expander_set(EXIO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    expander_set(EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (const InitCmd &c : st7701s_init) {
        lcd_cmd(c.cmd, c.data, c.len);
        if (c.delay_ms) vTaskDelay(pdMS_TO_TICKS(c.delay_ms));
    }
}

// ---------------------------------------------------------------------------
// RGB panel
// ---------------------------------------------------------------------------
static esp_lcd_panel_handle_t _panel = nullptr;

esp_lcd_panel_handle_t lcd_init() {
    gpio_config_t bl = {};
    bl.pin_bit_mask = 1ULL << LCD_BL;
    bl.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl);
    gpio_set_level((gpio_num_t)LCD_BL, 0);

    st7701s_send_init();

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    // Bounce buffers (internal RAM) are refilled from the PSRAM framebuffer by the CPU.
    // 2 x 10 lines = 19 KB internal RAM; 20 lines starved TLS (mbedTLS alloc failures).
    cfg.bounce_buffer_size_px = LCD_H_RES * 10;
    cfg.flags.fb_in_psram = 1;

    // Porches from Seeed/Meshtastic: HS 8/50/10, VS 8/20/10.
    // Seeed's 18 MHz pclk starves PSRAM bandwidth: underruns shift the image until
    // the next vsync. Meshtastic runs 6 MHz; 12 MHz (~42 Hz refresh) is a middle ground.
    cfg.timings.h_res = LCD_H_RES;
    cfg.timings.v_res = LCD_V_RES;
    cfg.timings.pclk_hz = INDICATOR_LCD_PCLK_HZ;
    cfg.timings.hsync_pulse_width = 8;
    cfg.timings.hsync_back_porch = 50;
    cfg.timings.hsync_front_porch = 10;
    cfg.timings.vsync_pulse_width = 8;
    cfg.timings.vsync_back_porch = 20;
    cfg.timings.vsync_front_porch = 10;
    cfg.timings.flags.pclk_active_neg = 0;

    cfg.hsync_gpio_num = LCD_HSYNC;
    cfg.vsync_gpio_num = LCD_VSYNC;
    cfg.de_gpio_num = LCD_DE;
    cfg.pclk_gpio_num = LCD_PCLK;
    cfg.disp_gpio_num = -1;
    const int data_pins[16] = {
        LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_D4, LCD_D5, LCD_D6, LCD_D7,
        LCD_D8, LCD_D9, LCD_D10, LCD_D11, LCD_D12, LCD_D13, LCD_D14, LCD_D15,
    };
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data_pins[i];

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(_panel));
    ESP_LOGI(TAG, "RGB LCD %dx%d initialized", LCD_H_RES, LCD_V_RES);
    return _panel;
}

void backlight_on() {
    gpio_set_level((gpio_num_t)LCD_BL, 1);
}

void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *data) {
    esp_lcd_panel_draw_bitmap(_panel, x_start, y_start, x_end, y_end, data);
}

// ---------------------------------------------------------------------------
// FT6336U touch
// ---------------------------------------------------------------------------
static i2c_master_dev_handle_t _tp = nullptr;

bool touch_init(i2c_master_bus_handle_t bus) {
    expander_set(EXIO_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    expander_set(EXIO_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(300));  // FT6336 needs ~300 ms after reset

    if (i2c_master_probe(bus, TP_ADDR, 100) != ESP_OK) {
        ESP_LOGE(TAG, "touch controller not found at 0x%02X", TP_ADDR);
        return false;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = TP_ADDR;
    cfg.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(bus, &cfg, &_tp) == ESP_OK;
}

bool touch_read(uint16_t *x, uint16_t *y) {
    if (!_tp) return false;
    uint8_t reg = 0x02;  // TD_STATUS, followed by P1_XH, P1_XL, P1_YH, P1_YL
    uint8_t buf[5];
    if (i2c_master_transmit_receive(_tp, &reg, 1, buf, sizeof(buf), 20) != ESP_OK) return false;
    uint8_t points = buf[0] & 0x0F;
    if (points == 0 || points > 2) return false;
    uint16_t tx = ((buf[1] & 0x0F) << 8) | buf[2];
    uint16_t ty = ((buf[3] & 0x0F) << 8) | buf[4];
    if (tx >= LCD_H_RES) tx = LCD_H_RES - 1;
    if (ty >= LCD_V_RES) ty = LCD_V_RES - 1;
    // Touch panel is mounted rotated 180° relative to the display
    *x = LCD_H_RES - 1 - tx;
    *y = LCD_V_RES - 1 - ty;
    return true;
}

}  // namespace indicator
