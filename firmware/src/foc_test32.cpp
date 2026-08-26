// SmartKnob — KLASIK ESP32 + L6234 + MT6701: kapali cevrim FOC + centik hissi
// Sadece his testi (HID / ekran / LED / buton YOK).
//
// Pinler (platformio.ini env:foc32 ile override): motor 25/26/27, I2C SDA=21 SCL=22
// L6234: EN pinleri 5V'a SABIT (yazilim enable kullanmiyor). SENSE1(ped2)+SENSE2(ped19) -> GND (SART!)
//
// Komutlar: v <volt> | k <sertlik> | b <sonum> | d <centik> | m free|detent
//           H (homing) | I (sensor teshis) | P <pp> (yeniden kalibre) | S (stream) | ?
#include <Arduino.h>
#include <SimpleFOC.h>
#include "pins.h"

// ---- MT6701 14-bit manyetik aci sensoru (I2C 0x06, reg 0x03/0x04) ----
class MT6701_I2C : public Sensor {
public:
  MT6701_I2C(TwoWire& w, uint8_t addr = 0x06) : _wire(&w), _addr(addr) {}
  void init(TwoWire* w = nullptr) { if (w) _wire = w; this->Sensor::init(); }
  float getSensorAngle() override {
    _wire->beginTransmission(_addr);
    _wire->write((uint8_t)0x03);
    if (_wire->endTransmission(false) != 0) return _last;
    _wire->requestFrom(_addr, (uint8_t)2);
    if (_wire->available() < 2) return _last;
    uint16_t hi = _wire->read(), lo = _wire->read();
    int raw = (hi << 6) | (lo >> 2);                   // 14-bit
    // glitch filtresi: bir okumada >~9deg sicrama fiziksel imkansiz -> bozuk I2C, reddet
    int d = raw - _lastRaw;
    if (d > 8192) d -= 16384; else if (d < -8192) d += 16384;
    if (_lastRaw >= 0 && abs(d) > 400 && _reject < 3) { _reject++; return _last; }
    _reject = 0; _lastRaw = raw;
    _last = (float)raw / 16384.0f * _2PI;
    return _last;
  }
private:
  TwoWire* _wire; uint8_t _addr; float _last = 0.0f;
  int _lastRaw = -1, _reject = 0;
};

MT6701_I2C     sensor = MT6701_I2C(Wire);
BLDCMotor      motor  = BLDCMotor(MOTOR_POLE_PAIRS);
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C);   // EN donanimda 5V'a sabit

enum Mode { M_FREE, M_DETENT };
Mode  mode    = M_DETENT;
int   detents = 17;
float Kdet    = 22.0f;
float Kwall   = 40.0f;
float damping = 0.30f;
float minA = 0, maxA = 1.0f;      // fiziksel duvarlar (homing bulur)
float homingV = 2.2f;
float wallMargin = 0.14f;
bool  stream = false;
float lastT = 0;
String rx;

static float detentSpacing() { return (maxA - minA - 2*wallMargin) / (detents - 1); }
static float firstAngle()    { return minA + wallMargin; }

static float hapticTorque(float a) {
  if (mode == M_FREE) return 0;
  float dMin = minA + wallMargin, dMax = maxA - wallMargin;
  if (a < dMin) return  Kwall * (dMin - a);        // yazilim duvari
  if (a > dMax) return -Kwall * (a - dMax);
  float sp = detentSpacing();
  if (sp < 1e-4f) return 0;
  float idx = roundf((a - dMin) / sp);
  idx = fmaxf(0, fminf(detents - 1, idx));
  return -Kdet * (a - (dMin + idx * sp));           // en yakin centige cek
}

// Fiziksel duvarlari bul (hiz sinirli nazik yaklasim)
static void findWall(int dir, float &out) {
  uint32_t stable = millis(); float last = motor.shaft_angle;
  const float targetVel = dir * 4.0f;
  while (millis() - stable < 400) {
    motor.loopFOC();
    float e = targetVel - motor.shaft_velocity;
    float t = _constrain(0.8f * e, -homingV, homingV);
    motor.move(t);
    if (fabsf(motor.shaft_angle - last) > 0.02f) { last = motor.shaft_angle; stable = millis(); }
    if (millis() - stable > 3000) break;
  }
  out = motor.shaft_angle;
  motor.move(0);
}
static void home() {
  Serial.println("Homing: duvarlar bulunuyor...");
  findWall(-1, minA); delay(200);
  findWall(+1, maxA); delay(200);
  Serial.printf("Homing bitti: min=%.1f max=%.1f aralik=%.1f deg\n",
                minA*180/PI, maxA*180/PI, (maxA-minA)*180/PI);
}

static void handleLine(String s) {
  s.trim(); if (!s.length()) return;
  char c = s[0]; String v = s.substring(1); v.trim();
  if      (c=='v') { motor.voltage_limit = v.toFloat(); Serial.printf("voltage_limit=%.2f\n", motor.voltage_limit); }
  else if (c=='k') { Kdet = v.toFloat();    Serial.printf("Kdet=%.2f\n", Kdet); }
  else if (c=='b') { damping = v.toFloat(); Serial.printf("damping=%.2f\n", damping); }
  else if (c=='d') { detents = max(2,(int)v.toInt()); Serial.printf("detents=%d\n", detents); }
  else if (c=='m') { mode = v.startsWith("f") ? M_FREE : M_DETENT; Serial.printf("mod=%s\n", mode==M_FREE?"free":"detent"); }
  else if (c=='H') { home(); }
  else if (c=='S') { stream = !stream; Serial.printf("stream=%d\n", stream); }
  else if (c=='I') {
    Wire.beginTransmission(0x06);
    if (Wire.endTransmission() != 0) Serial.println("I2C: 0x06 ACK YOK -> SDA/SCL/guc/GND");
    else {
      Wire.beginTransmission(0x06); Wire.write((uint8_t)0x03); Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)0x06, (uint8_t)2);
      uint16_t hi=Wire.read(), lo=Wire.read(); uint16_t raw=(hi<<6)|(lo>>2);
      Serial.printf("I2C: 0x06 OK raw=%d/16384 (%.1f deg)\n", raw, raw*360.0f/16384.0f);
    }
  }
  else if (c=='P') {
    int pp = v.toInt(); if (pp>0) motor.pole_pairs = pp;
    Serial.printf("pole_pairs=%d -> re-initFOC...\n", motor.pole_pairs);
    motor.initFOC();
    Serial.printf("bitti: offset=%.3f dir=%d\n", motor.zero_electric_angle, motor.sensor_direction);
  }
  else if (c=='?') Serial.printf("STATUS aci=%.1f deg mod=%s vlim=%.2f detents=%d Kdet=%.1f damping=%.2f aralik=%.1f deg\n",
      motor.shaft_angle*180/PI, mode==M_FREE?"free":"detent", motor.voltage_limit, detents, Kdet, damping, (maxA-minA)*180/PI);
  else Serial.println("komut: v k b d m H I P S ?");
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println("=== SmartKnob FOC (klasik ESP32 + L6234 + MT6701) ===");
  Serial.printf("motor pinleri: %d %d %d | I2C SDA=%d SCL=%d\n",
                PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C, PIN_I2C_SDA, PIN_I2C_SCL);

  // I2C pin sirasini otomatik algila (capraz baglansa da calissin)
  auto probe = [](int sda, int scl) -> bool {
    Wire.end(); Wire.begin(sda, scl, 100000); delay(20);
    Wire.beginTransmission(0x06); return Wire.endTransmission() == 0;
  };
  if      (probe(PIN_I2C_SDA, PIN_I2C_SCL)) Serial.println("MT6701: normal baglanti OK");
  else if (probe(PIN_I2C_SCL, PIN_I2C_SDA)) Serial.println("MT6701: TERS baglanti algilandi, devam");
  else Serial.println("!! MT6701 0x06 BULUNAMADI (guc/tel) — FOC calismaz");

  sensor.init(&Wire);
  motor.linkSensor(&sensor);

  driver.voltage_power_supply = MOTOR_SUPPLY_V;
  driver.voltage_limit        = 12.0f;
  driver.pwm_frequency        = 20000;
  SimpleFOCDebug::enable(&Serial);
  if (!driver.init()) { Serial.println("!!! driver.init BASARISIZ !!!"); return; }
  motor.linkDriver(&driver);

  motor.controller           = MotionControlType::torque;
  motor.voltage_limit        = 4.0f;      // centik torku (runtime 'v' ile ayarla, isiyi izle)
  motor.voltage_sensor_align = 5.0f;      // kalibrasyon
  motor.velocity_limit       = 100;
  motor.LPF_velocity.Tf      = 0.03f;     // hiz filtresi (damping'i temizler)
  motor.useMonitoring(Serial);
  motor.init();

  Serial.println("initFOC (motor kisaca oynayacak)...");
  motor.initFOC();
  Serial.printf("initFOC bitti: offset=%.3f dir=%d\n", motor.zero_electric_angle, motor.sensor_direction);

  home();
  Serial.println("HAZIR — knob'u cevir, centikleri hissetmelisin.");
  Serial.println("Komut: v k b d m H I P S ?");
}

void loop() {
  motor.loopFOC();
  float t = hapticTorque(motor.shaft_angle) - damping * motor.shaft_velocity;
  if (fabsf(motor.shaft_velocity) > 40.0f) t = 0;            // runaway koruma
  lastT = _constrain(t, -motor.voltage_limit, motor.voltage_limit);
  motor.move(lastT);

  if (stream) {
    static uint32_t lp = 0;
    if (millis() - lp >= 8) { lp = millis();
      Serial.printf("%.1f %.2f %.2f\n", motor.shaft_angle*180/PI, motor.shaft_velocity, lastT);
    }
  }
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch=='\n'||ch=='\r') { handleLine(rx); rx=""; }
    else { rx += ch; if (rx.length()>40) rx=""; }
  }
}
