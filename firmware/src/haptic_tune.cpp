// SmartKnob — HAPTIK AYAR TEZGAHI  (pio run -e haptictune)
//
// AMAC: centik aralagi/sertligi/sonumu ELLE ve HIZLI denemek. Uretim firmware'ine
// (foc_haptic.cpp) test iskelesi koymamak icin ayri tutuldu.
//
// FARKLAR (uretim firmware'ine gore):
//   - HID YOK  : hicbir tus/ses/masaustu komutu gitmez, Mac'e dokunmaz
//   - MOD YOK  : tek bir sonsuz centik gridi; duvar/ortalama/deploy yok
//   - HS YOK   : Hammerspoon ile konusmaz
//   - Serial = native USB CDC (ayni port), ayrica PCF8574 butonlariyla canli ayar
//
// BUTONLAR (PCF8574 0x20, bit sirasi uretimdeki ile ayni fiziksel duzen):
//   bit7 / bit6 : centik SAYISI  -  / +      (aralik = kullanilabilir / (sayi-1))
//   bit5 / bit4 : SERTLIK Kdet   -  / +
//   bit3 / bit2 : SONUM damping  -  / +
//   bit1        : A/B  -> iki kayitli ayar arasinda gecis
//   bit0        : mevcut ayari DIGER slota kaydet
//
// SERI KOMUTLAR:
//   d <n>    centik sayisi        k <v>  Kdet        b <v>  damping
//   v <volt> voltage_limit        p <0|1> profil (0 testere, 1 sinus)
//   A        A/B slot degistir    S      mevcut ayari diger slota yaz
//   ?        durum
//
// GUVENLIK: kalibrasyon uretimdekiyle AYNI sabit (offset 1.308 / CW). Braket
// sokulurse ikisi birden gecersiz olur -> 'P 7' ile uretim firmware'inde yenile.

#include <Arduino.h>
#include <SimpleFOC.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "MagneticSensorMT6701SSI.h"
#include "pins.h"

#define MOTOR_POLE_PAIRS 7
#define MOTOR_SUPPLY_V   12.0f

SPIClass tftSPI(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
SPIClass sensorSPI(FSPI);
MagneticSensorMT6701SSI sensor;
BLDCMotor      motor  = BLDCMotor(MOTOR_POLE_PAIRS);
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C);

// --- ayar slotlari (A/B) ---
struct Cfg { int detents; float kdet; float damping; int profile; };
static Cfg slot[2] = { {17, 22.0f, 0.05f, 0}, {50, 30.0f, 0.05f, 0} };
static int  cur = 0;
#define C slot[cur]

// olculmus duvarlar (uretim firmware'i ile ayni)
static const float minA = 6.7f   * PI / 180.0f;
static const float maxA = 326.3f * PI / 180.0f;
static const float wallMargin = 0.14f;

static float relAnchor = 0, hapCenter = 0;
static bool  pcfPresent = false;
static float lastT = 0;

static float spacing() {
  float usable = (maxA - wallMargin) - (minA + wallMargin);
  return (C.detents > 1) ? usable / (C.detents - 1) : 0.26f;
}
static float peakV() { return C.kdet * spacing() / 2.0f; }

// --- haptik tork: sonsuz testere VEYA sinus gridi, duvar yok ---
static float hapticTorque(float a) {
  float sp = spacing();
  if (sp <= 0) return 0;
  if (fabsf(a - hapCenter) > 3.0f * sp)
    hapCenter = relAnchor + roundf((a - relAnchor) / sp) * sp;
  float thr = sp * 0.5f;
  float e = a - hapCenter;
  while (e >  thr) { hapCenter += sp; e -= sp; }
  while (e < -thr) { hapCenter -= sp; e += sp; }
  if (C.profile == 1) {                       // sinus: surekli, sinirda sicrama yok
    return -C.kdet * (sp / PI) * sinf(PI * e / (sp * 0.5f) * 0.5f);
  }
  return -C.kdet * e;                         // testere
}

// --- ekran ---
static void drawScreen() {
  char b[40];
  tft.fillScreen(ST77XX_BLACK);
  uint16_t accent = (cur == 0) ? tft.color565(80,160,255) : tft.color565(255,120,80);
  tft.setTextColor(accent); tft.setTextSize(3);
  tft.setCursor(14, 12); tft.print(cur == 0 ? "AYAR A" : "AYAR B");

  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(5);
  snprintf(b, sizeof b, "%d", C.detents);
  tft.setCursor(14, 52); tft.print(b);
  tft.setTextSize(2); tft.setCursor(14, 100); tft.print("centik");

  tft.setTextSize(3); tft.setTextColor(tft.color565(200,200,200));
  snprintf(b, sizeof b, "%.2f deg", spacing() * 180.0f / PI);
  tft.setCursor(150, 60); tft.print(b);

  tft.setTextSize(3); tft.setTextColor(accent);
  snprintf(b, sizeof b, "K %.0f", C.kdet);        tft.setCursor(14, 130); tft.print(b);
  snprintf(b, sizeof b, "B %.2f", C.damping);     tft.setCursor(150, 130); tft.print(b);

  tft.setTextSize(3); tft.setTextColor(ST77XX_WHITE);
  snprintf(b, sizeof b, "tepe %.2f V", peakV());
  tft.setCursor(14, 172); tft.print(b);

  tft.setTextSize(2); tft.setTextColor(tft.color565(120,120,120));
  snprintf(b, sizeof b, "profil: %s", C.profile ? "sinus" : "testere");
  tft.setCursor(14, 212); tft.print(b);
}
static volatile bool needDraw = true;
static void displayTask(void*) {
  for (;;) { if (needDraw) { needDraw = false; drawScreen(); } vTaskDelay(40 / portTICK_PERIOD_MS); }
}

// --- butonlar ---
static const int DET_LIST[] = {17, 25, 34, 50, 76, 100, 120, 150, 200};
static const int DET_N = sizeof(DET_LIST)/sizeof(DET_LIST[0]);
static void stepDetents(int dir) {
  int i = 0, best = 1e9;
  for (int k = 0; k < DET_N; k++) { int d = abs(DET_LIST[k] - C.detents); if (d < best) { best = d; i = k; } }
  i = constrain(i + dir, 0, DET_N - 1);
  C.detents = DET_LIST[i];
}
static void onButton(int bit) {
  switch (bit) {
    case 7: stepDetents(-1); break;
    case 6: stepDetents(+1); break;
    case 5: C.kdet    = _constrain(C.kdet    - 4.0f,  2.0f, 60.0f); break;
    case 4: C.kdet    = _constrain(C.kdet    + 4.0f,  2.0f, 60.0f); break;
    case 3: C.damping = _constrain(C.damping - 0.05f, 0.0f,  1.0f); break;
    case 2: C.damping = _constrain(C.damping + 0.05f, 0.0f,  1.0f); break;
    case 1: cur = 1 - cur; break;                       // A/B gecis
    case 0: slot[1 - cur] = slot[cur]; break;           // diger slota kopyala
  }
  relAnchor = motor.shaft_angle; hapCenter = relAnchor;
  needDraw = true;
  Serial.printf("[%c] centik=%d aralik=%.2fdeg K=%.0f B=%.2f tepe=%.2fV profil=%d\n",
                cur ? 'B' : 'A', C.detents, spacing()*180.0f/PI, C.kdet, C.damping, peakV(), C.profile);
}

static void handleLine(String s) {
  s.trim(); if (!s.length()) return;
  char c = s[0]; String v = s.substring(1); v.trim();
  if      (c=='d') { C.detents = max(2, (int)v.toInt()); }
  else if (c=='k') { C.kdet    = v.toFloat(); }
  else if (c=='b') { C.damping = v.toFloat(); }
  else if (c=='p') { C.profile = v.toInt() ? 1 : 0; }
  else if (c=='v') { motor.voltage_limit = v.toFloat(); }
  else if (c=='A') { cur = 1 - cur; }
  else if (c=='S') { slot[1-cur] = slot[cur]; }
  else if (c=='?') { }
  else { Serial.printf("bilinmeyen: %s\n", s.c_str()); return; }
  relAnchor = motor.shaft_angle; hapCenter = relAnchor; needDraw = true;
  Serial.printf("[%c] centik=%d aralik=%.2fdeg K=%.0f B=%.2f tepe=%.2fV profil=%d vlim=%.1f aci=%.1f\n",
                cur ? 'B' : 'A', C.detents, spacing()*180.0f/PI, C.kdet, C.damping, peakV(),
                C.profile, motor.voltage_limit, motor.shaft_angle*180.0f/PI);
}

void setup() {
  pinMode(PIN_MOTOR_A, OUTPUT); digitalWrite(PIN_MOTOR_A, LOW);
  pinMode(PIN_MOTOR_B, OUTPUT); digitalWrite(PIN_MOTOR_B, LOW);
  pinMode(PIN_MOTOR_C, OUTPUT); digitalWrite(PIN_MOTOR_C, LOW);
  Serial.begin(115200); delay(800);
  Serial.println("\nSmartKnob — HAPTIK AYAR TEZGAHI (HID YOK)");

  sensorSPI.begin(PIN_I2C_SCL, PIN_I2C_SDA, -1, PIN_MT_CSN);
  sensor.init(&sensorSPI);
  motor.linkSensor(&sensor);

  driver.voltage_power_supply = MOTOR_SUPPLY_V;
  driver.voltage_limit        = 8.0f;
  driver.pwm_frequency        = 20000;
  if (!driver.init()) { Serial.println("HATA driver.init"); return; }
  motor.linkDriver(&driver);

  motor.controller           = MotionControlType::torque;
  motor.voltage_limit        = 5.0f;
  motor.voltage_sensor_align = 1.0f;
  motor.velocity_limit       = 100;
  sensor.min_elapsed_time    = 0.0005f;
  motor.LPF_velocity.Tf      = 0.01f;
  motor.zero_electric_angle  = 1.308f;            // uretim firmware'i ile AYNI
  motor.sensor_direction     = Direction::CW;
  motor.init();
  motor.initFOC();
  Serial.printf("initFOC bitti. offset=%.3f dir=%d\n", motor.zero_electric_angle, motor.sensor_direction);

  Wire1.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, 100000);
  Wire1.beginTransmission(0x20);
  pcfPresent = (Wire1.endTransmission() == 0);
  if (pcfPresent) { Wire1.beginTransmission(0x20); Wire1.write(0xFF); Wire1.endTransmission(); }
  Serial.printf("PCF8574: %s\n", pcfPresent ? "VAR" : "yok");

  tftSPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240, 320); tft.setRotation(1);
  xTaskCreatePinnedToCore(displayTask, "disp", 4096, NULL, 1, NULL, 0);

  relAnchor = motor.shaft_angle; hapCenter = relAnchor;
  Serial.printf("hazir. centik=%d aralik=%.2fdeg K=%.0f tepe=%.2fV\n",
                C.detents, spacing()*180.0f/PI, C.kdet, peakV());
}

void loop() {
  motor.loopFOC();
  float t = hapticTorque(motor.shaft_angle);
  t -= C.damping * motor.shaft_velocity;
  if (fabsf(motor.shaft_velocity) > 40.0f) t = 0;      // runaway kesme
  lastT = _constrain(t, -motor.voltage_limit, motor.voltage_limit);
  motor.move(lastT);

  static uint32_t lastPoll = 0; static uint8_t prev = 0xFF;
  if (pcfPresent && millis() - lastPoll > 40) {
    lastPoll = millis();
    Wire1.requestFrom(0x20, 1);
    if (Wire1.available()) {
      uint8_t cur8 = Wire1.read();
      uint8_t fell = (prev ^ cur8) & prev;             // 1->0 kenari (butona basildi)
      for (int b = 0; b < 8; b++) if (fell & (1 << b)) onButton(b);
      prev = cur8;
    }
  }

  static String rx;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch=='\n'||ch=='\r') { handleLine(rx); rx=""; }
    else { rx += ch; if (rx.length() > 40) rx = ""; }
  }
}
