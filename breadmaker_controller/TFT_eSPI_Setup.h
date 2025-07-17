// TFT_eSPI User Setup for TTGO T-Display 1.14"
// Configure this file for the TTGO T-Display 1.14" ESP32 board
// This should be placed in the TFT_eSPI library folder or configured through platformio.ini

// ESP32 with TFT_eSPI library configuration for TTGO T-Display 1.14"
// Board: ESP32 Dev Module or TTGO T-Display
// Display: ST7789 135x240 TFT (1.14 inch)

// Generic ESP32 setup
// Note: ESP32_PARALLEL is NOT needed for SPI displays like TTGO T-Display
// #define ESP32_PARALLEL  // Only for parallel 8-bit displays

// For TTGO T-Display 1.14" ESP32
#define ST7789_DRIVER    // Configure all registers
#define TFT_WIDTH  135
#define TFT_HEIGHT 240

// TTGO T-Display 1.14" pin connections
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS   5  // Chip select control pin
#define TFT_DC   16  // Data Command control pin
#define TFT_RST  23  // Reset pin (could connect to RST pin)
#define TFT_BL   4   // LED back-light

// For ST7789 driver only
#define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
//#define TFT_RGB_ORDER TFT_BGR  // Colour order Blue-Green-Red

// Fonts to be available
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel high font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel high font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel high font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

// Comment out the #define below to stop the SPIFFS filing system and smooth font code being loaded
// this will save ~20kbytes of FLASH
#define SMOOTH_FONT

// SPI frequency for TFT (can be adjusted for stability)
#define SPI_FREQUENCY  40000000  // 40MHz (may need to reduce if display issues occur)

// SPI read frequency
#define SPI_READ_FREQUENCY  16000000  // 16MHz

// TFT SPI port (1 or 2)
#define TFT_SPI_PORT 1
