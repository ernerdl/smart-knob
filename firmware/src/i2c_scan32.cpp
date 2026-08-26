// Klasik ESP32 — I2C TARAYICI: birden cok pin ciftini dener, cihazlari bulur.
// MT6701 = 0x06 beklenir. GPIO 6-11 FLASH (denenmez), 34-39 sadece giris.
#include <Arduino.h>
#include <Wire.h>

struct PinPair { int sda, scl; };
static const PinPair PAIRS[] = {
  {21,22},{22,21},{18,19},{19,18},{32,33},{33,32},{25,26},{26,25},
  {4,5},{5,4},{13,14},{14,13},{23,19},{19,23},{16,17},{17,16},{27,14},{15,4}
};
static const int NP = sizeof(PAIRS)/sizeof(PAIRS[0]);

static void scanPair(int sda, int scl) {
  Wire.end(); delay(5);
  pinMode(sda, INPUT_PULLUP); pinMode(scl, INPUT_PULLUP);   // dahili pull-up: floating yanlis-pozitifleri keser
  delay(5);
  int sdaLvl = digitalRead(sda), sclLvl = digitalRead(scl);
  if (!Wire.begin(sda, scl, 100000)) { Serial.printf("SDA=%-2d SCL=%-2d : begin HATA\n", sda, scl); return; }
  delay(30);
  int found = 0; String list = "";
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { found++; list += " 0x"; list += String(a, HEX); }
  }
  if (found) Serial.printf("SDA=%-2d SCL=%-2d : %d cihaz ->%s   (bosta sda=%d scl=%d)  <<< BULUNDU\n", sda, scl, found, list.c_str(), sdaLvl, sclLvl);
  else       Serial.printf("SDA=%-2d SCL=%-2d : -   (bosta sda=%d scl=%d)\n", sda, scl, sdaLvl, sclLvl);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== I2C TARAYICI (klasik ESP32) — MT6701 icin 0x06 araniyor ===");
  for (int i = 0; i < NP; i++) scanPair(PAIRS[i].sda, PAIRS[i].scl);
  Serial.println("=== tarama bitti (tekrar icin 'r') ===");
}
void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    Serial.println("--- tekrar ---");
    for (int i = 0; i < NP; i++) scanPair(PAIRS[i].sda, PAIRS[i].scl);
    Serial.println("--- bitti ---");
  }
  delay(50);
}
