// SmartKnob — Ekran (ST7789) + LED ring (WS2812) BIRLIKTE testi
// SADECE `pio run -e tftledtest`.
// Ekran: CS42 DC41 RST40 SDA39 SCL38 | Ring: DIN=GPIO4 (16 LED) | ikisi 3V3/5V + ortak GND

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#define FASTLED_INTERNAL
#include <FastLED.h>
#include "pins.h"

#define NLED 16
CRGB leds[NLED];

SPIClass tftSPI(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(300);

  // LED ring (FastLED, RMT — SPI'dan bagimsiz)
  FastLED.addLeds<WS2812B, PIN_RGB, GRB>(leds, NLED);
  FastLED.setBrightness(40);
  fill_solid(leds, NLED, CRGB(20, 20, 20)); FastLED.show();

  // Ekran (Adafruit SPI)
  tftSPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3);
  tft.setCursor(15, 30); tft.println("SmartKnob");
  tft.setTextColor(ST77XX_CYAN); tft.setTextSize(2);
  tft.setCursor(15, 85); tft.println("Ekran + LED");

  Serial.println("ekran+led birlikte basladi");
}

void loop() {
  static uint8_t hue = 0;

  // LED ring: donen gokkusagi
  fill_rainbow(leds, NLED, hue, 256 / NLED);
  FastLED.show();

  // ekran: ring rengiyle senkron dolan bar + renk kutusu
  CRGB c = CHSV(hue, 255, 255);
  uint16_t c565 = tft.color565(c.r, c.g, c.b);
  tft.fillRect(20, 140, 200, 60, c565);
  static int x = 0;
  if (x == 0) tft.fillRect(10, 250, 220, 24, ST77XX_BLACK);
  tft.fillRect(10, 250, x, 24, c565);
  x = (x + 10) % 220;

  hue += 3;
  delay(40);
}
