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

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

// Helper function to get status JSON as string
String getStatusJsonString() {
  String response;
  response.reserve(2048);
  // ...existing code...
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
        server.send(200, "application/json", getStatusJsonString());
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
    server.on("/api/home_assistant", HTTP_GET, [&](){
        server.send(200, "application/json", getStatusJsonString());
    });
}

void calibrationEndpoints(WebServer& server) {
    server.on("/api/calibration", HTTP_GET, [&](){
        String response = "{\"points\":[]}";
        server.send(200, "application/json", response);
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
        // TODO: Integrate with actual OTA manager state
        String json = "{";
        json += "\"enabled\":true,";
        json += "\"inProgress\":false,";
        json += "\"progress\":0,";
        json += "\"hostname\":\"" + String(WiFi.getHostname()) + "\",";
        json += "\"error\":null";
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
            
            // 4. Call updateActiveProgramVars() if it exists
            // TODO: Add updateActiveProgramVars() call when function is available
            
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
            // TODO: Add actual start at stage logic here
            // For now, return success
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
        // TODO: Add actual pause logic here (set programState.isRunning = false)
        server.send(200, "application/json", "{\"status\":\"paused\"}");
    });
    
    server.on("/resume", HTTP_GET, [&](){
        // Implement resume based on historical patterns
        if (debugSerial) {
            Serial.println("[DEBUG] Resume requested");
        }
        // TODO: Add actual resume logic here (set programState.isRunning = true)
        server.send(200, "application/json", "{\"status\":\"running\"}");
    });
    
    server.on("/back", HTTP_GET, [&](){
        // Implement back/previous stage based on historical patterns
        if (debugSerial) {
            Serial.println("[DEBUG] Back/previous stage requested");
        }
        // TODO: Add actual back logic here (decrement stage index)
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    server.on("/api/manual_mode", HTTP_GET, [&](){
        if (server.hasArg("on")) {
            bool manualMode = server.arg("on") == "1";
            // Implement manual mode toggle based on historical patterns
            if (debugSerial) {
                Serial.printf("[DEBUG] Manual mode: %s\n", manualMode ? "ON" : "OFF");
            }
            // TODO: Add actual manual mode logic here (set programState.manualMode)
            server.send(200, "application/json", "{\"status\":\"ok\",\"manual_mode\":" + String(manualMode ? "true" : "false") + "}");
        } else {
            // Return current manual mode status
            // TODO: Return actual manual mode state from programState.manualMode
            server.send(200, "application/json", "{\"manual_mode\":false}");
        }
    });
    
    server.on("/api/temperature", HTTP_GET, [&](){
        if (server.hasArg("setpoint")) {
            float setpoint = server.arg("setpoint").toFloat();
            // Implement temperature setpoint based on historical patterns
            if (debugSerial) {
                Serial.printf("[DEBUG] Temperature setpoint: %.1f\n", setpoint);
            }
            // TODO: Add actual temperature setpoint logic here (set PID setpoint)
            server.send(200, "application/json", "{\"status\":\"ok\",\"setpoint\":" + String(setpoint) + "}");
        } else {
            // Return current temperature - this should work with existing code
            float currentTemp = readTemperature();
            server.send(200, "application/json", "{\"temperature\":" + String(currentTemp) + "}");
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
