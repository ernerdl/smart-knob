// SmartKnob — NeoPixel ring testi (16 LED, GPIO14)
// SADECE `pio run -e neopixeltest`.
// Baglanti: 5V, GND, DIN->GPIO14. Parlaklik DUSUK (USB akimi guvenli).

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "pins.h"

#define NUM 16
Adafruit_NeoPixel ring(NUM, PIN_RGB, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel onboard(1, 48, NEO_GRB + NEO_KHZ800);  // DevKit onboard WS2812 (izole test)

// baglam renkleri (Mac tarafi profil renkleri gibi)
static const uint32_t ctxColors[] = {
  0x00FF88, // yesil (Chrome/tabs)
  0x0088FF, // mavi (Safari)
  0xFF8800, // turuncu (ses)
  0xFF0044, // kirmizi (video)
  0xFFFFFF, // beyaz (varsayilan)
};

void wheel(uint16_t off) {           // gokkusagi
  for (int i = 0; i < NUM; i++) {
    uint16_t h = (i * 65536L / NUM + off) & 0xFFFF;
    ring.setPixelColor(i, ring.gamma32(ring.ColorHSV(h)));
  }
  ring.show();
}

void solid(uint32_t c) {
  for (int i = 0; i < NUM; i++) ring.setPixelColor(i, c);
  ring.show();
  onboard.setPixelColor(0, c); onboard.show();   // ayni rengi onboard'a da
}

void spin(uint32_t c, int pos) {     // tek nokta doner (bag. gostergesi gibi)
  ring.clear();
  ring.setPixelColor(pos % NUM, c);
  ring.setPixelColor((pos + 1) % NUM, c);
  ring.show();
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("SmartKnob — NeoPixel 16 ring testi (GPIO14)");
  ring.begin();
  ring.setBrightness(40);            // DUSUK (USB guvenli) — full beyaz ~0.15A
  ring.clear(); ring.show();
  onboard.begin(); onboard.setBrightness(40); onboard.clear(); onboard.show();
  Serial.println("baslatildi, parlaklik=40/255");
}

void loop() {
  // 1) gokkusagi supurme (2 tur)
  Serial.println(">> gokkusagi");
  for (int k = 0; k < 2 * 65536; k += 512) { wheel(k); delay(3); }

  // 2) baglam renkleri sirayla
  Serial.println(">> baglam renkleri");
  for (uint32_t c : ctxColors) { solid(c); delay(700); }

  // 3) donen nokta (renk gostergesi)
  Serial.println(">> donen nokta");
  for (int p = 0; p < NUM * 2; p++) { spin(0x00FF88, p); delay(60); }

  ring.clear(); ring.show(); delay(300);
}
