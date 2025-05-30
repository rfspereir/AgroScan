#include <Arduino.h>

#define RXp2 16  // GPIO16 (RX) conectado ao TX da CAM
#define TXp2 17  // GPIO17 (TX) conectado ao RX da CAM

void setup() {
  pinMode(0, INPUT_PULLUP);  // Detecta se GPIO0 está GND

  if (digitalRead(0) == LOW) {
    // GPIO0 está GND => impedir upload neste chip
    while (true) {
      delay(100);
    }
  }

  Serial.begin(115200);                           // USB do DevKit
  Serial2.begin(115200, SERIAL_8N1, RXp2, TXp2);   // Ponte com ESP32-CAM
  Serial.println(">> Bridge ativa. Monitorando serial do ESP32-CAM");
}

void loop() {
  while (Serial.available()) {
    Serial2.write(Serial.read());
  }
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}
