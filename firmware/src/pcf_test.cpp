// SmartKnob — PCF8574 (I2C1) + I2C tarama testi
// SADECE `pio run -e pcftest`.
// I2C0 (Wire, GPIO5/6) = AS5600 | I2C1 (Wire1, GPIO10/11) = PCF8574
// Buton: P pini <-> GND. Basili=0, serbest=1 (dahili pull-up).

#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

#define PCF_ADDR 0x20

static void scan(TwoWire &w, const char* name) {
  Serial.printf("%s tarama: ", name);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) { Serial.printf("0x%02X ", a); n++; }
  }
  Serial.println(n ? "" : "(cihaz yok!)");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("SmartKnob — I2C tarama + PCF8574 buton testi");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);        // I2C0 AS5600
  Wire1.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, 100000);     // I2C1 PCF8574
  delay(100);
  scan(Wire,  "I2C0 (AS5600 0x36)");
  scan(Wire1, "I2C1 (PCF8574 0x20)");
  // PCF8574 girisleri HIGH yap (input + pull-up)
  Wire1.beginTransmission(PCF_ADDR); Wire1.write(0xFF); Wire1.endTransmission();
  Serial.println("Butonlara bas -> ilgili bit 0 olur");
}

void loop() {
  Wire1.requestFrom(PCF_ADDR, 1);
  if (Wire1.available()) {
    uint8_t b = Wire1.read();
    Serial.printf("P0-P7: ");
    for (int i = 0; i < 8; i++) Serial.printf("%d", (b >> i) & 1);
    Serial.printf("   (0x%02X)%s\n", b, (b != 0xFF) ? "  <-- BASILI" : "");
  } else {
    Serial.println("PCF8574 okunamadi (baglanti/adres/3V3)");
  }
  delay(250);
}
