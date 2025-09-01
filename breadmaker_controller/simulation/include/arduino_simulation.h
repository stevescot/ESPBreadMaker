#pragma once

#ifdef NATIVE_SIMULATION

#include <cstdint>
#include <string>
#include <map>
#include <functional>
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>

// ===== Arduino Core Simulation =====
#define HIGH 1
#define LOW 0
#define INPUT 1
#define OUTPUT 2
#define INPUT_PULLUP 3

// ESP32 specific pins
#define A0 36

// Simulated millis() function
extern unsigned long simulated_millis;
extern double time_acceleration_factor;

inline unsigned long millis() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    auto millis_real = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return (unsigned long)(millis_real * time_acceleration_factor);
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds((long)(ms / time_acceleration_factor)));
}

// ===== WiFi Simulation =====
class WiFiClass {
public:
    void begin(const char* ssid, const char* password) {
        connected = true;
        std::cout << "[SIM] WiFi connected to " << ssid << std::endl;
    }
    
    bool isConnected() { return connected; }
    
    std::string localIP() { return "192.168.1.100"; }
    
    int status() { return connected ? 3 : 6; } // WL_CONNECTED : WL_DISCONNECTED
    
private:
    bool connected = false;
};

extern WiFiClass WiFi;

// ===== Web Server Simulation =====
enum HTTPMethod { HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE };

class WebServer {
public:
    WebServer(int port) : port_(port) {}
    
    void begin() {
        std::cout << "[SIM] Web server started on port " << port_ << std::endl;
    }
    
    void handleClient() {
        // Simulate periodic client requests for testing
        static auto last_request = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_request).count() > 10) {
            // Simulate status request every 10 seconds
            simulateRequest("/api/status", HTTP_GET);
            last_request = now;
        }
    }
    
    void on(const char* path, HTTPMethod method, std::function<void()> handler) {
        routes_[std::string(path)] = handler;
        std::cout << "[SIM] Route registered: " << path << std::endl;
    }
    
    void send(int code, const char* content_type, const char* content) {
        std::cout << "[SIM] HTTP " << code << " " << content_type << ": " << content << std::endl;
    }
    
    std::string arg(const char* name) {
        return request_args_[std::string(name)];
    }
    
    void setContentLength(size_t len) {}
    void sendContent(const char* content) {}
    
private:
    int port_;
    std::map<std::string, std::function<void()>> routes_;
    std::map<std::string, std::string> request_args_;
    
    void simulateRequest(const char* path, HTTPMethod method) {
        auto it = routes_.find(std::string(path));
        if (it != routes_.end()) {
            std::cout << "[SIM] Simulating request: " << path << std::endl;
            it->second();
        }
    }
};

// ===== FFat File System Simulation =====
class File {
public:
    File() : valid_(false) {}
    File(const std::string& content) : content_(content), valid_(true), pos_(0) {}
    
    operator bool() const { return valid_; }
    
    size_t write(const char* data, size_t len) {
        content_.append(data, len);
        return len;
    }
    
    size_t write(const char* str) {
        content_ += str;
        return strlen(str);
    }
    
    int read() {
        if (pos_ >= content_.size()) return -1;
        return content_[pos_++];
    }
    
    size_t readBytes(char* buffer, size_t length) {
        size_t available = content_.size() - pos_;
        size_t to_read = std::min(length, available);
        memcpy(buffer, content_.data() + pos_, to_read);
        pos_ += to_read;
        return to_read;
    }
    
    void close() { valid_ = false; }
    size_t size() { return content_.size(); }
    
private:
    std::string content_;
    bool valid_;
    size_t pos_;
};

class FFatClass {
public:
    bool begin(bool format_if_failed = false) {
        std::cout << "[SIM] FFat file system initialized" << std::endl;
        return true;
    }
    
    File open(const char* path, const char* mode = "r") {
        std::cout << "[SIM] Opening file: " << path << " mode: " << mode << std::endl;
        
        // Return mock file content based on path
        if (std::string(path).find("programs") != std::string::npos) {
            return File(R"([{"name":"Test Program","customStages":[{"label":"Mix","min":2,"temp":25}]}])");
        }
        if (std::string(path).find("resume") != std::string::npos) {
            return File(R"({"running":false,"stage":0})");
        }
        
        return File("{}"); // Empty JSON for other files
    }
    
    bool exists(const char* path) {
        std::cout << "[SIM] Checking if file exists: " << path << std::endl;
        return true; // Assume files exist for simulation
    }
    
    bool remove(const char* path) {
        std::cout << "[SIM] Removing file: " << path << std::endl;
        return true;
    }
    
    bool mkdir(const char* path) {
        std::cout << "[SIM] Creating directory: " << path << std::endl;
        return true;
    }
    
    bool rmdir(const char* path) {
        std::cout << "[SIM] Removing directory: " << path << std::endl;
        return true;
    }
};

extern FFatClass FFat;

// ===== Hardware Pin Simulation =====
extern std::map<int, int> pin_values;
extern std::map<int, int> pin_modes;

inline void pinMode(int pin, int mode) {
    pin_modes[pin] = mode;
    std::cout << "[SIM] pinMode(" << pin << ", " << mode << ")" << std::endl;
}

inline void digitalWrite(int pin, int value) {
    pin_values[pin] = value;
    std::cout << "[SIM] digitalWrite(" << pin << ", " << value << ")" << std::endl;
}

inline int digitalRead(int pin) {
    return pin_values[pin];
}

// ===== Temperature Sensor Simulation =====
class SimulatedTemperatureSensor {
public:
    static double getTemperature() {
        // Simulate realistic temperature behavior
        static double target_temp = 20.0;
        static double current_temp = 20.0;
        static auto last_update = std::chrono::steady_clock::now();
        
        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration<double>(now - last_update).count() * time_acceleration_factor;
        last_update = now;
        
        // Simple thermal model
        double heater_power = pin_values[4] ? 1.0 : 0.0; // Assuming pin 4 is heater
        double heating_rate = heater_power * 50.0; // 50°C/hour max heating
        double cooling_rate = (current_temp - 20.0) * 2.0; // Cooling to ambient
        
        current_temp += (heating_rate - cooling_rate) * dt / 3600.0;
        current_temp = std::max(15.0, std::min(250.0, current_temp));
        
        // Add some noise
        static int noise_counter = 0;
        double noise = (noise_counter++ % 10 - 5) * 0.1;
        
        return current_temp + noise;
    }
    
    static void setRoomTemperature(double temp) {
        room_temperature = temp;
    }
    
    static void setTargetTemperature(double temp) {
        target_temperature = temp;
    }
    
private:
    static double room_temperature;
    static double target_temperature;
};

// ===== Analog Read Simulation =====
inline int analogRead(int pin) {
    if (pin == A0) {
        // Simulate temperature sensor reading
        double temp = SimulatedTemperatureSensor::getTemperature();
        // Convert to ADC value (assuming thermistor)
        return (int)((temp - 15.0) / 235.0 * 4095.0);
    }
    return 0;
}

// ===== Serial Simulation =====
class SerialClass {
public:
    void begin(unsigned long baud) {
        std::cout << "[SIM] Serial initialized at " << baud << " baud" << std::endl;
    }
    
    void print(const char* str) {
        std::cout << str;
    }
    
    void println(const char* str) {
        std::cout << str << std::endl;
    }
    
    void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
    
    bool available() { return false; }
    int read() { return -1; }
};

extern SerialClass Serial;

// ===== Time Functions =====
inline time_t time(time_t* timer) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    if (timer) *timer = time_t_now;
    return time_t_now;
}

// ===== String class simulation =====
class String {
public:
    String() {}
    String(const char* str) : data_(str ? str : "") {}
    String(int value) : data_(std::to_string(value)) {}
    String(float value) : data_(std::to_string(value)) {}
    String(double value) : data_(std::to_string(value)) {}
    
    const char* c_str() const { return data_.c_str(); }
    size_t length() const { return data_.length(); }
    
    String operator+(const String& other) const {
        return String((data_ + other.data_).c_str());
    }
    
    String& operator+=(const String& other) {
        data_ += other.data_;
        return *this;
    }
    
    bool operator==(const String& other) const {
        return data_ == other.data_;
    }
    
    int toInt() const {
        return std::stoi(data_);
    }
    
    float toFloat() const {
        return std::stof(data_);
    }
    
private:
    std::string data_;
};

// ===== Simulation Control Functions =====
namespace Simulation {
    void setTimeAcceleration(double factor);
    void setRoomTemperature(double temp);
    void setTargetTemperature(double temp);
    void logState();
    void runTestSequence();
}

#endif // NATIVE_SIMULATION
