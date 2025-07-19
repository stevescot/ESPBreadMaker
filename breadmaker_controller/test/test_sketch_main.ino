void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 Test - Hello World!");
  Serial.println("If you can see this, serial communication is working.");
  pinMode(2, OUTPUT); // Built-in LED on most ESP32 boards
}

void loop() {
  Serial.println("Loop running...");
  digitalWrite(2, HIGH);
  delay(1000);
  digitalWrite(2, LOW);
  delay(1000);
}
