// SmartKnob — I2C tarayici (iki bus): bus0=GPIO5/6, bus1=GPIO10/11
// SADECE `pio run -e i2cscan`. UART serial.
// bus0: AS5600/MT6701 (0x36 / 0x06)  |  bus1: PCF8574 (0x20)

#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

static void scan(TwoWire &w, const char *name) {
  Serial.printf("[%s] ", name);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) { Serial.printf("0x%02X ", a); n++; }
  }
  Serial.println(n ? "" : "(cihaz yok)");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== I2C TARAYICI (bus0=5/6, bus1=10/11) ===");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  Wire1.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, 100000);
}

static int mt6701raw() {   // MT6701 14-bit ham aci (0x06)
  Wire.beginTransmission(0x06);
  if (Wire.endTransmission() != 0) return -1;
  Wire.beginTransmission(0x06); Wire.write((uint8_t)0x03); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)0x06, (uint8_t)2);
  if (Wire.available() < 2) return -1;
  uint16_t hi = Wire.read(), lo = Wire.read();
  return (hi << 6) | (lo >> 2);
}

void loop() {
  scan(Wire,  "bus0 5/6   sensor");
  scan(Wire1, "bus1 10/11 PCF(0x20)");
  int raw = mt6701raw();
  if (raw < 0) Serial.println("MT6701: CEVAP YOK (tel/guc)");
  else Serial.printf("MT6701 aci: %d (%.1f deg) <- cevir, degisiyor mu?\n", raw, raw*360.0f/16384.0f);
  Serial.println();
  delay(700);
}
