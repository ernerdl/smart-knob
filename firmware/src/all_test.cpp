// SmartKnob — TOPLU TEST (sensor haric hepsi): ekran + LED ring + motor(open-loop) + PCF buton
// SADECE `pio run -e alltest`. Sensor yok -> motor ACIK CEVRIM (nazik gidip-gelme).
// MT6701 gelince gercek FOC foc env'inde.

#include <Arduino.h>
#include <SimpleFOC.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#define FASTLED_INTERNAL
#include <FastLED.h>
#include <Wire.h>
#include "pins.h"

#define NLED 16
CRGB leds[NLED];
SPIClass tftSPI(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C);
BLDCMotor      motor  = BLDCMotor(MOTOR_POLE_PAIRS);

void setup() {
  Serial.begin(115200);
  delay(300);

  FastLED.addLeds<WS2812B, PIN_RGB, GRB>(leds, NLED);
  FastLED.setBrightness(40);
  fill_solid(leds, NLED, CRGB(20,20,20)); FastLED.show();

  Wire1.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, 100000);          // PCF
  Wire1.beginTransmission(0x20); Wire1.write(0xFF); Wire1.endTransmission();

  tftSPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240,320); tft.setRotation(0); tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3);
  tft.setCursor(15,20); tft.print("TOPLU TEST");

  driver.voltage_power_supply = MOTOR_SUPPLY_V;             // 12V
  driver.voltage_limit = 6.0f;
  driver.init();
  motor.linkDriver(&driver);
  motor.controller    = MotionControlType::velocity_openloop; // SENSORSUZ
  motor.voltage_limit = 3.0f;                                 // nazik
  motor.init();

  Serial.println("TOPLU TEST basladi (ekran+led+motor+pcf)");
}

void loop() {
  static uint8_t  hue = 0;
  static uint32_t lastDir = 0, lastDraw = 0;
  static float    vel = 2.5f;

  // motor: nazik gidip-gelme (open-loop, ~1.8sn'de yon degistir)
  if (millis() - lastDir > 1800) { lastDir = millis(); vel = -vel; }
  motor.move(vel);

  EVERY_N_MILLISECONDS(30) { fill_rainbow(leds, NLED, hue++, 16); FastLED.show(); }

  // PCF + ekran (250ms)
  if (millis() - lastDraw > 250) {
    lastDraw = millis();
    uint8_t b = 0xFF;
    if (Wire1.requestFrom((uint8_t)0x20,(uint8_t)1)) { if (Wire1.available()) b = Wire1.read(); }

    tft.fillRect(0, 80, 240, 170, ST77XX_BLACK);
    tft.setTextSize(2); tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(15, 95);  tft.print("Motor: ");  tft.print(vel > 0 ? "ileri" : "geri ");
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(15, 135); tft.print("Butonlar (P7..P0):");
    tft.setTextSize(3); tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(15, 165);
    char bits[9]; for (int i=0;i<8;i++) bits[i] = ((b>>(7-i))&1) ? '1':'0'; bits[8]=0;
    tft.print(bits);   // basili buton = 0
    // LED: buton basiliysa o rengi vurgula degil; rainbow yeter
    Serial.printf("PCF=0x%02X motor=%s\n", b, vel>0?"ileri":"geri");
  }
}
