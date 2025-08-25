// Minimal LovyanGFX configuration to reduce flash usage
// Only include what's needed for TTGO T-Display

#pragma once

// Disable all unused panels
#define LGFX_USE_V1
#define LGFX_AUTODETECT     // Only for TTGO T-Display auto-detection

// Disable font files we don't need
#define LGFX_NO_FONT_CN     // No Chinese fonts
#define LGFX_NO_FONT_JA     // No Japanese fonts  
#define LGFX_NO_FONT_KR     // No Korean fonts
#define LGFX_NO_FONT_TW     // No Traditional Chinese fonts

// Disable platforms we don't use
#define LGFX_NO_ESP8266
#define LGFX_NO_RP2040
#define LGFX_NO_SAMD
#define LGFX_NO_STM32
#define LGFX_NO_OPENCV
#define LGFX_NO_FRAMEBUFFER
#define LGFX_NO_SDL

// Disable touch controllers we don't use
#define LGFX_NO_TOUCH_CST816S
#define LGFX_NO_TOUCH_FT5X06  
#define LGFX_NO_TOUCH_GT911
#define LGFX_NO_TOUCH_STMPE610
#define LGFX_NO_TOUCH_XPT2046

// Only keep essential panels (ST7789 for TTGO T-Display)
// All others will be excluded automatically
