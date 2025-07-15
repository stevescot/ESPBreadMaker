#include "wifi_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

const byte DNS_PORT = 53;
const char* WIFI_FILE = "/wifi.json";
extern DNSServer dnsServer;
extern AsyncWebServer server;
extern const char* apSSID;

bool loadWiFiCreds(String &ssid, String &pass) {
  File f = LittleFS.open(WIFI_FILE, "r");
  if (!f) return false;
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  if (!doc.containsKey("ssid") || !doc.containsKey("pass")) return false;
  ssid = String((const char*)doc["ssid"]);
  pass = String((const char*)doc["pass"]);
  return ssid.length() > 0;
}

void startCaptivePortal() {
  WiFi.mode(WIFI_AP_STA); // Enable both AP and STA
  WiFi.softAP(apSSID);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    options += "<option value='" + ssid + "'>" + ssid + "</option>";
  }

  String page;
  File f = LittleFS.open("/wifi.html", "r");
  if (f) {
    while (f.available()) page += (char)f.read();
    f.close();
    page.replace("{{options}}", options);
  } else {
    page = "<h2>WiFi Setup</h2><form method='POST' action='/save'><label>SSID: <select name='ssid'>" + options + "</select></label><br><label>Password: <input name='pass' type='password' placeholder='Password'></label><br><button>Save</button></form>";
  }

  auto sendPortal = [page](AsyncWebServerRequest* req) {
    req->send(200, "text/html", page);
  };

  server.on("/", HTTP_GET, sendPortal);
  server.onNotFound(sendPortal);

  // Captive portal detection endpoints (only needed in AP mode)
  const char* captiveEndpoints[] = {
    "/generate_204", "/fwlink", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt", "/wpad.dat"
  };
  for (auto ep : captiveEndpoints) {
    server.on(ep, HTTP_GET, sendPortal);
  }

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* req){
    String ssid = req->arg("ssid");
    String pass = req->arg("pass");
    DynamicJsonDocument doc(256);
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    File f = LittleFS.open(WIFI_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
    req->send(200, "text/plain", "Saved! Connecting to WiFi...\nThe device will reboot and disconnect from this network.\nYou may need to reconnect to the new WiFi.");
    delay(1000);
    ESP.restart();
  });
  server.begin();
}

void cleanupCaptivePortalEndpoints() {
  // Note: ESP8266 AsyncWebServer doesn't have a direct way to remove endpoints
  // The endpoints will remain registered but inactive after WiFi connection
  // This is a limitation of the current library
  // TODO: Consider switching to a different web server library that supports endpoint removal
  Serial.println("[WiFi] Captive portal endpoints remain registered but inactive");
}
