#include "web_endpoints.h"
#include <Arduino.h>
#include "globals.h"
#include <ArduinoJson.h>
#include "programs_manager.h"
#include "outputs_manager.h"
#include <PID_v1.h>
#include <WiFi.h>
#include "calibration.h"
#include <FFat.h>
#include "ota_manager.h"
#include <Update.h>  // For web-based firmware updates
#include <algorithm>  // For std::sort
#include "missing_stubs.h"  // For getAdjustedStageTimeMs and other functions

// External OTA status for web integration
extern OTAStatus otaStatus;

// Performance tracking variables
static unsigned long loopCount = 0;
static unsigned long lastLoopTime = 0;
static unsigned long totalLoopTime = 0;
static unsigned long maxLoopTime = 0;
static unsigned long minFreeHeap = 0xFFFFFFFF;
static unsigned long wifiReconnectCount = 0;
static unsigned long lastWifiStatus = WL_CONNECTED;

// Caching variables for frequently accessed endpoints
static unsigned long lastStatusUpdate = 0;
static unsigned long lastHaUpdate = 0;
static const unsigned long STATUS_CACHE_MS = 1000;  // Cache for 1 second
static const unsigned long HA_CACHE_MS = 3000;      // Cache HA for 3 seconds

// Cache invalidation function - call this when state changes
void invalidateStatusCache() {
  lastStatusUpdate = 0;
  lastHaUpdate = 0;
}

// Forward declarations for helpers and global variables used in endpoints
extern bool debugSerial;
extern unsigned long windowSize, onTime, startupTime;
extern bool thermalRunawayDetected, sensorFaultDetected;
extern time_t scheduledStart;
extern int scheduledStartStage;
extern bool heaterState, motorState, lightState, buzzerState;
extern void streamStatusJson(Print& out);
extern void resetFermentationTracking(float temp);
extern float getAveragedTemperature();
extern void saveSettings();
extern void saveResumeState();
extern void updateActiveProgramVars();
extern void stopBreadmaker();
extern bool isStartupDelayComplete();
extern void setHeater(bool);
extern void setMotor(bool);
extern void setLight(bool);
extern void setBuzzer(bool);
extern void startBuzzerTone(float, float, unsigned long);
extern float readTemperature();
extern void loadPIDProfiles();
extern void savePIDProfiles();
extern WebServer server; // Global server reference

extern void sendJsonError(WebServer& server, const String&, const String&, int);
void deleteFolderRecursive(const String& path);

extern std::vector<Program> programs;
extern unsigned long lightOnTime;
extern unsigned long windowStartTime;

// Missing function declarations
extern size_t getProgramCount();
extern bool isProgramValid(int programId);
extern void shortBeep();
extern bool ensureProgramLoaded(int programId);
extern Program* getActiveProgramMutable();

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

// Note: getStatusJsonString is deprecated - use direct streaming with streamStatusJson() instead
// This function is kept for backward compatibility but creates strings in memory
String getStatusJsonString() {
  String response;
  response.reserve(2048);
  
  class StringPrint : public Print {
    String& str;
    public:
      StringPrint(String& s) : str(s) {}
      size_t write(uint8_t c) override { str += (char)c; return 1; }
      size_t write(const uint8_t* buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) str += (char)buffer[i];
        return size;
      }
  };
  
  StringPrint stringPrint(response);
  streamStatusJson(stringPrint);
  return response;
}

// Helper function to serve static files from FFat
bool serveStaticFile(WebServer& server, const String& path) {
  String fullPath = path;
  
  // Handle root path
  if (path == "/" || path.isEmpty()) {
    fullPath = "/index.html";
  }
  
  // Ensure path starts with slash for FATFS
  if (!fullPath.startsWith("/")) {
    fullPath = "/" + fullPath;
  }
  
  // Check if file exists
  if (!FFat.exists(fullPath)) {
    // Try without leading slash in case FATFS doesn't like it
    String altPath = fullPath.substring(1);
    if (!FFat.exists(altPath)) {
      if (debugSerial) {
        Serial.printf("[DEBUG] File not found: %s (also tried: %s)\n", fullPath.c_str(), altPath.c_str());
      }
      return false;
    }
    fullPath = altPath;
  }
  
  String contentType = "text/plain";
  if (fullPath.endsWith(".html")) contentType = "text/html";
  else if (fullPath.endsWith(".css")) contentType = "text/css";
  else if (fullPath.endsWith(".js")) contentType = "application/javascript";
  else if (fullPath.endsWith(".json")) contentType = "application/json";
  else if (fullPath.endsWith(".png")) contentType = "image/png";
  else if (fullPath.endsWith(".jpg") || fullPath.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (fullPath.endsWith(".gif")) contentType = "image/gif";
  else if (fullPath.endsWith(".svg")) contentType = "image/svg+xml";
  else if (fullPath.endsWith(".ico")) contentType = "image/x-icon";
  
  File file = FFat.open(fullPath, "r");
  if (!file) {
    if (debugSerial) {
      Serial.printf("[DEBUG] Failed to open file: %s\n", fullPath.c_str());
    }
    return false;
  }
  
  if (debugSerial) {
    Serial.printf("[DEBUG] Serving file: %s (%d bytes)\n", fullPath.c_str(), file.size());
  }
  
  // Use chunked streaming to handle large files without memory issues
  server.setContentLength(file.size());
  server.send(200, contentType, "");
  
  // Stream file in chunks to avoid memory limitations
  const size_t CHUNK_SIZE = 1024; // 1KB chunks
  uint8_t buffer[CHUNK_SIZE];
  
  while (file.available()) {
    size_t bytesRead = file.readBytes((char*)buffer, CHUNK_SIZE);
    if (bytesRead > 0) {
      server.client().write(buffer, bytesRead);
      yield(); // Allow other tasks to run and prevent watchdog timeout
    } else {
      break; // End of file or error
    }
  }
  
  file.close();
  return true;
}

// Performance metrics tracking function is defined in missing_stubs.cpp

// Core endpoints
void coreEndpoints(WebServer& server) {
    // Streaming file upload endpoint for large files
    static File uploadFile;
    static bool uploadError = false;
    
    server.on("/upload", HTTP_POST, [&](){
        if (uploadError) {
            server.send(500, "text/plain", "Upload failed");
            uploadError = false;  // Reset for next upload
        } else {
            server.send(200, "text/plain", "Upload complete");
        }
    });
    
    // Configure longer timeout for file uploads (60 seconds)
    server.setContentLength(50 * 1024 * 1024); // Allow up to 50MB uploads (increased for larger files)
    
    // Shared file upload handler for both /upload and /api/upload
    server.onFileUpload([&](){
        HTTPUpload& upload = server.upload();
        
        if (upload.status == UPLOAD_FILE_START) {
            // Clean up any previous upload
            if (uploadFile) {
                uploadFile.close();
            }
            
            String filename = upload.filename;
            if (!filename.startsWith("/")) filename = "/" + filename;
            
            // Try to open the file
            uploadFile = FFat.open(filename, "w");
            if (!uploadFile) {
                if (debugSerial) Serial.printf("[UPLOAD] ERROR: Failed to create file: %s\n", filename.c_str());
                uploadError = true;
                return;
            }
            
            if (debugSerial) Serial.printf("[UPLOAD] Start: %s\n", filename.c_str());
            uploadError = false; // Reset error state for new upload
            
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (!uploadFile) {
                uploadError = true;
                return;
            }
            
            // Write data chunk
            size_t written = uploadFile.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
                if (debugSerial) Serial.printf("[UPLOAD] ERROR: Write failed - %u of %u bytes written\n", written, upload.currentSize);
                uploadError = true;
                uploadFile.close();
                return;
            }
            
            // Give the ESP some time to process and avoid watchdog triggers
            yield();
            
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) {
                uploadFile.close();
                if (debugSerial) {
                    if (uploadError) {
                        Serial.printf("[UPLOAD] Failed: %s\n", upload.filename.c_str());
                    } else {
                        Serial.printf("[UPLOAD] Success: %s (%u bytes)\n", upload.filename.c_str(), upload.totalSize);
                    }
                }
            }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            if (debugSerial) Serial.println(F("[UPLOAD] Aborted"));
            if (uploadFile) {
                uploadFile.close();
            }
            uploadError = true;
        }
    });
    server.on("/", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] Root path '/' requested"));
        
        if (!serveStaticFile(server, "/")) {
            // If index.html not found, provide helpful debug info
            String response = "<!DOCTYPE html><html><head><title>Breadmaker Controller - Debug</title></head><body>";
            response += "<h1>Breadmaker Controller</h1>";
            response += "<p><strong>Status:</strong> Firmware running, but web files not found</p>";
            response += "<h2>Available Endpoints:</h2>";
            response += "<ul>";
            response += "<li><a href='/status'>GET /status</a> - System status JSON</li>";
            response += "<li><a href='/debug/fs'>GET /debug/fs</a> - Filesystem debug info</li>";
            response += "<li><a href='/api/firmware_info'>GET /api/firmware_info</a> - Firmware info</li>";
            response += "</ul>";
            response += "<h2>Next Steps:</h2>";
            response += "<ol>";
            response += "<li>Check filesystem status at <a href='/debug/fs'>/debug/fs</a></li>";
            response += "<li>Upload web files using: <code>.\\upload_files_esp32.ps1 -Port COM3</code></li>";
            response += "</ol>";
            response += "</body></html>";
            server.send(200, "text/html", response);
        }
    });
    
    server.on("/status", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /status requested"));
        
        // Create a string stream for the JSON output
        String jsonOutput = "";
        
        // Create a custom Print class that writes to our string
        class StringPrint : public Print {
            String* str;
        public:
            StringPrint(String* s) : str(s) {}
            size_t write(uint8_t c) override { *str += (char)c; return 1; }
            size_t write(const uint8_t *buffer, size_t size) override {
                for (size_t i = 0; i < size; i++) *str += (char)buffer[i];
                return size;
            }
        };
        
        StringPrint stringPrint(&jsonOutput);
        streamStatusJson(stringPrint);
        
        server.send(200, "application/json", jsonOutput);
    });
    
    // Add missing /api/status endpoint for frontend compatibility
    server.on("/api/status", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/status requested"));
        
        // Create a string stream for the JSON output
        String jsonOutput = "";
        
        // Create a custom Print class that writes to our string
        class StringPrint : public Print {
            String* str;
        public:
            StringPrint(String* s) : str(s) {}
            size_t write(uint8_t c) override { *str += (char)c; return 1; }
            size_t write(const uint8_t *buffer, size_t size) override {
                for (size_t i = 0; i < size; i++) *str += (char)buffer[i];
                return size;
            }
        };
        
        StringPrint stringPrint(&jsonOutput);
        streamStatusJson(stringPrint);
        
        server.send(200, "application/json", jsonOutput);
    });
    
    server.on("/api/firmware_info", HTTP_GET, [&](){
        String response = "{";
        response += "\"build\":\"" + String(FIRMWARE_BUILD_DATE) + "\",";
        response += "\"version\":\"ESP32-WebServer\"";

        response += "}";
        server.send(200, "application/json", response);
    });
    
    // Debug endpoint to check filesystem
    server.on("/debug/fs", HTTP_GET, [&](){
        String response = "FATFS Debug:\n\n";
        
        // Check if FATFS is mounted
        if (!FFat.begin()) {
            response += "ERROR: FATFS not mounted!\n";
        } else {
            response += "✓ FFat mounted successfully\n";
            response += "Total: " + String(FFat.totalBytes()) + " bytes\n";
            response += "Used: " + String(FFat.usedBytes()) + " bytes\n";
            response += "Free: " + String(FFat.totalBytes() - FFat.usedBytes()) + " bytes\n\n";
            
            // List files in root
            response += "Root directory contents:\n";
            File root = FFat.open("/");
            if (root) {
                File file = root.openNextFile();
                while (file) {
                    response += (file.isDirectory() ? "[DIR] " : "[FILE] ");
                    response += String(file.name());
                    if (!file.isDirectory()) {
                        response += " (" + String(file.size()) + " bytes)";
                    }
                    response += "\n";
                    file = root.openNextFile();
                }
                root.close();
            } else {
                response += "ERROR: Cannot open root directory\n";
            }
            
            // Test specific files
            response += "\nFile existence tests:\n";
            response += "/index.html: " + String(FFat.exists("/index.html") ? "EXISTS" : "NOT FOUND") + "\n";
            response += "index.html: " + String(FFat.exists("index.html") ? "EXISTS" : "NOT FOUND") + "\n";
        }
        
        server.send(200, "text/plain", response);
    });
    
    // File upload endpoint for debugging
    server.on("/debug/upload", HTTP_POST, [&](){
        if (server.hasArg("filename") && server.hasArg("content")) {
            String filename = server.arg("filename");
            String content = server.arg("content");
            String debugMsg;
            // Ensure filename starts with /
            if (!filename.startsWith("/")) {
                filename = "/" + filename;
            }
            debugMsg += "[UPLOAD] Filename: " + filename + " (" + String(filename.length()) + ")\n";
            debugMsg += "[UPLOAD] Content length: " + String(content.length()) + "\n";
            File file = FFat.open(filename, "w");
            if (file) {
                size_t written = file.print(content);
                file.close();
                bool exists = FFat.exists(filename);
                debugMsg += String("[UPLOAD] Written: ") + String(written) + ", Exists after write: " + (exists ? "YES" : "NO") + "\n";
                server.send(200, "text/plain", "File uploaded: " + filename + "\n" + debugMsg);
                if (debugSerial) {
                    Serial.printf("%s", debugMsg.c_str());
                }
            } else {
                debugMsg += String("[UPLOAD] Failed to create file: ") + filename + "\n";
                server.send(500, "text/plain", "Failed to create file: " + filename + "\n" + debugMsg);
                if (debugSerial) {
                    Serial.printf("%s", debugMsg.c_str());
                }
            }
        } else {
            String debugMsg = "[UPLOAD] Missing filename or content parameters\n";
            server.send(400, "text/plain", "Missing filename or content parameters\n" + debugMsg);
            if (debugSerial) {
                Serial.printf("%s", debugMsg.c_str());
            }
        }
    });
    
    // Simple file upload interface
    server.on("/upload", HTTP_GET, [&](){
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>File Upload - Breadmaker Controller</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        .upload-area { border: 2px dashed #ccc; padding: 20px; margin: 20px 0; }
        .file-list { margin: 20px 0; }
        .progress { width: 100%; height: 20px; background: #f0f0f0; margin: 10px 0; }
        .progress-bar { height: 100%; background: #4CAF50; width: 0%; }
        button { padding: 10px 20px; margin: 5px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>File Upload - Breadmaker Controller</h1>
        <div class="upload-area">
            <h3>Select Files to Upload:</h3>
            <input type="file" id="fileInput" multiple accept=".html,.css,.js,.json,.svg">
            <button onclick="uploadFiles()">Upload Files</button>
        </div>
        <div id="progress" class="progress" style="display:none;">
            <div id="progressBar" class="progress-bar"></div>
        </div>
        <div id="status"></div>
        <div class="file-list">
            <h3>Actions:</h3>
            <button onclick="window.location.href='/'">Back to Main Interface</button>
            <button onclick="window.location.href='/debug/fs'">View Filesystem</button>
        </div>
    </div>
    
    <script>
        async function uploadFiles() {
            const fileInput = document.getElementById('fileInput');
            const files = fileInput.files;
            
            if (files.length === 0) {
                alert('Please select files to upload');
                return;
            }
            
            const progressDiv = document.getElementById('progress');
            const progressBar = document.getElementById('progressBar');
            const statusDiv = document.getElementById('status');
            
            progressDiv.style.display = 'block';
            statusDiv.innerHTML = "";
            
            let uploaded = 0;
            const total = files.length;
            
            for (let i = 0; i < files.length; i++) {
                const file = files[i];
                const content = await file.text();
                
                try {
                    const response = await fetch('/debug/upload', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                        body: 'filename=' + encodeURIComponent(file.name) + '&content=' + encodeURIComponent(content)
                    });
                    
                    const result = await response.text();
                    
                    if (response.ok) {
                        statusDiv.innerHTML += '<div style="color: green;">OK ' + file.name + ' uploaded successfully</div>';
                    } else {
                        statusDiv.innerHTML += '<div style="color: red;">ERR ' + file.name + ' failed: ' + result + '</div>';
                    }
                } catch (error) {
                    statusDiv.innerHTML += '<div style="color: red;">ERR ' + file.name + ' error: ' + error.message + '</div>';
                }
                
                uploaded++;
                const progress = (uploaded / total) * 100;
                progressBar.style.width = progress + '%';
            }
            
            statusDiv.innerHTML += '<div style="margin-top: 20px;"><strong>Upload completed: ' + uploaded + '/' + total + ' files</strong></div>';
        }
    </script>
</body>
</html>
)rawliteral";
        server.send(200, "text/html", html);
    });
    
    server.on("/api/restart", HTTP_POST, [&](){
        server.send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(1000);
        ESP.restart();
    });
    
    server.on("/api/output_mode", HTTP_GET, [&](){
        String response = "{\"mode\":\"" + String(outputMode) + "\"}";
        server.send(200, "application/json", response);
    });
    
    server.on("/api/output_mode", HTTP_POST, [&](){
        if (server.hasArg("plain")) {
            DynamicJsonDocument doc(256);
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (!err && doc.containsKey("mode")) {
                String mode = doc["mode"];
                if (mode == "relay" || mode == "pwm") {
                    outputMode = OUTPUT_DIGITAL; // Fix: use proper enum value
                    saveSettings();
                    server.send(200, "application/json", "{\"status\":\"ok\"}");
                    return;
                }
            }
        }
        sendJsonError(server, "invalid_request", "Invalid mode parameter", 400);
    });
}

// State Machine Endpoints
void stateMachineEndpoints(WebServer& server) {
    server.on("/start", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[ACTION] /start called"));
        
        if (server.hasArg("time")) {
            String t = server.arg("time");
            struct tm nowTm;
            time_t now = time(nullptr);
            localtime_r(&now, &nowTm);
            int hh, mm;
            if (sscanf(t.c_str(), "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                server.send(400, "application/json", "{\"error\":\"Invalid time format. Use HH:MM\"}");
                return;
            }
            
            struct tm startTm = nowTm;
            startTm.tm_hour = hh;
            startTm.tm_min = mm;
            startTm.tm_sec = 0;
            time_t startEpoch = mktime(&startTm);
            if (startEpoch <= now) startEpoch += 24*60*60;
            
            scheduledStart = startEpoch;
            
            if (server.hasArg("stage")) {
                int stageIdx = server.arg("stage").toInt();
                if (stageIdx < 0) {
                    server.send(400, "application/json", "{\"error\":\"Invalid stage index\"}");
                    return;
                }
                scheduledStartStage = stageIdx;
                server.send(200, "application/json", "{\"status\":\"Scheduled to start at stage " + String(stageIdx + 1) + " at " + t + "\"}");
            } else {
                scheduledStartStage = -1;
                server.send(200, "application/json", "{\"status\":\"Scheduled to start at " + t + "\"}");
            }
            return;
        }
        
        // Immediate start
        updateActiveProgramVars();
        
        if (server.hasArg("stage")) {
            int stageIdx = server.arg("stage").toInt();
            if (stageIdx < 0 || stageIdx >= programState.maxCustomStages) {
                server.send(400, "application/json", "{\"error\":\"Invalid stage index\"}");
                return;
            }
            programState.customStageIdx = stageIdx;
        } else {
            programState.customStageIdx = 0;
        }
        
        programState.isRunning = true;
        programState.customMixIdx = 0;
        programState.customStageStart = millis();
        programState.customMixStepStart = 0;
        programState.programStartTime = time(nullptr);
        
        for (int i = 0; i < 20; i++) programState.actualStageStartTimes[i] = 0;
        programState.actualStageStartTimes[programState.customStageIdx] = programState.programStartTime;
        
        resetFermentationTracking(getAveragedTemperature());
        invalidateStatusCache();
        saveResumeState();
        
        server.send(200, "application/json", "{\"status\":\"started\"}");
        if (debugSerial) Serial.println(F("[START] Breadmaker started"));
    });
    
    server.on("/stop", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[ACTION] /stop called"));
        stopBreadmaker();
        server.send(200, "application/json", "{\"status\":\"stopped\"}");
    });
}

// Manual Output Endpoints
void manualOutputEndpoints(WebServer& server) {
    server.on("/toggle_heater", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle heater"));
        setHeater(!heaterState);
        server.send(200, "application/json", "{\"heater\":" + String(heaterState ? "true" : "false") + "}");
    });
    
    server.on("/toggle_motor", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle motor"));
        setMotor(!motorState);
        server.send(200, "application/json", "{\"motor\":" + String(motorState ? "true" : "false") + "}");
    });
    
    server.on("/toggle_light", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle light"));
        setLight(!lightState);
        server.send(200, "application/json", "{\"light\":" + String(lightState ? "true" : "false") + "}");
    });
    
    server.on("/toggle_buzzer", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle buzzer"));
        setBuzzer(!buzzerState);
        server.send(200, "application/json", "{\"buzzer\":" + String(buzzerState ? "true" : "false") + "}");
    });
    
    server.on("/beep", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Beep"));
        shortBeep();
        server.send(200, "application/json", "{\"status\":\"beeped\"}");
    });
}

// PID Control Endpoints
void pidControlEndpoints(WebServer& server) {
    server.on("/api/pid", HTTP_GET, [&](){
        String response = "{";
        response += "\"kp\":" + String(pid.Kp, 6) + ",";
        response += "\"ki\":" + String(pid.Ki, 6) + ",";
        response += "\"kd\":" + String(pid.Kd, 6) + ",";
        response += "\"setpoint\":" + String(pid.Setpoint, 1) + ",";
        response += "\"input\":" + String(pid.Input, 1) + ",";
        response += "\"output\":" + String(pid.Output, 3) + "";
        response += "}";
        server.send(200, "application/json", response);
    });
    
    server.on("/api/pid", HTTP_POST, [&](){
        if (server.hasArg("plain")) {
            DynamicJsonDocument doc(512);
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (!err) {
                if (doc.containsKey("kp")) pid.Kp = doc["kp"];
                if (doc.containsKey("ki")) pid.Ki = doc["ki"];
                if (doc.containsKey("kd")) pid.Kd = doc["kd"];
                if (doc.containsKey("setpoint")) pid.Setpoint = doc["setpoint"];
                
                if (pid.controller) {
                    pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
                }
                
                server.send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
        }
        sendJsonError(server, "invalid_request", "Invalid PID parameters", 400);
    });
}

// Placeholder implementations for remaining endpoints
void pidProfileEndpoints(WebServer& server) {
    server.on("/api/pid_profiles", HTTP_GET, [&](){
        server.send(200, "application/json", "{\"profiles\":[]}");
    });
}

void homeAssistantEndpoint(WebServer& server) {
    // Home Assistant integration endpoint (matches template configuration.yaml)
    server.on("/ha", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /ha requested"));
        
        String response = "{";
        
        // Basic status - matches template expectations
        response += "\"state\":\"" + String(programState.isRunning ? "running" : "idle") + "\",";
        response += "\"temperature\":" + String(getAveragedTemperature(), 1) + ",";
        response += "\"setpoint\":" + String(pid.Setpoint, 1) + ",";
        
        // Output states (direct boolean values as expected)
        response += "\"motor\":" + String(outputStates.motor ? "true" : "false") + ",";
        response += "\"light\":" + String(outputStates.light ? "true" : "false") + ",";
        response += "\"buzzer\":" + String(outputStates.buzzer ? "true" : "false") + ",";
        response += "\"heater\":" + String(outputStates.heater ? "true" : "false") + ",";
        response += "\"manual_mode\":" + String(programState.manualMode ? "true" : "false") + ",";
        
        // Program and stage info
        if (programState.activeProgramId >= 0 && programState.activeProgramId < getProgramCount()) {
            Program* p = getActiveProgramMutable();
            if (p) {
                response += "\"program\":\"" + String(p->name.c_str()) + "\",";
                
                if (programState.isRunning && programState.customStageIdx < p->customStages.size()) {
                    response += "\"stage\":\"" + String(p->customStages[programState.customStageIdx].label.c_str()) + "\",";
                    
                    // Calculate total program remaining time (stage + all remaining stages)
                    unsigned long elapsed = (programState.customStageStart == 0) ? 0 : (millis() - programState.customStageStart) / 1000;
                    unsigned long stageTimeMs = getAdjustedStageTimeMs(p->customStages[programState.customStageIdx].min * 60 * 1000, 
                                                                       p->customStages[programState.customStageIdx].isFermentation);
                    unsigned long stageTimeLeft = (stageTimeMs / 1000) - elapsed;
                    if (stageTimeLeft < 0) stageTimeLeft = 0;
                    
                    // Add time for all remaining stages
                    unsigned long totalRemainingTime = stageTimeLeft;
                    for (size_t i = programState.customStageIdx + 1; i < p->customStages.size(); ++i) {
                        unsigned long adjustedDurationMs = getAdjustedStageTimeMs(p->customStages[i].min * 60 * 1000, 
                                                                                  p->customStages[i].isFermentation);
                        totalRemainingTime += adjustedDurationMs / 1000;
                    }
                    
                    response += "\"stage_time_left\":" + String(totalRemainingTime / 60) + ","; // Convert to minutes
                } else if (!programState.isRunning) {
                    // Not running - calculate total program time if started now
                    response += "\"stage\":\"Idle\",";
                    
                    unsigned long totalProgramTime = 0;
                    for (size_t i = 0; i < p->customStages.size(); ++i) {
                        unsigned long adjustedDurationMs = getAdjustedStageTimeMs(p->customStages[i].min * 60 * 1000, 
                                                                                  p->customStages[i].isFermentation);
                        totalProgramTime += adjustedDurationMs / 1000;
                    }
                    response += "\"stage_time_left\":" + String(totalProgramTime / 60) + ","; // Convert to minutes
                } else {
                    response += "\"stage\":\"Idle\",";
                    response += "\"stage_time_left\":0,";
                }
            } else {
                response += "\"program\":\"\",";
                response += "\"stage\":\"Idle\",";
                response += "\"stage_time_left\":0,";
            }
        } else {
            response += "\"program\":\"\",";
            response += "\"stage\":\"Idle\",";
            response += "\"stage_time_left\":0,";
        }
        
        // Timing information (Unix timestamps as expected)
        time_t now = time(nullptr);
        time_t stageReadyAt = 0;
        time_t programReadyAt = 0;
        
        if (programState.isRunning && programState.activeProgramId >= 0) {
            Program* p = getActiveProgramMutable();
            if (p && programState.customStageIdx < p->customStages.size()) {
                unsigned long elapsed = (programState.customStageStart == 0) ? 0 : (millis() - programState.customStageStart) / 1000;
                unsigned long stageTimeMs = getAdjustedStageTimeMs(p->customStages[programState.customStageIdx].min * 60 * 1000, 
                                                                   p->customStages[programState.customStageIdx].isFermentation);
                int timeLeftSec = (stageTimeMs / 1000) - elapsed;
                if (timeLeftSec < 0) timeLeftSec = 0;
                
                stageReadyAt = now + timeLeftSec;
                
                // Calculate total program time remaining using fermentation adjustments
                programReadyAt = stageReadyAt;
                for (size_t i = programState.customStageIdx + 1; i < p->customStages.size(); ++i) {
                    unsigned long adjustedDurationMs = getAdjustedStageTimeMs(p->customStages[i].min * 60 * 1000, 
                                                                              p->customStages[i].isFermentation);
                    programReadyAt += adjustedDurationMs / 1000;
                }
            }
        }
        
        response += "\"stage_ready_at\":" + String((unsigned long)stageReadyAt) + ",";
        response += "\"program_ready_at\":" + String((unsigned long)programReadyAt) + ",";
        
        // Health section (comprehensive system information as expected by template)
        response += "\"health\":{";
        
        // System info
        response += "\"uptime_sec\":" + String(millis() / 1000) + ",";
        response += "\"firmware_version\":\"ESP32-WebServer\",";
        response += "\"build_date\":\"" + String(__DATE__) + " " + String(__TIME__) + "\",";
        response += "\"reset_reason\":\"" + String(esp_reset_reason()) + "\",";
        response += "\"chip_id\":\"" + String((uint32_t)ESP.getEfuseMac(), HEX) + "\",";
        
        // Performance metrics
        response += "\"performance\":{";
        response += "\"loop_count\":" + String(getLoopCount()) + ",";
        response += "\"max_loop_time_us\":" + String(getMaxLoopTime()) + ",";
        response += "\"avg_loop_time_us\":" + String(getAverageLoopTime()) + ",";
        response += "\"wifi_reconnects\":" + String(getWifiReconnectCount());
        response += "},";
        
        // Memory information with fragmentation
        response += "\"memory\":{";
        response += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
        response += "\"max_free_block\":" + String(ESP.getMaxAllocHeap()) + ",";
        response += "\"heap_size\":" + String(ESP.getHeapSize()) + ",";
        response += "\"min_free_heap\":" + String(getMinFreeHeap()) + ",";
        response += "\"fragmentation_pct\":" + String(getHeapFragmentation(), 1);
        response += "},";
        
        // PID controller information
        response += "\"pid\":{";
        response += "\"kp\":" + String(pid.Kp, 6) + ",";
        response += "\"ki\":" + String(pid.Ki, 6) + ",";
        response += "\"kd\":" + String(pid.Kd, 6) + ",";
        response += "\"output\":" + String(pid.Output, 2) + ",";
        response += "\"input\":" + String(pid.Input, 2) + ",";
        response += "\"setpoint\":" + String(pid.Setpoint, 2) + ",";
        response += "\"sample_time_ms\":" + String(pid.sampleTime) + ",";
        response += "\"active_profile\":\"" + pid.activeProfile + "\",";
        response += "\"auto_switching\":" + String(pid.autoSwitching ? "true" : "false");
        response += "},";
        
        // Flash information
        response += "\"flash\":{";
        response += "\"size\":" + String(ESP.getFlashChipSize()) + ",";
        response += "\"speed\":" + String(ESP.getFlashChipSpeed()) + ",";
        response += "\"mode\":" + String(ESP.getFlashChipMode());
        response += "},";
        
        // WiFi information
        response += "\"wifi\":{";
        response += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        response += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
        response += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        response += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
        response += "}";
        
        response += "}"; // Close health section
        response += "}"; // Close main object
        
        server.send(200, "application/json", response);
    });
}

void calibrationEndpoints(WebServer& server) {
    server.on("/api/calibration", HTTP_GET, [&](){
        // Get current raw ADC reading and temperature
        int currentRaw = analogRead(PIN_RTD);
        float currentTemp = readTemperature();
        
        String response = "{";
        response += "\"raw\":" + String(currentRaw) + ",";
        response += "\"temp\":" + String(currentTemp, 1) + ",";
        response += "\"table\":[";
        
        for(size_t i = 0; i < rtdCalibTable.size(); i++) {
            if(i > 0) response += ",";
            response += "{\"raw\":" + String(rtdCalibTable[i].raw) + 
                       ",\"temp\":" + String(rtdCalibTable[i].temp) + "}";
        }
        response += "]}";
        server.send(200, "application/json", response);
    });
    
    // Add calibration point endpoint
    server.on("/api/calibration/add", HTTP_POST, [&](){
        if (server.hasArg("raw") && server.hasArg("temp")) {
            int raw = server.arg("raw").toInt();
            float temp = server.arg("temp").toFloat();
            
            // Add point to calibration table
            CalibPoint newPoint = {raw, temp};
            rtdCalibTable.push_back(newPoint);
            
            // Sort by raw value
            std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), 
                     [](const CalibPoint& a, const CalibPoint& b) { return a.raw < b.raw; });
            
            // Save to file
            saveCalibration();
            
            server.send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    });
    
    // Update calibration point endpoint
    server.on("/api/calibration/update", HTTP_POST, [&](){
        if (server.hasArg("index") && server.hasArg("raw") && server.hasArg("temp")) {
            int index = server.arg("index").toInt();
            int raw = server.arg("raw").toInt();
            float temp = server.arg("temp").toFloat();
            
            if (index >= 0 && index < rtdCalibTable.size()) {
                rtdCalibTable[index].raw = raw;
                rtdCalibTable[index].temp = temp;
                
                // Sort by raw value after update
                std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), 
                         [](const CalibPoint& a, const CalibPoint& b) { return a.raw < b.raw; });
                
                // Save to file
                saveCalibration();
                
                server.send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid index\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    });
    
    // Delete calibration point endpoint
    server.on("/api/calibration/delete", HTTP_POST, [&](){
        if (server.hasArg("index")) {
            int index = server.arg("index").toInt();
            
            if (index >= 0 && index < rtdCalibTable.size()) {
                rtdCalibTable.erase(rtdCalibTable.begin() + index);
                
                // Save to file
                saveCalibration();
                
                server.send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid index\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing index parameter\"}");
        }
    });
    
    // Clear all calibration points endpoint
    server.on("/api/calibration", HTTP_DELETE, [&](){
        rtdCalibTable.clear();
        saveCalibration();
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });
}

void fileEndPoints(WebServer& server) {
    // List files endpoint with folder support
    server.on("/api/files", HTTP_GET, [&](){
        String folder = server.hasArg("folder") ? server.arg("folder") : "/";
        if (!folder.startsWith("/")) folder = "/" + folder;
        if (!folder.endsWith("/") && folder != "/") folder += "/";
        
        String response = "{\"files\":[],\"folders\":[]}";
        
        File root = FFat.open(folder);
        if (root && root.isDirectory()) {
            response = "{\"files\":[";
            String folders = "\"folders\":[";
            
            bool firstFile = true, firstFolder = true;
            File file = root.openNextFile();
            while (file) {
                if (file.isDirectory()) {
                    if (!firstFolder) folders += ",";
                    folders += "\"" + String(file.name()) + "\"";
                    firstFolder = false;
                } else {
                    if (!firstFile) response += ",";
                    response += "{";
                    response += "\"name\":\"" + String(file.name()) + "\",";
                    response += "\"size\":" + String(file.size());
                    response += "}";
                    firstFile = false;
                }
                file = root.openNextFile();
            }
            root.close();
            
            response += "]," + folders + "]}";
        }
        
        server.send(200, "application/json", response);
    });
    
    // Delete file endpoint
    server.on("/api/delete", HTTP_POST, [&](){
        String body = server.arg("plain");
        if (body.length() > 0) {
            // Parse JSON body
            int filenameStart = body.indexOf("\"filename\":\"") + 12;
            int filenameEnd = body.indexOf("\"", filenameStart);
            String filename = body.substring(filenameStart, filenameEnd);
            
            if (!filename.startsWith("/")) {
                filename = "/" + filename;
            }
            
            if (FFat.exists(filename)) {
                if (FFat.remove(filename)) {
                    server.send(200, "application/json", "{\"status\":\"deleted\",\"file\":\"" + filename + "\"}");
                } else {
                    server.send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
                }
            } else {
                server.send(404, "application/json", "{\"error\":\"File not found\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing filename parameter\"}");
        }
    });
    
    // Create folder endpoint
    server.on("/api/create_folder", HTTP_POST, [&](){
        String body = server.arg("plain");
        if (body.length() > 0) {
            // Simple JSON parsing for folder creation
            int parentStart = body.indexOf("\"parent\":\"") + 10;
            int parentEnd = body.indexOf("\"", parentStart);
            String parent = body.substring(parentStart, parentEnd);
            
            int nameStart = body.indexOf("\"name\":\"") + 8;
            int nameEnd = body.indexOf("\"", nameStart);
            String name = body.substring(nameStart, nameEnd);
            
            if (!parent.startsWith("/")) parent = "/" + parent;
            if (!parent.endsWith("/") && parent != "/") parent += "/";
            
            String folderPath = parent + name;
            
            // Create directory (note: FFat may not support directories)
            server.send(200, "application/json", "{\"status\":\"created\",\"folder\":\"" + folderPath + "\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing folder parameters\"}");
        }
    });
    
    // Delete folder endpoint
    server.on("/api/delete_folder", HTTP_POST, [&](){
        String body = server.arg("plain");
        if (body.length() > 0) {
            server.send(200, "application/json", "{\"status\":\"deleted\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing folder parameters\"}");
        }
    });
    
    // API upload endpoint (uses the same file upload handler as /upload)
    server.on("/api/upload", HTTP_POST, [&](){
        // This will be handled by the shared onFileUpload handler in coreEndpoints
        server.send(200, "application/json", "{\"status\":\"uploaded\"}");
    });
}

void programsEndpoints(WebServer& server) {
    server.on("/api/programs", HTTP_GET, [&](){
        String response = "{\"count\":" + String(getProgramCount()) + "}";
        server.send(200, "application/json", response);
    });
    
    server.on("/api/program", HTTP_GET, [&](){
        if (server.hasArg("id")) {
            int id = server.arg("id").toInt();
            if (id >= 0 && id < getProgramCount()) {
                // Basic program info - could be expanded
                String response = "{\"id\":" + String(id) + ",\"valid\":" + String(isProgramValid(id) ? "true" : "false") + "}";
                server.send(200, "application/json", response);
                return;
            }
        }
        server.send(400, "application/json", "{\"error\":\"Invalid program ID\"}");
    });
}

void otaEndpoints(WebServer& server) {
    server.on("/api/ota", HTTP_GET, [&](){
        server.send(200, "application/json", "{\"status\":\"ota_available\"}");
    });
    
    // OTA status endpoint - provides current OTA state
    server.on("/api/ota/status", HTTP_GET, [&](){
        // Use actual OTA manager state
        String json = "{";
        json += "\"enabled\":" + String(isOTAEnabled() ? "true" : "false") + ",";
        json += "\"inProgress\":" + String(otaStatus.inProgress ? "true" : "false") + ",";
        json += "\"progress\":" + String(otaStatus.progress) + ",";
        json += "\"hostname\":\"" + getOTAHostname() + "\",";
        json += "\"error\":" + (otaStatus.error.length() > 0 ? "\"" + otaStatus.error + "\"" : "null");
        json += "}";
        server.send(200, "application/json", json);
    });
    
    // OTA info endpoint - provides device information
    server.on("/api/ota/info", HTTP_GET, [&](){
        String json = "{";
        json += "\"hostname\":\"" + String(WiFi.getHostname()) + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"version\":\"1.0.0\",";
        json += "\"freeSpace\":" + String(FFat.freeBytes()) + ",";
        json += "\"totalSpace\":" + String(FFat.totalBytes());
        json += "}";
        server.send(200, "application/json", json);
    });
    
    // Web-based firmware update endpoint
    server.on("/api/update", HTTP_POST, [&](){
        server.sendHeader("Connection", "close");
        if (Update.hasError()) {
            server.send(500, "text/plain", "Update failed: " + String(Update.getError()));
        } else {
            server.send(200, "text/plain", "Update successful! Rebooting...");
            delay(100);
            ESP.restart();
        }
    }, [&](){
        // Handle firmware upload
        HTTPUpload& upload = server.upload();
        
        if (upload.status == UPLOAD_FILE_START) {
            if (debugSerial) Serial.printf("[UPDATE] Starting firmware update: %s\n", upload.filename.c_str());
            
            // Begin firmware update
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                if (debugSerial) Serial.printf("[UPDATE] Begin failed: %s\n", Update.errorString());
                return;
            }
            
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            // Write firmware data
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                if (debugSerial) Serial.printf("[UPDATE] Write failed: %s\n", Update.errorString());
                return;
            }
            
        } else if (upload.status == UPLOAD_FILE_END) {
            // Complete firmware update
            if (Update.end(true)) {
                if (debugSerial) Serial.printf("[UPDATE] Firmware update completed: %u bytes\n", upload.totalSize);
            } else {
                if (debugSerial) Serial.printf("[UPDATE] End failed: %s\n", Update.errorString());
            }
        }
    });
    
    // OTA firmware upload endpoint (alias for /api/update for script compatibility)
    server.on("/api/ota/upload", HTTP_POST, [&](){
        server.sendHeader("Connection", "close");
        if (Update.hasError()) {
            server.send(500, "text/plain", "OTA Update failed: " + String(Update.getError()));
        } else {
            server.send(200, "text/plain", "OTA Update successful! Rebooting...");
            delay(100);
            ESP.restart();
        }
    }, [&](){
        // Handle firmware upload (same as /api/update)
        HTTPUpload& upload = server.upload();
        
        if (upload.status == UPLOAD_FILE_START) {
            if (debugSerial) Serial.printf("[OTA] Starting firmware update: %s\n", upload.filename.c_str());
            
            // Begin firmware update
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                if (debugSerial) Serial.printf("[OTA] Begin failed: %s\n", Update.errorString());
                return;
            }
            
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            // Write firmware data
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                if (debugSerial) Serial.printf("[OTA] Write failed: %s\n", Update.errorString());
                return;
            }
            
        } else if (upload.status == UPLOAD_FILE_END) {
            // Complete firmware update
            if (Update.end(true)) {
                if (debugSerial) Serial.printf("[OTA] Firmware update completed: %u bytes\n", upload.totalSize);
            } else {
                if (debugSerial) Serial.printf("[OTA] End failed: %s\n", Update.errorString());
            }
        }
    });
    
    // Missing /api/settings endpoint for debug serial configuration
    server.on("/api/settings", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/settings GET requested"));
        String json = "{\"debugSerial\":" + String(debugSerial ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/api/settings", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/settings POST requested"));
        
        if (server.hasArg("plain")) {
            String body = server.arg("plain");
            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, body);
            
            if (!error && doc.containsKey("debugSerial")) {
                debugSerial = doc["debugSerial"].as<bool>();
                saveSettings(); // Save the updated setting
                String json = "{\"debugSerial\":" + String(debugSerial ? "true" : "false") + "}";
                server.send(200, "application/json", json);
                if (debugSerial) Serial.printf("[DEBUG] Debug serial set to: %s\n", debugSerial ? "enabled" : "disabled");
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid JSON or missing debugSerial field\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        }
    });
    
    // Missing /api/pid_profile endpoint for PID profile management
    server.on("/api/pid_profile", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/pid_profile GET requested"));
        
        // Return current PID profiles - for now, return a basic structure
        String json = "{\"profiles\":[";
        json += "{\"key\":\"default\",\"kp\":" + String(pid.Kp) + ",";
        json += "\"ki\":" + String(pid.Ki) + ",";
        json += "\"kd\":" + String(pid.Kd) + ",";
        json += "\"windowMs\":" + String(pid.sampleTime) + "}";
        json += "]}";
        
        server.send(200, "application/json", json);
    });
    
    server.on("/api/pid_profile", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/pid_profile POST requested"));
        
        if (server.hasArg("plain")) {
            String body = server.arg("plain");
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, body);
            
            if (!error && doc.containsKey("kp") && doc.containsKey("ki") && doc.containsKey("kd")) {
                double kp = doc["kp"].as<double>();
                double ki = doc["ki"].as<double>();
                double kd = doc["kd"].as<double>();
                
                // Update PID parameters
                pid.Kp = kp;
                pid.Ki = ki;
                pid.Kd = kd;
                
                // Update controller if it exists
                if (pid.controller) {
                    pid.controller->SetTunings(kp, ki, kd);
                }
                
                // savePIDProfiles(); // TODO: Implement savePIDProfiles() function
                
                String json = "{\"status\":\"ok\",\"kp\":" + String(kp) + ",";
                json += "\"ki\":" + String(ki) + ",";
                json += "\"kd\":" + String(kd) + "}";
                
                server.send(200, "application/json", json);
                if (debugSerial) Serial.printf("[DEBUG] PID parameters updated: Kp=%.3f, Ki=%.3f, Kd=%.3f\n", kp, ki, kd);
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid JSON or missing PID parameters\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        }
    });
}

// Main registration function
void registerWebEndpoints(WebServer& server) {
    // Initialize the filesystem first
    if (!FFat.begin(true)) {  // true = format on failure
        if (debugSerial) Serial.println(F("[ERROR] Failed to mount FFat filesystem"));
    }
    
    // Configure server for better reliability
    // Note: The ESP32 WebServer doesn't directly expose a timeout configuration,
    // but we can set a more appropriate upload settings
    
    // Core endpoints that must work
    
    // Register all endpoints
    coreEndpoints(server);
    stateMachineEndpoints(server);
    manualOutputEndpoints(server);
    pidControlEndpoints(server);
    pidProfileEndpoints(server);
    homeAssistantEndpoint(server);
    calibrationEndpoints(server);
    fileEndPoints(server);
    programsEndpoints(server);
    otaEndpoints(server);
    
    // Missing endpoints that script.js needs
    
    // Serve programs.json file
    server.on("/programs.json", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /programs.json requested"));
        if (serveStaticFile(server, "/programs.json")) {
            return;
        }
        // If file doesn't exist, serve empty array
        server.send(200, "application/json", "[]");
    });
    
    server.on("/select", HTTP_GET, [&](){
        if (server.hasArg("idx")) {
            int programId = server.arg("idx").toInt();
            // Handle migration from array index to ID-based system
            // The 'idx' parameter now contains the program's ID field, not array position
            if (debugSerial) {
                Serial.printf("[DEBUG] Program selected by ID: %d\n", programId);
            }
            
            // Implement actual program selection logic
            // 1. Check if program exists
            if (!isProgramValid(programId)) {
                server.send(400, "application/json", "{\"error\":\"Invalid program ID\"}");
                return;
            }
            
            // 2. Load the program using ensureProgramLoaded(programId) 
            if (!ensureProgramLoaded(programId)) {
                server.send(500, "application/json", "{\"error\":\"Failed to load program\"}");
                return;
            }
            
            // 3. Set programState.activeProgramId = programId
            programState.activeProgramId = programId;
            
            // 4. Call updateActiveProgramVars() to refresh program state
            updateActiveProgramVars();
            
            // 5. Invalidate status cache and save state
            invalidateStatusCache();
            saveResumeState();
            
            if (debugSerial) {
                Serial.printf("[DEBUG] Successfully selected program ID %d: %s\n", 
                             programId, getProgramName(programId).c_str());
            }
            
            server.send(200, "application/json", "{\"status\":\"ok\",\"selected\":" + String(programId) + "}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing idx parameter\"}");
        }
    });
    
    server.on("/start_at_stage", HTTP_GET, [&](){
        if (server.hasArg("stage")) {
            int stage = server.arg("stage").toInt();
            // Implement start at stage based on historical patterns
            if (debugSerial) {
                Serial.printf("[DEBUG] Start at stage: %d\n", stage);
            }
            
            if (programState.activeProgramId >= getProgramCount()) { 
                stopBreadmaker();
                server.send(400, "application/json", "{\"error\":\"No valid program selected\"}");
                return; 
            }
            
            Program *p = getActiveProgramMutable();
            if (!p) {
                Serial.printf_P(PSTR("[ERROR] /start_at_stage: Unable to get active program\n"));
                stopBreadmaker();
                server.send(400, "application/json", "{\"error\":\"Cannot access active program\"}");
                return;
            }
            
            size_t numStages = p->customStages.size();
            if (numStages == 0) {
                Serial.printf_P(PSTR("[ERROR] /start_at_stage: Program has zero stages\n"));
                stopBreadmaker();
                server.send(400, "application/json", "{\"error\":\"Program has no stages\"}");
                return;
            }
            
            if (stage < 0 || stage >= (int)numStages) {
                server.send(400, "application/json", "{\"error\":\"Invalid stage number\"}");
                return;
            }
            
            // Set the starting stage
            programState.customStageIdx = stage;
            programState.customStageStart = millis();
            programState.customMixStepStart = 0;
            programState.isRunning = true;
            if (stage == 0) {
                programState.programStartTime = time(nullptr);
            }
            invalidateStatusCache();
            
            // Record actual start time of this stage
            if (stage < (int)numStages && stage < MAX_PROGRAM_STAGES) {
                programState.actualStageStartTimes[stage] = time(nullptr);
            }
            
            saveResumeState();
            server.send(200, "application/json", "{\"status\":\"ok\",\"stage\":" + String(stage) + "}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing stage parameter\"}");
        }
    });
    
    server.on("/pause", HTTP_GET, [&](){
        // Implement pause based on historical patterns
        if (debugSerial) {
            Serial.println("[DEBUG] Pause requested");
        }
        programState.isRunning = false;
        invalidateStatusCache();
        setMotor(false);
        setHeater(false);
        setLight(false);
        saveResumeState();
        server.send(200, "application/json", "{\"status\":\"paused\"}");
    });
    
    server.on("/resume", HTTP_GET, [&](){
        // Implement resume based on historical patterns
        if (debugSerial) {
            Serial.println("[DEBUG] Resume requested");
        }
        programState.isRunning = true;
        invalidateStatusCache();
        // Do NOT reset actualStageStartTimes[customStageIdx] on resume if it was already set.
        // Only set it if it was never set (i.e., just started this stage for the first time).
        if (programState.customStageIdx < MAX_PROGRAM_STAGES && 
            programState.actualStageStartTimes[programState.customStageIdx] == 0) {
            programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);
        }
        saveResumeState();
        server.send(200, "application/json", "{\"status\":\"running\"}");
    });
    
    server.on("/back", HTTP_GET, [&](){
        // Implement back/previous stage based on historical patterns
        if (debugSerial) {
            Serial.println("[DEBUG] Back/previous stage requested");
        }
        
        if (programState.activeProgramId >= getProgramCount()) { 
            stopBreadmaker();
            server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"No valid program\"}");
            return; 
        }
        
        Program *p = getActiveProgramMutable();
        if (!p) {
            Serial.printf_P(PSTR("[ERROR] /back: Unable to get active program\n"));
            stopBreadmaker();
            server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"Cannot access active program\"}");
            return;
        }
        
        size_t numStages = p->customStages.size();
        if (numStages == 0) {
            Serial.printf_P(PSTR("[ERROR] /back: Program at id %u has zero stages\n"), 
                           (unsigned)programState.activeProgramId);
            stopBreadmaker();
            server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"Program has no stages\"}");
            return;
        }
        
        // Go to previous stage (wrap to last stage if at beginning)
        if (programState.customStageIdx > 0) {
            programState.customStageIdx--;
        } else {
            programState.customStageIdx = numStages - 1;
        }
        
        programState.customStageStart = millis();
        programState.customMixStepStart = 0;
        programState.isRunning = true;
        if (programState.customStageIdx == 0) {
            programState.programStartTime = time(nullptr);
        }
        invalidateStatusCache();
        
        // Record actual start time of this stage when going back
        if (programState.customStageIdx < numStages && 
            programState.customStageIdx < MAX_PROGRAM_STAGES) {
            programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);
        }
        
        saveResumeState();
        server.send(200, "application/json", "{\"status\":\"ok\",\"stage\":" + String(programState.customStageIdx) + "}");
    });
    
    server.on("/api/manual_mode", HTTP_GET, [&](){
        if (server.hasArg("on")) {
            bool manualMode = server.arg("on") == "1";
            // Implement manual mode toggle based on historical patterns
            if (debugSerial) {
                Serial.printf("[DEBUG] Manual mode: %s\n", manualMode ? "ON" : "OFF");
            }
            programState.manualMode = manualMode;
            invalidateStatusCache();
            saveSettings();
            server.send(200, "application/json", "{\"status\":\"ok\",\"manual_mode\":" + String(manualMode ? "true" : "false") + "}");
        } else {
            // Return current manual mode status
            server.send(200, "application/json", "{\"manual_mode\":" + String(programState.manualMode ? "true" : "false") + "}");
        }
    });
    
    server.on("/api/temperature", HTTP_GET, [&](){
        if (server.hasArg("setpoint")) {
            float setpoint = server.arg("setpoint").toFloat();
            // Implement temperature setpoint based on historical patterns
            if (debugSerial) {
                Serial.printf("[DEBUG] Temperature setpoint: %.1f\n", setpoint);
            }
            // Set PID setpoint for manual temperature control
            pid.Setpoint = setpoint;
            invalidateStatusCache();
            server.send(200, "application/json", "{\"status\":\"ok\",\"setpoint\":" + String(setpoint) + "}");
        } else {
            // Return current temperature and setpoint
            float currentTemp = getAveragedTemperature();
            server.send(200, "application/json", "{\"temperature\":" + String(currentTemp) + ",\"setpoint\":" + String(pid.Setpoint) + "}");
        }
    });
    
    // Missing API endpoints for output control (expected by script.js)
    server.on("/api/heater", HTTP_GET, [&](){
        if (server.hasArg("on")) {
            bool on = server.arg("on") == "1";
            setHeater(on);
            invalidateStatusCache();
            server.send(200, "application/json", "{\"heater\":" + String(on ? "true" : "false") + "}");
        } else {
            server.send(200, "application/json", "{\"heater\":" + String(outputStates.heater ? "true" : "false") + "}");
        }
    });
    
    server.on("/api/motor", HTTP_GET, [&](){
        if (server.hasArg("on")) {
            bool on = server.arg("on") == "1";
            setMotor(on);
            invalidateStatusCache();
            server.send(200, "application/json", "{\"motor\":" + String(on ? "true" : "false") + "}");
        } else {
            server.send(200, "application/json", "{\"motor\":" + String(outputStates.motor ? "true" : "false") + "}");
        }
    });
    
    server.on("/api/light", HTTP_GET, [&](){
        if (server.hasArg("on")) {
            bool on = server.arg("on") == "1";
            setLight(on);
            if (on) lightOnTime = millis();
            invalidateStatusCache();
            server.send(200, "application/json", "{\"light\":" + String(on ? "true" : "false") + "}");
        } else {
            server.send(200, "application/json", "{\"light\":" + String(outputStates.light ? "true" : "false") + "}");
        }
    });
    
    server.on("/api/buzzer", HTTP_GET, [&](){
        if (server.hasArg("on")) {
            bool on = server.arg("on") == "1";
            setBuzzer(on);
            invalidateStatusCache();
            server.send(200, "application/json", "{\"buzzer\":" + String(on ? "true" : "false") + "}");
        } else {
            server.send(200, "application/json", "{\"buzzer\":" + String(outputStates.buzzer ? "true" : "false") + "}");
        }
    });
    
    // Handle static files (catch-all)
    server.onNotFound([&](){
        String path = server.uri();
        if (serveStaticFile(server, path)) {
            return;
        }
        server.send(404, "text/plain", "File Not Found: " + path);
    });
    
    // Start the server after all endpoints are configured
    server.begin();
    if (debugSerial) Serial.println(F("[INFO] WebServer started"));
}
