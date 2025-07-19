#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <LovyanGFX.hpp>
#include <Arduino.h>

// Custom display class for TTGO T-Display
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST; // Use VSPI_HOST for ESP32
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 18; // TTGO T-Display SCLK
      cfg.pin_mosi = 19; // TTGO T-Display MOSI
      cfg.pin_miso = -1; // Not used
      cfg.pin_dc = 16;   // TTGO T-Display DC
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 5;    // TTGO T-Display CS
      cfg.pin_rst = 23;  // TTGO T-Display RST
      cfg.pin_busy = -1; // Not used
      cfg.panel_width = 135;
      cfg.panel_height = 240;
      cfg.offset_x = 52;  // TTGO T-Display offset
      cfg.offset_y = 40;  // TTGO T-Display offset
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 4;  // TTGO T-Display backlight
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

// Display dimensions for TTGO T-Display
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 135

// Color definitions (16-bit RGB565 format)
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x7BEF
#define COLOR_DARKGRAY  0x4208

// Display states
enum DisplayState {
  DISPLAY_STATUS,
  DISPLAY_MENU,
  DISPLAY_PROGRAMS,
  DISPLAY_SETTINGS,
  DISPLAY_WIFI_SETUP
};

// Function declarations
void displayManagerInit();
void updateDisplay();
void displayStatus();
void displayMenu();
void displayPrograms();
void displaySettings();
void displayWiFiSetup();
void displayError(const String& message);
void displayBootScreen();
void handleButtons();

// Display state management
void setDisplayState(DisplayState state);
DisplayState getDisplayState();

// Display utilities
void drawProgressBar(int x, int y, int width, int height, float progress);
void drawTemperature(int x, int y, float temp);
void drawStageInfo(int x, int y, const String& stage, unsigned long timeLeft);
void drawOutputStates(int x, int y, bool heater, bool motor, bool light, bool buzzer);

extern LGFX display;

#endif // DISPLAY_MANAGER_H
