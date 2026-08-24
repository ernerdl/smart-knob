// SmartKnob — ASAMA 1: OPEN-LOOP velocity testi (sensORSUZ)
// Amac: motor + L298N + baglanti dogru mu? Motor canlaniyor mu?
// Sensor YOK, kalibrasyon YOK. Sadece 3 fazi surup donmeye calismasini gormek.
//
// GUVENLIK: voltage_limit = 2V ile baslar (2V / 6ohm = 0.33A). Seri porttan
// kademeli artirilir. Motor sinirli donuslu (~300°); duvara vurunca open-loop'ta
// stall olur (visildar) — dusuk voltajda GUVENLI ama uzun sure duvarda BEKLETME.
//
// Bu dosya sadece `pio run -e bringup` ile derlenir (bkz platformio.ini).
//
// Komutlar (Serial Monitor / CDC):
//   t <rad/s>   hedef hiz ( or: t 5  /  t -5 ters yon  /  t 0 dur)
//   v <volt>    voltage_limit degistir (2 -> 3 -> 4 kademeli, ISIYI IZLE)
//   s           stop (hedef 0)
//   ?           durum

#include <Arduino.h>
#include <SimpleFOC.h>
#include "pins.h"

BLDCMotor      motor  = BLDCMotor(MOTOR_POLE_PAIRS);
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C);

float target = 0.0f;   // rad/s

static void printStatus() {
  float vlim = motor.voltage_limit;
  Serial.printf("STATUS target=%.2f rad/s  voltage_limit=%.2fV  ~akim=%.2fA (6ohm)\n",
                target, vlim, vlim / 6.0f);
}

static String rx;
static void handleLine(String s) {
  s.trim();
  if (s.length() == 0) return;
  if (s == "s") { target = 0; Serial.println("STOP"); printStatus(); return; }
  if (s == "?") { printStatus(); return; }
  if (s.startsWith("t")) {
    target = s.substring(1).toFloat();
    Serial.printf("hedef -> %.2f rad/s\n", target);
    return;
  }
  if (s.startsWith("v")) {
    float v = s.substring(1).toFloat();
    if (v < 0.5f) v = 0.5f;
    if (v > 6.0f) { v = 6.0f; Serial.println("!! 6V ustune cikma (bu testte)"); }
    motor.voltage_limit = v;
    Serial.printf("voltage_limit -> %.2fV (~%.2fA)  ISIYI IZLE\n", v, v / 6.0f);
    return;
  }
  Serial.printf("bilinmeyen: %s\n", s.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("SmartKnob bring-up — OPEN-LOOP velocity (sensorsuz)");

  // Surucu
  driver.voltage_power_supply = MOTOR_SUPPLY_V;  // 12V gercek besleme
  driver.voltage_limit        = 6.0f;            // surucu capi (guvenli tavan)
  if (!driver.init()) {
    Serial.println("HATA: driver.init() basarisiz — pin/baglanti kontrol");
    return;
  }
  motor.linkDriver(&driver);

  // Open-loop hiz kontrolu
  motor.controller     = MotionControlType::velocity_openloop;
  motor.voltage_limit  = MOTOR_VOLTAGE_LIMIT;    // !!! 2V guvenli baslangic
  motor.velocity_limit = 20.0f;

  motor.init();

  Serial.println("HAZIR. voltage_limit=2V ile basladi.");
  Serial.println("Komut:  t <rad/s> |  v <volt> |  s (stop) |  ?");
  Serial.println("Ilk deneme: 't 5' gonder. Kipirdamiyorsa 'v 3', sonra 'v 4' (isiyi izle).");
  printStatus();
}

// GUVENLIK: komut gelmezse motoru kes (donmeyen motor akim cekip ISINIR — motor yakmayalim)
static uint32_t lastCmdMs = 0;
static const uint32_t AUTO_STOP_MS = 4000;   // 4sn komut yoksa dur
static bool autoStopped = false;

void loop() {
  // GUVENLIK: son komuttan 4sn sonra otomatik dur (motor isinmasin)
  if (target != 0 && millis() - lastCmdMs > AUTO_STOP_MS) {
    target = 0; motor.disable(); autoStopped = true;
    Serial.println("!! OTO-STOP (4sn komut yok) — motor kesildi, isinmasin");
  }
  // FOC open-loop: her dongude cagrilmali, bloklanmamali
  motor.move(target);

  // Serial komutlari
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleLine(rx); rx = "";
      lastCmdMs = millis();
      if (autoStopped && target != 0) { motor.enable(); autoStopped = false; }
    }
    else { rx += c; if (rx.length() > 40) rx = ""; }
  }
}
