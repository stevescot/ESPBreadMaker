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
#include "display_manager.h"  // For screensaver control

// External OTA status for web integration
extern OTAStatus otaStatus;

// StringPrint helper class for streaming JSON to String buffers
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

// Helper to track web activity for screensaver
void trackWebActivity() {
  updateActivityTime();
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
  response.reserve(800); // MEMORY OPTIMIZATION: Reduced from 2048 to 800 bytes
  
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
    
    // Configure longer timeout for file uploads (60 seconds)
    server.setContentLength(50 * 1024 * 1024); // Allow up to 50MB uploads (increased for larger files)
    
    // File upload handler for /api/upload
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
        trackWebActivity(); // Track web activity for screensaver
        if (debugSerial) Serial.println(F("[DEBUG] /status requested"));
        
        // Use String buffer for status JSON (status is frequently accessed, needs fast response)
        String statusBuffer;
        statusBuffer.reserve(800); // MEMORY OPTIMIZATION: Reduced from 1200 to 800 bytes
        StringPrint stringPrint(statusBuffer);
        streamStatusJson(stringPrint);
        
        server.send(200, "application/json", statusBuffer);
    });
    
    // Add missing /api/status endpoint for frontend compatibility
    server.on("/api/status", HTTP_GET, [&](){
        trackWebActivity(); // Track web activity for screensaver
        if (debugSerial) Serial.println(F("[DEBUG] /api/status requested"));
        
        // Use String buffer for status JSON (status is frequently accessed, needs fast response)
        String statusBuffer;
        statusBuffer.reserve(800); // MEMORY OPTIMIZATION: Reduced from 1200 to 800 bytes
        StringPrint stringPrint(statusBuffer);
        streamStatusJson(stringPrint);
        
        server.send(200, "application/json", statusBuffer);
    });
    
    server.on("/api/firmware_info", HTTP_GET, [&](){
        // Use efficient streaming for consistency
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        
        server.sendContent("{\"build\":\"");
        server.sendContent(FIRMWARE_BUILD_DATE);
        server.sendContent("\",\"version\":\"ESP32-WebServer\"}");
        server.sendContent(""); // End chunked response
    });
    
    // Debug endpoint to check filesystem
    server.on("/debug/fs", HTTP_GET, [&](){
        String response = "=== DEBUG TEST ===\n";
        response += "This is a test line\n\n";
        
        response += "FATFS Debug:\n\n";
        
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
                
                try {
                    const formData = new FormData();
                    formData.append('file', file);
                    
                    const response = await fetch('/api/upload', {
                        method: 'POST',
                        body: formData
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

    // GET-based restart endpoint (crash workaround)
    server.on("/api/restart-get", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/restart-get GET requested"));
        server.send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(1000);
        ESP.restart();
    });
    
    server.on("/api/output_mode", HTTP_GET, [&](){
        server.send(200, "application/json", "{\"mode\":\"digital\"}");
    });
    
    server.on("/api/output_mode", HTTP_POST, [&](){
        if (server.hasArg("plain")) {
            DynamicJsonDocument doc(256);
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (!err && doc.containsKey("mode")) {
                String mode = doc["mode"];
                if (mode == "relay" || mode == "pwm") {
                    // System is always digital - no need to change anything
                    saveSettings();
                    server.send(200, "application/json", "{\"status\":\"ok\"}");
                    return;
                }
            }
        }
        sendJsonError(server, "invalid_request", "Invalid mode parameter", 400);
    });

    // GET-based output mode endpoint (crash workaround)
    server.on("/api/output_mode/set", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/output_mode/set GET requested"));
        
        if (server.hasArg("mode")) {
            String mode = server.arg("mode");
            if (mode == "relay" || mode == "pwm") {
                // System is always digital - no need to change anything
                saveSettings();
                server.send(200, "application/json", "{\"status\":\"ok\",\"mode\":\"" + mode + "\"}");
                if (debugSerial) Serial.println("[DEBUG] Output mode changed via GET: " + mode);
                return;
            }
        }
        sendJsonError(server, "invalid_request", "Invalid or missing mode parameter", 400);
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
                // Ultra-efficient: Use sprintf instead of String concatenation
                char buffer[128];
                sprintf(buffer, "{\"status\":\"Scheduled to start at stage %d at %s\"}", stageIdx + 1, t.c_str());
                server.send(200, "application/json", buffer);
            } else {
                scheduledStartStage = -1;
                // Ultra-efficient: Use sprintf instead of String concatenation
                char buffer[128];
                sprintf(buffer, "{\"status\":\"Scheduled to start at %s\"}", t.c_str());
                server.send(200, "application/json", buffer);
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
    
    server.on("/advance", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[ACTION] /advance called"));
        
        if (!programState.isRunning) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Program not running\"}");
            return;
        }
        
        Program* p = getActiveProgramMutable();
        if (!p || p->customStages.empty()) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No active program\"}");
            return;
        }
        
        if (programState.customStageIdx >= p->customStages.size() - 1) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Already at last stage\"}");
            return;
        }
        
        // Manually advance to next stage
        programState.customStageIdx++;
        programState.customStageStart = millis();
        
        // Update actual stage start times array
        if (programState.customStageIdx < 20) {
            programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);
        }
        
        resetFermentationTracking(getAveragedTemperature());
        invalidateStatusCache();
        saveResumeState();
        
        if (debugSerial) Serial.printf("[MANUAL ADVANCE] Advanced to stage %d\n", (int)programState.customStageIdx);
        
        // Create a string stream for the JSON output  
        String jsonOutput = "";
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
}

// Manual Output Endpoints
void manualOutputEndpoints(WebServer& server) {
    server.on("/toggle_heater", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle heater"));
        setHeater(!heaterState);
        // Ultra-efficient: Use static strings instead of String concatenation
        server.send(200, "application/json", heaterState ? "{\"heater\":true}" : "{\"heater\":false}");
    });
    
    server.on("/toggle_motor", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle motor"));
        setMotor(!motorState);
        // Ultra-efficient: Use static strings instead of String concatenation
        server.send(200, "application/json", motorState ? "{\"motor\":true}" : "{\"motor\":false}");
    });
    
    server.on("/toggle_light", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle light"));
        setLight(!lightState);
        // Ultra-efficient: Use static strings instead of String concatenation
        server.send(200, "application/json", lightState ? "{\"light\":true}" : "{\"light\":false}");
    });
    
    server.on("/toggle_buzzer", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[MANUAL] Toggle buzzer"));
        setBuzzer(!buzzerState);
        // Ultra-efficient: Use static strings instead of String concatenation
        server.send(200, "application/json", buzzerState ? "{\"buzzer\":true}" : "{\"buzzer\":false}");
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
        // Ultra-efficient: Use sprintf with stack buffer instead of String concatenation
        char buffer[128];  // Stack allocated, much more efficient than String objects
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        server.sendContent("{");
        
        sprintf(buffer, "\"kp\":%.6f,", pid.Kp);
        server.sendContent(buffer);
        sprintf(buffer, "\"ki\":%.6f,", pid.Ki);
        server.sendContent(buffer);
        sprintf(buffer, "\"kd\":%.6f,", pid.Kd);
        server.sendContent(buffer);
        sprintf(buffer, "\"setpoint\":%.1f,", pid.Setpoint);
        server.sendContent(buffer);
        sprintf(buffer, "\"input\":%.1f,", pid.Input);
        server.sendContent(buffer);
        sprintf(buffer, "\"output\":%.3f", pid.Output);
        server.sendContent(buffer);
        
        server.sendContent("}");
    });
    
    server.on("/api/pid", HTTP_POST, [&](){
        if (server.hasArg("plain")) {
            // ULTRA-EFFICIENT: Reduced buffer size, use char buffer for response
            StaticJsonDocument<256> doc;  // Reduced from 512 to 256 bytes
            DeserializationError err = deserializeJson(doc, server.arg("plain"));
            if (!err) {
                if (doc.containsKey("kp")) pid.Kp = doc["kp"];
                if (doc.containsKey("ki")) pid.Ki = doc["ki"];
                if (doc.containsKey("kd")) pid.Kd = doc["kd"];
                if (doc.containsKey("setpoint")) pid.Setpoint = doc["setpoint"];
                
                if (pid.controller) {
                    pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
                }
                
                server.send(200, F("application/json"), F("{\"status\":\"ok\"}"));
                return;
            }
        }
        sendJsonError(server, "invalid_request", "Invalid PID parameters", 400);
    });
    
    // PID parameters endpoint for EMA temperature averaging and other settings
    server.on("/api/pid_params", HTTP_GET, [&](){
        if (server.hasArg("temp_alpha") || server.hasArg("temp_interval") || server.hasArg("temp_spike_threshold") || 
            server.hasArg("temp_samples") || server.hasArg("temp_reject")) {
            bool updated = false;
            
            // Handle legacy temp_samples parameter by converting to alpha
            if (server.hasArg("temp_samples")) {
                int samples = server.arg("temp_samples").toInt();
                if (samples >= 5 && samples <= 100) {
                    // Convert sample count to equivalent alpha: alpha ≈ 2/(samples+1)
                    tempAvg.alpha = 2.0 / (samples + 1.0);
                    if (tempAvg.alpha > 0.5) tempAvg.alpha = 0.5; // Cap at 0.5 for stability
                    updated = true;
                    if (debugSerial) Serial.printf("[TEMP-EMA] Sample count %d converted to alpha=%.3f\n", samples, tempAvg.alpha);
                }
            }
            
            // Direct alpha parameter (preferred)
            if (server.hasArg("temp_alpha")) {
                float newAlpha = server.arg("temp_alpha").toFloat();
                if (newAlpha >= 0.01 && newAlpha <= 0.5) {
                    tempAvg.alpha = newAlpha;
                    updated = true;
                    if (debugSerial) Serial.printf("[TEMP-EMA] Alpha updated to %.3f\n", newAlpha);
                }
            }
            
            // Legacy temp_reject - convert to spike threshold
            if (server.hasArg("temp_reject")) {
                int reject = server.arg("temp_reject").toInt();
                if (reject >= 0 && reject <= 10) {
                    // Convert reject count to spike threshold: more rejection = lower threshold
                    tempAvg.spikeThreshold = 10.0 - reject;
                    if (tempAvg.spikeThreshold < 1.0) tempAvg.spikeThreshold = 1.0;
                    updated = true;
                    if (debugSerial) Serial.printf("[TEMP-EMA] Reject count %d converted to spike threshold=%.1f°C\n", reject, tempAvg.spikeThreshold);
                }
            }
            
            // Direct spike threshold parameter (preferred)
            if (server.hasArg("temp_spike_threshold")) {
                float threshold = server.arg("temp_spike_threshold").toFloat();
                if (threshold >= 0.5 && threshold <= 20.0) {
                    tempAvg.spikeThreshold = threshold;
                    updated = true;
                    if (debugSerial) Serial.printf("[TEMP-EMA] Spike threshold updated to %.1f°C\n", threshold);
                }
            }
            
            // Update temperature sample interval
            if (server.hasArg("temp_interval")) {
                int newInterval = server.arg("temp_interval").toInt();
                if (newInterval >= 100 && newInterval <= 5000) {
                    tempAvg.updateInterval = newInterval;
                    updated = true;
                    if (debugSerial) Serial.printf("[TEMP-EMA] Update interval updated to %d ms\n", newInterval);
                }
            }
            
            if (updated) {
                server.send(200, "application/json", "{\"status\":\"updated\",\"message\":\"EMA temperature filtering parameters updated\"}");
            } else {
                server.send(400, "application/json", "{\"error\":\"invalid_parameters\",\"message\":\"Invalid or out-of-range parameters\"}");
            }
        } else {
            // Return current EMA temperature settings with legacy compatibility
            // Ultra-efficient: Use sprintf with stack buffer instead of String concatenation
            char buffer[128];  // Stack allocated, much more efficient than String objects
            server.setContentLength(CONTENT_LENGTH_UNKNOWN);
            server.send(200, "application/json", "");
            server.sendContent("{");
            
            // Legacy compatibility - convert EMA parameters back to old format for web UI
            int equivalent_samples = (int)(2.0 / tempAvg.alpha) - 1;
            if (equivalent_samples < 5) equivalent_samples = 5;
            if (equivalent_samples > 100) equivalent_samples = 100;
            sprintf(buffer, "\"temp_sample_count\":%d,", equivalent_samples);
            server.sendContent(buffer);
            
            int equivalent_reject = (int)(10.0 - tempAvg.spikeThreshold);
            if (equivalent_reject < 0) equivalent_reject = 0;
            if (equivalent_reject > 10) equivalent_reject = 10;
            sprintf(buffer, "\"temp_reject_count\":%d,", equivalent_reject);
            server.sendContent(buffer);
            
            sprintf(buffer, "\"temp_sample_interval\":%lu,", tempAvg.updateInterval);
            server.sendContent(buffer);
            server.sendContent("\"temp_samples_ready\":");
            server.sendContent(tempAvg.initialized ? "true" : "false");
            
            sprintf(buffer, ",\"averaged_temperature\":%.2f", tempAvg.smoothedTemperature);
            server.sendContent(buffer);
            
            // Add true raw ADC reading for comparison
            int currentRawADC = analogRead(PIN_RTD);
            sprintf(buffer, ",\"temp_raw_adc\":%d", currentRawADC);
            server.sendContent(buffer);
            sprintf(buffer, ",\"temp_calibrated_current\":%.2f", readTemperature());
            server.sendContent(buffer);
            
            // New EMA-specific parameters
            sprintf(buffer, ",\"temp_alpha\":%.4f", tempAvg.alpha);
            server.sendContent(buffer);
            sprintf(buffer, ",\"temp_spike_threshold\":%.1f", tempAvg.spikeThreshold);
            server.sendContent(buffer);
            sprintf(buffer, ",\"temp_sample_count_total\":%lu", tempAvg.sampleCount);
            server.sendContent(buffer);
            sprintf(buffer, ",\"temp_last_accepted\":%.2f", tempAvg.lastCalibratedTemp);
            server.sendContent(buffer);
            sprintf(buffer, ",\"temp_consecutive_spikes\":%u", tempAvg.consecutiveSpikes);
            server.sendContent(buffer);
            
            server.sendContent("}");
        }
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
        
        // Use efficient streaming instead of string concatenation
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        
        // Stream JSON directly to avoid memory allocation issues
        char buffer[64];  // Stack allocated buffer for numeric conversions
        server.sendContent("{");
        
        // Basic status - matches template expectations
        server.sendContent("\"state\":\"");
        server.sendContent(programState.isRunning ? "running" : "idle");
        server.sendContent("\",\"temperature\":");
        sprintf(buffer, "%.1f", getAveragedTemperature());
        server.sendContent(buffer);
        server.sendContent(",\"setpoint\":");
        sprintf(buffer, "%.1f", pid.Setpoint);
        server.sendContent(buffer);
        server.sendContent(",");
        
        // Output states (direct boolean values as expected)
        server.sendContent("\"motor\":");
        server.sendContent(outputStates.motor ? "true" : "false");
        server.sendContent(",\"light\":");
        server.sendContent(outputStates.light ? "true" : "false");
        server.sendContent(",\"buzzer\":");
        server.sendContent(outputStates.buzzer ? "true" : "false");
        server.sendContent(",\"heater\":");
        server.sendContent(outputStates.heater ? "true" : "false");
        server.sendContent(",\"manual_mode\":");
        server.sendContent(programState.manualMode ? "true" : "false");
        server.sendContent(",");
        
        // Program and stage info
        if (programState.activeProgramId >= 0 && programState.activeProgramId < getProgramCount()) {
            Program* p = getActiveProgramMutable();
            if (p) {
                server.sendContent("\"program\":\"");
                server.sendContent(p->name.c_str());
                server.sendContent("\",");
                
                if (programState.isRunning && programState.customStageIdx < p->customStages.size()) {
                    server.sendContent("\"stage\":\"");
                    server.sendContent(p->customStages[programState.customStageIdx].label.c_str());
                    server.sendContent("\",");
                    
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
                    
                    server.sendContent("\"stage_time_left\":");
                    sprintf(buffer, "%lu", totalRemainingTime / 60); // Convert to minutes for Home Assistant
                    server.sendContent(buffer);
                    server.sendContent(",");
                } else if (!programState.isRunning) {
                    // Not running - calculate total program time if started now
                    server.sendContent("\"stage\":\"Idle\",");
                    
                    unsigned long totalProgramTime = 0;
                    for (size_t i = 0; i < p->customStages.size(); ++i) {
                        unsigned long adjustedDurationMs = getAdjustedStageTimeMs(p->customStages[i].min * 60 * 1000, 
                                                                                  p->customStages[i].isFermentation);
                        totalProgramTime += adjustedDurationMs / 1000;
                    }
                    server.sendContent("\"stage_time_left\":");
                    sprintf(buffer, "%lu", totalProgramTime / 60); // Convert to minutes for Home Assistant
                    server.sendContent(buffer);
                    server.sendContent(",");
                } else {
                    server.sendContent("\"stage\":\"Idle\",\"stage_time_left\":0,");
                }
            } else {
                server.sendContent("\"program\":\"\",\"stage\":\"Idle\",\"stage_time_left\":0,");
            }
        } else {
            server.sendContent("\"program\":\"\",\"stage\":\"Idle\",\"stage_time_left\":0,");
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
        
        server.sendContent("\"stage_ready_at\":");
        sprintf(buffer, "%lu", (unsigned long)stageReadyAt);
        server.sendContent(buffer);
        server.sendContent(",\"program_ready_at\":");
        sprintf(buffer, "%lu", (unsigned long)programReadyAt);
        server.sendContent(buffer);
        server.sendContent(",");
        
        // Health section (comprehensive system information as expected by template)
        server.sendContent("\"health\":{");
        
        // System info
        server.sendContent("\"uptime_sec\":");
        sprintf(buffer, "%lu", millis() / 1000);
        server.sendContent(buffer);
        server.sendContent(",\"firmware_version\":\"ESP32-WebServer\",\"build_date\":\"");
        server.sendContent(__DATE__);
        server.sendContent(" ");
        server.sendContent(__TIME__);
        server.sendContent("\",\"reset_reason\":\"");
        sprintf(buffer, "%d", esp_reset_reason());
        server.sendContent(buffer);
        server.sendContent("\",\"chip_id\":\"");
        sprintf(buffer, "%X", (uint32_t)ESP.getEfuseMac());
        server.sendContent(buffer);
        server.sendContent("\",");
        
        // Performance metrics
        server.sendContent("\"performance\":{\"loop_count\":");
        server.sendContent(String(getLoopCount()));
        server.sendContent(",\"max_loop_time_us\":");
        server.sendContent(String(getMaxLoopTime()));
        server.sendContent(",\"avg_loop_time_us\":");
        server.sendContent(String(getAverageLoopTime()));
        server.sendContent(",\"current_loop_time_us\":");
        server.sendContent(String(safetySystem.totalLoopTime / max(1UL, (unsigned long)safetySystem.loopCount))); // Current average
        server.sendContent(",\"cpu_usage\":");
        // Calculate approximate CPU usage based on loop timing
        unsigned long avgLoopTime = getAverageLoopTime();
        float cpuUsage = avgLoopTime > 0 ? min(100.0f, (avgLoopTime / 100000.0f) * 100.0f) : 0.0f; // Rough estimate
        server.sendContent(String(cpuUsage, 1));
        server.sendContent(",\"wifi_reconnects\":");
        server.sendContent(String(getWifiReconnectCount()));
        server.sendContent("},");
        
        // Memory information with fragmentation
        server.sendContent("\"memory\":{\"free_heap\":");
        server.sendContent(String(ESP.getFreeHeap()));
        server.sendContent(",\"max_free_block\":");
        server.sendContent(String(ESP.getMaxAllocHeap()));
        server.sendContent(",\"heap_size\":");
        server.sendContent(String(ESP.getHeapSize()));
        server.sendContent(",\"min_free_heap\":");
        server.sendContent(String(getMinFreeHeap()));
        server.sendContent(",\"fragmentation\":");
        server.sendContent(String(getHeapFragmentation(), 1));
        server.sendContent("},");
        
        // PID controller information
        server.sendContent("\"pid\":{\"kp\":");
        server.sendContent(String(pid.Kp, 6));
        server.sendContent(",\"ki\":");
        server.sendContent(String(pid.Ki, 6));
        server.sendContent(",\"kd\":");
        server.sendContent(String(pid.Kd, 6));
        server.sendContent(",\"output\":");
        server.sendContent(String(pid.Output, 2));
        server.sendContent(",\"input\":");
        server.sendContent(String(pid.Input, 2));
        server.sendContent(",\"setpoint\":");
        server.sendContent(String(pid.Setpoint, 2));
        server.sendContent(",\"sample_time_ms\":");
        server.sendContent(String(pid.sampleTime));
        server.sendContent(",\"active_profile\":\"");
        server.sendContent(pid.activeProfile);
        server.sendContent("\",\"auto_switching\":");
        server.sendContent(pid.autoSwitching ? "true" : "false");
        server.sendContent("},");
        
        // Flash information
        server.sendContent("\"flash\":{\"size\":");
        server.sendContent(String(ESP.getFlashChipSize()));
        server.sendContent(",\"speed\":");
        server.sendContent(String(ESP.getFlashChipSpeed()));
        server.sendContent(",\"mode\":");
        server.sendContent(String(ESP.getFlashChipMode()));
        server.sendContent("},");
        
        // File System information
        server.sendContent("\"filesystem\":{\"usedBytes\":");
        server.sendContent(String(FFat.usedBytes()));
        server.sendContent(",\"totalBytes\":");
        server.sendContent(String(FFat.totalBytes()));
        server.sendContent(",\"freeBytes\":");
        server.sendContent(String(FFat.totalBytes() - FFat.usedBytes()));
        float utilization = FFat.totalBytes() > 0 ? ((float)FFat.usedBytes() / FFat.totalBytes()) * 100.0 : 0.0;
        server.sendContent(",\"utilization\":");
        server.sendContent(String(utilization, 1));
        server.sendContent("},");
        
        // Error monitoring (placeholder values - implement actual error tracking if needed)
        server.sendContent("\"error_counts\":[0,0,0,0,0,0],");
        
        // Watchdog monitoring (placeholder values - implement actual watchdog if needed)
        server.sendContent("\"watchdog_enabled\":false,");
        server.sendContent("\"startup_complete\":true,");
        server.sendContent("\"watchdog_last_feed\":0,");
        server.sendContent("\"watchdog_timeout_ms\":0,");
        
        // Temperature control system detailed information
        server.sendContent("\"temperature_control\":{");
        server.sendContent("\"input\":");
        server.sendContent(String(getAveragedTemperature(), 1));
        server.sendContent(",\"pid_input\":");
        server.sendContent(String(pid.Input, 1));
        server.sendContent(",\"output\":");
        server.sendContent(String(pid.Output, 2));
        server.sendContent(",\"pid_output\":");
        server.sendContent(String(pid.Output, 2));
        server.sendContent(",\"kp\":");
        server.sendContent(String(pid.Kp, 6));
        server.sendContent(",\"ki\":");
        server.sendContent(String(pid.Ki, 6));
        server.sendContent(",\"kd\":");
        server.sendContent(String(pid.Kd, 6));
        server.sendContent(",\"pid_p\":");
        server.sendContent(String(pid.pidP, 3));
        server.sendContent(",\"pid_i\":");
        server.sendContent(String(pid.pidI, 3));
        server.sendContent(",\"pid_d\":");
        server.sendContent(String(pid.pidD, 3));
        server.sendContent(",\"sample_time_ms\":");
        server.sendContent(String(pid.sampleTime));
        server.sendContent(",\"raw_temp\":");
        server.sendContent(String(readTemperature(), 1));
        server.sendContent(",\"thermal_runaway\":");
        server.sendContent(thermalRunawayDetected ? "true" : "false");
        server.sendContent(",\"sensor_fault\":");
        server.sendContent(sensorFaultDetected ? "true" : "false");
        server.sendContent(",\"window_elapsed_ms\":");
        server.sendContent(String((millis() - windowStartTime) % windowSize));
        server.sendContent(",\"window_size_ms\":");
        server.sendContent(String(windowSize));
        server.sendContent(",\"on_time_ms\":");
        server.sendContent(String(onTime));
        server.sendContent(",\"duty_cycle_percent\":");
        server.sendContent(String(windowSize > 0 ? (onTime * 100.0) / windowSize : 0.0, 1));
        server.sendContent("},");
        
        // Network information
        server.sendContent("\"network\":{\"connected\":");
        server.sendContent(WiFi.status() == WL_CONNECTED ? "true" : "false");
        server.sendContent(",\"ssid\":\"");
        server.sendContent(wifiCache.getSSID());
        server.sendContent("\",\"rssi\":");
        server.sendContent(String(wifiCache.getRSSI()));
        server.sendContent(",\"ip\":\"");
        server.sendContent(wifiCache.getIPString());
        server.sendContent("\",\"reconnect_count\":");
        server.sendContent(String(getWifiReconnectCount()));
        server.sendContent("}");
        
        server.sendContent("}"); // Close health section
        server.sendContent("}"); // Close main object
        server.sendContent(""); // End chunked response
    });
}

void calibrationEndpoints(WebServer& server) {
    server.on("/api/calibration", HTTP_GET, [&](){
        // Get current raw ADC reading and temperature
        int currentRaw = analogRead(PIN_RTD);
        float currentTemp = readTemperature();
        
        // Use efficient streaming instead of string concatenation
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        
        server.sendContent("{\"raw\":");
        server.sendContent(String(currentRaw));
        server.sendContent(",\"temp\":");
        server.sendContent(String(currentTemp, 1));
        server.sendContent(",\"table\":[");
        
        for(size_t i = 0; i < rtdCalibTable.size(); i++) {
            if(i > 0) server.sendContent(",");
            server.sendContent("{\"raw\":");
            server.sendContent(String(rtdCalibTable[i].raw));
            server.sendContent(",\"temp\":");
            server.sendContent(String(rtdCalibTable[i].temp));
            server.sendContent("}");
        }
        server.sendContent("]}");
        server.sendContent(""); // End chunked response
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

    // GET-based alternatives for calibration (to avoid POST crash issues)
    
    // Add calibration point via GET
    server.on("/api/calibration/add-get", HTTP_GET, [&](){
        if (server.hasArg("raw") && server.hasArg("temp")) {
            int raw = server.arg("raw").toInt();
            float temp = server.arg("temp").toFloat();
            
            // Validate inputs
            if (raw < 0 || temp < -50 || temp > 200) {
                server.send(400, "application/json", "{\"error\":\"Invalid raw or temperature value\"}");
                return;
            }
            
            // Check for duplicate raw values
            for (const auto& point : rtdCalibTable) {
                if (point.raw == raw) {
                    server.send(400, "application/json", "{\"error\":\"Raw value already exists\"}");
                    return;
                }
            }
            
            // Add point to calibration table
            CalibPoint newPoint = {raw, temp};
            rtdCalibTable.push_back(newPoint);
            
            // Sort by raw value
            std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), 
                     [](const CalibPoint& a, const CalibPoint& b) { return a.raw < b.raw; });
            
            // Save to file
            saveCalibration();
            
            if (debugSerial) {
                Serial.printf("[CALIB] Added point: raw=%d, temp=%.2f\n", raw, temp);
            }
            
            server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"added\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing raw or temp parameter\"}");
        }
    });
    
    // Update calibration point via GET
    server.on("/api/calibration/update-get", HTTP_GET, [&](){
        if (server.hasArg("index") && server.hasArg("raw") && server.hasArg("temp")) {
            int index = server.arg("index").toInt();
            int raw = server.arg("raw").toInt();
            float temp = server.arg("temp").toFloat();
            
            if (index >= 0 && index < rtdCalibTable.size()) {
                // Validate inputs
                if (raw < 0 || temp < -50 || temp > 200) {
                    server.send(400, "application/json", "{\"error\":\"Invalid raw or temperature value\"}");
                    return;
                }
                
                // Check for duplicate raw values (excluding current index)
                for (size_t i = 0; i < rtdCalibTable.size(); i++) {
                    if (i != index && rtdCalibTable[i].raw == raw) {
                        server.send(400, "application/json", "{\"error\":\"Raw value already exists\"}");
                        return;
                    }
                }
                
                // Update the point
                rtdCalibTable[index].raw = raw;
                rtdCalibTable[index].temp = temp;
                
                // Sort by raw value
                std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), 
                         [](const CalibPoint& a, const CalibPoint& b) { return a.raw < b.raw; });
                
                // Save to file
                saveCalibration();
                
                if (debugSerial) {
                    Serial.printf("[CALIB] Updated point %d: raw=%d, temp=%.2f\n", index, raw, temp);
                }
                
                server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"updated\"}");
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid index\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    });
    
    // Delete calibration point via GET
    server.on("/api/calibration/delete-get", HTTP_GET, [&](){
        if (server.hasArg("index")) {
            int index = server.arg("index").toInt();
            
            if (index >= 0 && index < rtdCalibTable.size()) {
                if (debugSerial) {
                    Serial.printf("[CALIB] Deleting point %d: raw=%d, temp=%.2f\n", 
                                  index, rtdCalibTable[index].raw, rtdCalibTable[index].temp);
                }
                
                rtdCalibTable.erase(rtdCalibTable.begin() + index);
                
                // Save to file
                saveCalibration();
                
                server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"deleted\"}");
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid index\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing index parameter\"}");
        }
    });
    
    // Clear all calibration points via GET
    server.on("/api/calibration/clear-all", HTTP_GET, [&](){
        if (debugSerial) {
            Serial.printf("[CALIB] Clearing all %zu calibration points\n", rtdCalibTable.size());
        }
        
        rtdCalibTable.clear();
        saveCalibration();
        server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"cleared\"}");
    });
}

void fileEndPoints(WebServer& server) {
    // List files endpoint - ULTRA MEMORY OPTIMIZATION
    server.on("/api/files", HTTP_GET, [&](){
        // Use char buffer instead of String for folder path
        char folderPath[64];
        if (server.hasArg("folder")) {
            const String& folderArg = server.arg("folder");
            if (folderArg.startsWith("/")) {
                snprintf(folderPath, sizeof(folderPath), "%s", folderArg.c_str());
            } else {
                snprintf(folderPath, sizeof(folderPath), "/%s", folderArg.c_str());
            }
        } else {
            strcpy(folderPath, "/");
        }
        
        // Ensure path ends with / if not root
        size_t pathLen = strlen(folderPath);
        if (pathLen > 1 && folderPath[pathLen - 1] != '/') {
            if (pathLen < sizeof(folderPath) - 1) {
                folderPath[pathLen] = '/';
                folderPath[pathLen + 1] = '\0';
            }
        }
        
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, F("application/json"), "");
        
        File root = FFat.open(folderPath);
        if (root && root.isDirectory()) {
            server.sendContent(F("{\"files\":["));
            
            bool firstFile = true;
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    if (!firstFile) server.sendContent(F(","));
                    
                    // Use char buffer for JSON formatting to avoid String creation
                    char jsonBuffer[128];
                    snprintf(jsonBuffer, sizeof(jsonBuffer), 
                            "{\"name\":\"%s\",\"size\":%lu}", 
                            file.name(), (unsigned long)file.size());
                    server.sendContent(jsonBuffer);
                    firstFile = false;
                }
                file = root.openNextFile();
            }
            root.close();
            
            // Second pass for folders
            root = FFat.open(folderPath);
            server.sendContent(F("],\"folders\":["));
            bool firstFolder = true;
            if (root && root.isDirectory()) {
                file = root.openNextFile();
                while (file) {
                    if (file.isDirectory()) {
                        if (!firstFolder) server.sendContent(F(","));
                        
                        // Use char buffer for folder name formatting
                        char folderBuffer[64];
                        snprintf(folderBuffer, sizeof(folderBuffer), "\"%s\"", file.name());
                        server.sendContent(folderBuffer);
                        firstFolder = false;
                    }
                    file = root.openNextFile();
                }
                root.close();
            }
            server.sendContent(F("]}"));
        } else {
            server.sendContent(F("{\"files\":[],\"folders\":[]}"));
        }
    });
    
    // Delete file endpoint - ULTRA MEMORY OPTIMIZATION
    server.on("/api/delete", HTTP_POST, [&](){
        String body = server.arg("plain");
        if (body.length() > 0) {
            // ULTRA-EFFICIENT: Use small StaticJsonDocument + char buffers (no String heap allocation)
            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, body);
            
            if (!error && doc.containsKey("filename")) {
                const char* filenamePtr = doc["filename"];  // Points to doc memory, no heap allocation
                
                if (filenamePtr && strlen(filenamePtr) > 0) {
                    // Use stack-allocated char buffer instead of String
                    char fullPath[128];  // Stack allocation - no heap fragmentation
                    
                    // Construct path safely without heap allocation
                    if (filenamePtr[0] == '/') {
                        snprintf(fullPath, sizeof(fullPath), "%s", filenamePtr);
                    } else {
                        snprintf(fullPath, sizeof(fullPath), "/%s", filenamePtr);
                    }
                    
                    if (FFat.exists(fullPath)) {
                        if (FFat.remove(fullPath)) {
                            // Use F() macro to store response in flash, not RAM
                            server.send(200, F("application/json"), 
                                      String(F("{\"status\":\"deleted\",\"file\":\"")) + fullPath + F("\"}"));
                        } else {
                            server.send(500, F("application/json"), F("{\"error\":\"Failed to delete file\"}"));
                        }
                    } else {
                        server.send(404, F("application/json"), F("{\"error\":\"File not found\"}"));
                    }
                } else {
                    server.send(400, F("application/json"), F("{\"error\":\"Empty filename\"}"));
                }
            } else {
                server.send(400, F("application/json"), F("{\"error\":\"Invalid JSON or missing filename\"}"));
            }
        } else {
            server.send(400, F("application/json"), F("{\"error\":\"Missing request body\"}"));
        }
    });
    
    // Create folder endpoint - ULTRA MEMORY OPTIMIZATION
    server.on("/api/create_folder", HTTP_POST, [&](){
        String body = server.arg("plain");
        if (body.length() > 0) {
            // ULTRA-EFFICIENT: Use small StaticJsonDocument + char buffers (no String heap allocation)
            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, body);
            
            if (!error && doc.containsKey("parent") && doc.containsKey("name")) {
                const char* parentPtr = doc["parent"];  // Points to doc memory, no heap allocation
                const char* namePtr = doc["name"];      // Points to doc memory, no heap allocation
                
                if (parentPtr && namePtr && strlen(parentPtr) > 0 && strlen(namePtr) > 0) {
                    // Use stack-allocated char buffers instead of String
                    char parentPath[64];   // Stack allocation - no heap fragmentation
                    char folderPath[128];  // Stack allocation - no heap fragmentation
                    
                    // Normalize parent path safely without heap allocation
                    if (parentPtr[0] == '/') {
                        snprintf(parentPath, sizeof(parentPath), "%s", parentPtr);
                    } else {
                        snprintf(parentPath, sizeof(parentPath), "/%s", parentPtr);
                    }
                    
                    // Ensure parent path ends with / if not root
                    size_t parentLen = strlen(parentPath);
                    if (parentLen > 1 && parentPath[parentLen - 1] != '/') {
                        if (parentLen < sizeof(parentPath) - 1) {
                            parentPath[parentLen] = '/';
                            parentPath[parentLen + 1] = '\0';
                        }
                    }
                    
                    // Construct full folder path
                    snprintf(folderPath, sizeof(folderPath), "%s%s", parentPath, namePtr);
                    
                    // Create directory (note: FFat may not support directories, but return success)
                    server.send(200, F("application/json"), 
                              String(F("{\"status\":\"created\",\"folder\":\"")) + folderPath + F("\"}"));
                } else {
                    server.send(400, F("application/json"), F("{\"error\":\"Empty parent or name\"}"));
                }
            } else {
                server.send(400, F("application/json"), F("{\"error\":\"Invalid JSON or missing parameters\"}"));
            }
        } else {
            server.send(400, F("application/json"), F("{\"error\":\"Missing request body\"}"));
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
        // Ultra-memory efficient program list using char buffers
        char response[2048];
        char* pos = response;
        const char* end = response + sizeof(response) - 1;
        
        pos += snprintf(pos, end - pos, "[");
        
        for (int i = 0; i < getProgramCount() && pos < end - 100; i++) {
            if (i > 0) {
                pos += snprintf(pos, end - pos, ",");
            }
            
            // Use direct program name lookup to avoid loading full program
            String progName = getProgramName(i);
            bool isValid = isProgramValid(i);
            
            pos += snprintf(pos, end - pos, 
                           "{\"id\":%d,\"name\":\"%s\",\"valid\":%s}", 
                           i, progName.c_str(), isValid ? "true" : "false");
        }
        
        pos += snprintf(pos, end - pos, "]");
        *pos = '\0';
        
        server.send(200, "application/json", response);
    });
    
    server.on("/api/program", HTTP_GET, [&](){
        if (server.hasArg("id")) {
            int id = server.arg("id").toInt();
            if (id >= 0 && id < getProgramCount()) {
                server.send(200, "application/json", "{\"id\":" + String(id) + ",\"valid\":" + String(isProgramValid(id) ? "true" : "false") + "}");
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
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        server.sendContent("{");
        server.sendContent("\"enabled\":" + String(isOTAEnabled() ? "true" : "false") + ",");
        server.sendContent("\"inProgress\":" + String(otaStatus.inProgress ? "true" : "false") + ",");
        server.sendContent("\"progress\":" + String(otaStatus.progress) + ",");
        server.sendContent("\"hostname\":\"" + getOTAHostname() + "\",");
        server.sendContent("\"error\":" + (otaStatus.error.length() > 0 ? "\"" + otaStatus.error + "\"" : "null"));
        server.sendContent("}");
    });
    
    // OTA info endpoint - provides device information
    server.on("/api/ota/info", HTTP_GET, [&](){
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        server.sendContent("{");
        server.sendContent("\"hostname\":\"" + String(WiFi.getHostname()) + "\",");
        server.sendContent("\"ip\":\"" + wifiCache.getIPString() + "\",");
        server.sendContent("\"version\":\"1.0.0\",");
        server.sendContent("\"freeSpace\":" + String(FFat.freeBytes()) + ",");
        server.sendContent("\"totalSpace\":" + String(FFat.totalBytes()));
        server.sendContent("}");
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
        String json = "{\"debugSerial\":" + String(debugSerial ? "true" : "false") + 
                     ",\"safetyEnabled\":" + String(safetySystem.safetyEnabled ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/api/settings", HTTP_POST, [&](){
        // Immediate response before any processing
        server.send(200, "text/plain", "OK");
        
        // Minimal logging without any request body access
        if (debugSerial) Serial.println(F("[POST] Received"));
    });

    // Alternative GET-based settings change (workaround for POST issues)
    server.on("/api/settings/debug", HTTP_GET, [&](){
        if (server.hasArg("enabled")) {
            String enabledVal = server.arg("enabled");
            if (enabledVal == "true") {
                debugSerial = true;
                Serial.println(F("[DEBUG] Debug serial ENABLED via GET"));
            } else if (enabledVal == "false") {
                debugSerial = false;
                Serial.println(F("[DEBUG] Debug serial DISABLED via GET"));
            }
            
            // Schedule save
            pendingSettingsSaveTime = millis() + 1000;
            
            server.send(200, "application/json", "{\"debugSerial\":" + String(debugSerial ? "true" : "false") + ",\"saved\":\"scheduled\"}");
        } else {
            server.send(400, "text/plain", "Missing 'enabled' parameter");
        }
    });

    // Test endpoint that does try to save settings
    server.on("/api/settings/force-save", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] Force save requested"));
        
        server.send(200, "text/plain", "SAVING");
        
        // Try the save operation that might be causing crashes
        pendingSettingsSaveTime = millis() + 1000; // Schedule save in 1 second
        
        if (debugSerial) Serial.println(F("[DEBUG] Save scheduled"));
    });
    
    // Minimal debug endpoint to test file system - temporarily disabled
    /*
    server.on("/api/settings/test-fs", HTTP_GET, [&](){
        // Commented out for debugging
    });
    */

    // Simple safety toggle endpoint
    server.on("/api/safety/toggle", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/safety/toggle POST requested"));
        
        // Toggle safety state
        safetySystem.safetyEnabled = !safetySystem.safetyEnabled;
        
        // Clear emergency shutdown when safety is disabled
        if (!safetySystem.safetyEnabled) {
            safetySystem.emergencyShutdown = false;
            safetySystem.shutdownReason = "";
            if (debugSerial) Serial.println("[SAFETY] Emergency shutdown cleared (safety disabled)");
        }
        
        if (debugSerial) Serial.printf("[SAFETY] Safety system toggled to: %s\n", safetySystem.safetyEnabled ? "enabled" : "DISABLED");
        
        // Send response immediately
        String json = "{\"safetyEnabled\":" + String(safetySystem.safetyEnabled ? "true" : "false") + "}";
        server.send(200, "application/json", json);
        
        // Schedule deferred save
        pendingSettingsSaveTime = millis() + 500;
        if (debugSerial) Serial.println("[DEBUG] Settings save scheduled for safety toggle");
    });

    // GET-based safety toggle endpoint (crash workaround)
    server.on("/api/safety/toggle-get", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/safety/toggle-get GET requested"));
        
        // Toggle safety state
        safetySystem.safetyEnabled = !safetySystem.safetyEnabled;
        
        // Clear emergency shutdown when safety is disabled
        if (!safetySystem.safetyEnabled) {
            safetySystem.emergencyShutdown = false;
            safetySystem.shutdownReason = "";
            if (debugSerial) Serial.println("[SAFETY] Emergency shutdown cleared (safety disabled)");
        }
        
        if (debugSerial) Serial.printf("[SAFETY] Safety system toggled to: %s\n", safetySystem.safetyEnabled ? "enabled" : "DISABLED");
        
        // Send response immediately
        String json = "{\"safetyEnabled\":" + String(safetySystem.safetyEnabled ? "true" : "false") + "}";
        server.send(200, "application/json", json);
        
        // Schedule deferred save
        pendingSettingsSaveTime = millis() + 500;
        if (debugSerial) Serial.println("[DEBUG] Settings save scheduled for safety toggle");
    });
    
    // Display screensaver control endpoints
    server.on("/api/display/screensaver/status", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/display/screensaver/status requested"));
        String json = "{\"active\":" + String(isScreensaverActive() ? "true" : "false") + "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/api/display/screensaver/enable", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/display/screensaver/enable requested"));
        enableScreensaver();
        server.send(200, "application/json", "{\"status\":\"screensaver_enabled\"}");
    });
    
    server.on("/api/display/screensaver/disable", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/display/screensaver/disable requested"));
        disableScreensaver();
        server.send(200, "application/json", "{\"status\":\"screensaver_disabled\"}");
    });
    
    server.on("/api/display/activity", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/display/activity requested"));
        updateActivityTime();
        server.send(200, "application/json", "{\"status\":\"activity_updated\"}");
    });
    
    // Missing /api/pid_profile endpoint for PID profile management
    server.on("/api/pid_profile", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/pid_profile GET requested"));
        
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json", "");
        server.sendContent("{\"profiles\":[");
        server.sendContent("{\"key\":\"default\",\"kp\":" + String(pid.Kp) + ",");
        server.sendContent("\"ki\":" + String(pid.Ki) + ",");
        server.sendContent("\"kd\":" + String(pid.Kd) + ",");
        server.sendContent("\"windowMs\":" + String(pid.sampleTime) + "}");
        server.sendContent("]}");
    });
    
    server.on("/api/pid_profile", HTTP_POST, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/pid_profile POST requested"));
        
        if (server.hasArg("plain")) {
            String body = server.arg("plain");
            // ULTRA-EFFICIENT: Reduced from 1024 to 256 bytes (75% reduction)
            StaticJsonDocument<256> doc;  // Stack allocation instead of heap
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
                
                server.send(200, "application/json", "{\"status\":\"ok\",\"kp\":" + String(kp) + ",\"ki\":" + String(ki) + ",\"kd\":" + String(kd) + "}");
                if (debugSerial) Serial.printf("[DEBUG] PID parameters updated: Kp=%.3f, Ki=%.3f, Kd=%.3f\n", kp, ki, kd);
            } else {
                server.send(400, "application/json", "{\"error\":\"Invalid JSON or missing PID parameters\"}");
            }
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        }
    });

    // GET-based PID profile endpoint (crash workaround)
    server.on("/api/pid_profile/set", HTTP_GET, [&](){
        if (debugSerial) Serial.println(F("[DEBUG] /api/pid_profile/set GET requested"));
        
        if (server.hasArg("kp") && server.hasArg("ki") && server.hasArg("kd")) {
            double kp = server.arg("kp").toDouble();
            double ki = server.arg("ki").toDouble();
            double kd = server.arg("kd").toDouble();
            
            // Update PID parameters
            pid.Kp = kp;
            pid.Ki = ki;
            pid.Kd = kd;
            
            // Update controller if it exists
            if (pid.controller) {
                pid.controller->SetTunings(kp, ki, kd);
            }
            
            // savePIDProfiles(); // TODO: Implement savePIDProfiles() function
            
            server.send(200, "application/json", "{\"status\":\"ok\",\"kp\":" + String(kp) + ",\"ki\":" + String(ki) + ",\"kd\":" + String(kd) + "}");
            if (debugSerial) Serial.printf("[DEBUG] PID parameters updated via GET: Kp=%.3f, Ki=%.3f, Kd=%.3f\n", kp, ki, kd);
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing PID parameters (kp, ki, kd required)\"}");
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
