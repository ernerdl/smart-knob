// SmartKnob — ST7789 2.0" 240x320 SPI ekran testi (Adafruit ST7789 — S3'te saglam)
// SADECE `pio run -e tfttest`.
// Baglanti: CS=42 DC=41 RST=40 SDA(MOSI)=39 SCL(SCK)=38 | VCC=3V3 GND=GND

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define TFT_CS   42
#define TFT_DC   41
#define TFT_RST  40
#define TFT_MOSI 39
#define TFT_SCLK 38

SPIClass tftSPI(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("ST7789 (Adafruit) basliyor (CS42 DC41 RST40 SDA39 SCL38)...");

  tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);   // SCLK, MISO(-1), MOSI, SS
  tft.init(240, 320);                              // ST7789 240x320
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(15, 30);
  tft.println("SmartKnob");

  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(15, 80);
  tft.println("ST7789 OK");

  // renk barlari (dogru renkler cikiyorsa renk sirasi tamam)
  tft.fillRect(0, 120, 240, 30, ST77XX_RED);
  tft.fillRect(0, 150, 240, 30, ST77XX_GREEN);
  tft.fillRect(0, 180, 240, 30, ST77XX_BLUE);
  tft.fillRect(0, 210, 240, 30, ST77XX_YELLOW);

  Serial.println("cizildi — ekrana bak");
}

void loop() {
  // canli animasyon: dolan cyan bar (ekran diri mi)
  static int x = 0;
  if (x == 0) tft.fillRect(10, 270, 220, 24, ST77XX_BLACK);
  tft.fillRect(10, 270, x, 24, ST77XX_CYAN);
  x = (x + 8) % 220;
  delay(70);
}
