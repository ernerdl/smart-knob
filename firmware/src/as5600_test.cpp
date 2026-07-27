// SmartKnob — AS5600 okuma testi (saf I2C, SimpleFOC yok)
// Mıknatıs algılandı mı + AGC (mesafe) + ham açı gösterir.
// SADECE `pio run -e as5600test` ile derlenir.
//
// Baglanti: VCC->3V3 (5V DEGIL!), GND->GND, SDA->D4/GPIO5, SCL->D5/GPIO6, DIR->GND
// I2C adres 0x36.

#include <Arduino.h>
#include <Wire.h>

#define AS5600_ADDR 0x36
#define PIN_SDA 5   // D4
#define PIN_SCL 6   // D5

uint8_t rd8(uint8_t reg) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom((int)AS5600_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

uint16_t rd12(uint8_t regHi) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(regHi);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  Wire.requestFrom((int)AS5600_ADDR, 2);
  if (Wire.available() < 2) return 0xFFFF;
  uint8_t hi = Wire.read(), lo = Wire.read();
  return ((hi << 8) | lo) & 0x0FFF;
}

void scan() {
  Serial.print("I2C tarama: ");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("0x%02X ", a); n++; }
  }
  Serial.println(n ? "" : "(cihaz yok!)");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("SmartKnob — AS5600 okuma testi");
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  delay(100);
  scan();
  Serial.println("STATUS: MD=miknatis algilandi | ML=cok zayif(uzak) | MH=cok guclu(yakin)");
  Serial.println("AGC (3.3V'ta 0-128): ORTA (~64) ideal. Dusuk=cok yakin, Yuksek=cok uzak.");
}

void loop() {
  uint8_t st = rd8(0x0B);            // STATUS
  uint8_t agc = rd8(0x1A);           // AGC
  uint16_t raw = rd12(0x0C);         // RAW ANGLE (12-bit)

  bool MD = st & (1 << 5);
  bool ML = st & (1 << 4);
  bool MH = st & (1 << 3);
  float deg = (raw == 0xFFFF) ? -1 : raw * 360.0f / 4096.0f;

  if (st == 0xFF) {
    Serial.println("HATA: AS5600 okunamadi (baglanti/3V3/pull-up kontrol)");
  } else {
    Serial.printf("MD=%d ML=%d MH=%d | AGC=%3d | aci=%.1f deg  %s\n",
                  MD, ML, MH, agc, deg,
                  MH ? "-> COK YAKIN" : ML ? "-> COK UZAK" : MD ? "-> IYI" : "-> MIKNATIS YOK");
  }
  delay(300);
}
