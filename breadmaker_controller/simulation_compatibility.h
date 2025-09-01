#pragma once

// Conditional compilation for simulation vs real hardware
#ifdef NATIVE_SIMULATION
    #include "simulation/include/arduino_simulation.h"
    
    // Mock ESP32-specific libraries for simulation
    #define CONTENT_LENGTH_UNKNOWN SIZE_MAX
    
    // TFT Display simulation
    class TFT_eSPI {
    public:
        void init() { Serial.println("[SIM] TFT Display initialized"); }
        void setRotation(int r) { Serial.printf("[SIM] TFT rotation set to %d\n", r); }
        void fillScreen(uint16_t color) { Serial.printf("[SIM] TFT filled with color %d\n", color); }
        void setTextColor(uint16_t color) {}
        void setCursor(int x, int y) {}
        void print(const char* text) { Serial.printf("[SIM] TFT: %s\n", text); }
        void println(const char* text) { Serial.printf("[SIM] TFT: %s\n", text); }
        int width() { return 135; }
        int height() { return 240; }
    };
    
    // NTP Client simulation
    class NTPClient {
    public:
        NTPClient(void* udp, const char* server, int offset) {}
        void begin() { Serial.println("[SIM] NTP Client started"); }
        void update() {}
        unsigned long getEpochTime() { return time(nullptr); }
        String getFormattedTime() { return String("12:00:00"); }
    };
    
    // WiFi UDP simulation
    class WiFiUDP {
    public:
        void begin(int port) {}
    };
    
    // SPIFFS simulation
    class SPIFFSClass {
    public:
        bool begin() { return true; }
        File open(const char* path, const char* mode) { return FFat.open(path, mode); }
        bool exists(const char* path) { return FFat.exists(path); }
    };
    extern SPIFFSClass SPIFFS;
    
    // ESP32Servo simulation
    class Servo {
    public:
        void attach(int pin) { 
            Serial.printf("[SIM] Servo attached to pin %d\n", pin); 
        }
        void write(int angle) { 
            Serial.printf("[SIM] Servo angle set to %d\n", angle); 
        }
        void detach() {
            Serial.println("[SIM] Servo detached");
        }
    };
    
    // ArduinoJson already works in native mode
    
#else
    // Real hardware includes
    #include <WiFi.h>
    #include <WebServer.h>
    #include <FFat.h>
    #include <TFT_eSPI.h>
    #include <NTPClient.h>
    #include <WiFiUdp.h>
    #include <ESP32Servo.h>
    #include <SPIFFS.h>
    #include <ArduinoJson.h>
#endif

// Common macros that work in both environments
#ifndef F
#define F(string_literal) (string_literal)
#endif
