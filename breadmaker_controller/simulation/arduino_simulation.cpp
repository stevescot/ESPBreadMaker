#ifdef NATIVE_SIMULATION

#include "arduino_simulation.h"
#include <cstdarg>
#include <cstring>

// Global simulation variables
unsigned long simulated_millis = 0;
double time_acceleration_factor = 60.0; // Default 60x speed

std::map<int, int> pin_values;
std::map<int, int> pin_modes;

// Global instances
WiFiClass WiFi;
FFatClass FFat;
SerialClass Serial;

// Temperature sensor statics
double SimulatedTemperatureSensor::room_temperature = 20.0;
double SimulatedTemperatureSensor::target_temperature = 20.0;

// Simulation control functions
namespace Simulation {
    void setTimeAcceleration(double factor) {
        time_acceleration_factor = factor;
        std::cout << "[SIM] Time acceleration set to " << factor << "x" << std::endl;
    }
    
    void setRoomTemperature(double temp) {
        SimulatedTemperatureSensor::setRoomTemperature(temp);
        std::cout << "[SIM] Room temperature set to " << temp << "°C" << std::endl;
    }
    
    void setTargetTemperature(double temp) {
        SimulatedTemperatureSensor::setTargetTemperature(temp);
        std::cout << "[SIM] Target temperature set to " << temp << "°C" << std::endl;
    }
    
    void logState() {
        double temp = SimulatedTemperatureSensor::getTemperature();
        bool heater = pin_values[4] > 0;
        bool motor = pin_values[2] > 0;
        
        std::cout << "[SIM STATE] Time: " << millis() << "ms, "
                  << "Temp: " << temp << "°C, "
                  << "Heater: " << (heater ? "ON" : "OFF") << ", "
                  << "Motor: " << (motor ? "ON" : "OFF") << std::endl;
    }
    
    void runTestSequence() {
        std::cout << "[SIM] Starting automated test sequence..." << std::endl;
        
        // Test 1: Temperature control
        std::cout << "[SIM] Test 1: Temperature control" << std::endl;
        setRoomTemperature(20.0);
        setTargetTemperature(30.0);
        
        // Let it run for simulated 10 minutes
        auto start_time = millis();
        while ((millis() - start_time) < 600000) { // 10 minutes in ms
            delay(1000); // 1 second delays
            logState();
        }
        
        // Test 2: Fermentation calculations
        std::cout << "[SIM] Test 2: Fermentation calculations" << std::endl;
        // This would trigger fermentation logic in the main code
        
        // Test 3: PID control stability
        std::cout << "[SIM] Test 3: PID control stability" << std::endl;
        setTargetTemperature(25.0);
        
        std::cout << "[SIM] Test sequence completed" << std::endl;
    }
}

// Arduino setup() and loop() function declarations
extern void setup();
extern void loop();

// Main function for native simulation
int main() {
    std::cout << "===== ESP32 Breadmaker Simulator Starting =====" << std::endl;
    std::cout << "Time acceleration: " << time_acceleration_factor << "x" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Simulation runs automatically" << std::endl;
    std::cout << "  - Press Ctrl+C to stop" << std::endl;
    std::cout << "===============================================" << std::endl;
    
    // Initialize simulation
    Serial.begin(115200);
    
    // Call Arduino setup
    setup();
    
    // Run the test sequence in a separate thread
    std::thread test_thread([]() {
        delay(5000); // Wait 5 seconds
        Simulation::runTestSequence();
    });
    
    // Main loop
    try {
        while (true) {
            loop();
            delay(100); // 100ms delay between loop iterations
        }
    } catch (const std::exception& e) {
        std::cout << "[SIM] Exception: " << e.what() << std::endl;
    }
    
    if (test_thread.joinable()) {
        test_thread.join();
    }
    
    std::cout << "[SIM] Simulation ended" << std::endl;
    return 0;
}

#endif // NATIVE_SIMULATION
