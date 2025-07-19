void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== MINIMAL ESP32 TEST ===");
  Serial.println("If you see this, the ESP32 is working!");
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Setup complete, entering loop...");
}

void loop() {
  Serial.println("Loop iteration - ESP32 is alive!");
  delay(2000);
}
