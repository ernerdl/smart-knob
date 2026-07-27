// SmartKnob — I2C OLED testi (1.3" SH1106 / SSD1306)
// SADECE `pio run -e screentest`.
// Baglanti: VCC->3V3, GND->GND, SDA->GPIO10, SCL->GPIO11 (I2C1)
// 1.3" genelde SH1106. Ekran kaymis/bozuksa SSD1306 satirina gec.

#include <Arduino.h>
#include <U8g2lib.h>
#include "pins.h"

// SW I2C (bit-bang) — istedigimiz pinlerde calisir (SCL=11, SDA=10)
U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, PIN_I2C1_SCL, PIN_I2C1_SDA, U8X8_PIN_NONE);
// SSD1306 ise: U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, PIN_I2C1_SCL, PIN_I2C1_SDA, U8X8_PIN_NONE);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("OLED testi (SH1106 128x64)");
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(12, 30, "SmartKnob");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(20, 50, "OLED calisiyor");
  u8g2.sendBuffer();
  Serial.println("cizildi");
}

void loop() {
  // basit animasyon (ekran canli mi): dolan bar
  static int x = 0;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(12, 26, "SmartKnob");
  u8g2.drawFrame(10, 40, 108, 14);
  u8g2.drawBox(12, 42, x, 10);
  u8g2.sendBuffer();
  x = (x + 4) % 106;
  delay(60);
}
