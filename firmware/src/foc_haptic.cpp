// SmartKnob — CLOSED-LOOP FOC + haptik (gercek AS5600 + motor)
// SADECE `pio run -e foc`.
// Bu, open-loop'un "sik sik"ini bitirir: sensor rotorun yerini bilir, tork orantili uygulanir.
//
// Baglanti: AS5600 I2C GPIO5/6 (3.3V), motor L298N GPIO15/16/17, 12V + ortak GND.
// GUVENLIK: voltage_limit DUSUK (3V) baslar; serial 'v <volt>' ile artir, isiyi izle.
//
// Komutlar (UART/CH340):
//   v <volt>      motor voltage_limit
//   m <mod>       free | detent | spring | endstop
//   d <sayi>      centik sayisi (detent modunda, tam tur icin)
//   k <deger>     centik/yay sertligi
//   ?             durum (aci, mod)

#include <Arduino.h>
#include <SimpleFOC.h>
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#define FASTLED_INTERNAL
#include <FastLED.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "pins.h"

#define NLED 16
USBHIDConsumerControl Consumer;   // medya tusu (ses)
USBHIDKeyboard        Keyboard;   // klavye (Ctrl+ok = masaustu gecis)
USBHIDMouse           Mouse;      // fare tekeri (scroll modu — her uygulamada calisir)

// Uygulama modu: knob ne kontrol ediyor (knob-modu olan aksiyonlar)
enum AppMode { APP_VOLUME, APP_SPACES, APP_TABS, APP_BRIGHTNESS, APP_SCROLL };   // ses/masaustu/chrome/parlaklik/scroll
static AppMode appMode = APP_VOLUME;
CRGB leds[NLED];                   // FastLED (RMT, TinyUSB ile uyumlu)
// Moda gore renk: [0]=VOLUME [1]=SPACES [2]=TABS [3]=BRIGHTNESS [4]=SCROLL
static CRGB colBase[5]   = { CRGB(0,0,0),   CRGB(0,8,20),  CRGB(20,8,0),  CRGB(20,20,0), CRGB(15,0,20) };   // ses/mavi/turuncu/sari/mor
static CRGB colActive[5] = { CRGB(0,35,25), CRGB(0,60,0),  CRGB(90,45,0), CRGB(90,80,0), CRGB(70,0,90) };   // aktif renkler
static float spaceSpacing = 0.26f; // SPACES centik araligi (~15 deg) — aralik ortasina yerlesir

// ---- 8 BUTON -> AKSIYON HARITASI (UI'dan ayarlanir; fiziksel PCF bit sirasi karisik olabilir) ----
enum BtnAction {
  ACT_NONE = 0, ACT_VOLUME, ACT_SPACES, ACT_BRIGHTNESS, ACT_DEPLOY,
  ACT_CLAUDE, ACT_CHROME, ACT_FUSION, ACT_SCROLL, ACT_COUNT
};
// Varsayilan harita (fiziksel dizilis TERS: bit sirasi tersine cevrildi -> alt/ust duzeldi).
// bit0..bit7 = scroll,fusion,chrome,claude,deploy,parlaklik,space,ses
// (UI 'BTN <bit> <act>' ile degistirilebilir; ikili takas gerekirse soyle, degistiririm)
static uint8_t btnAction[8] = {
  ACT_SCROLL, ACT_FUSION, ACT_CHROME, ACT_CLAUDE,
  ACT_DEPLOY, ACT_BRIGHTNESS, ACT_SPACES, ACT_VOLUME
};

// Ekran (ST7789 SPI) — ayri core'da render edilir (FOC'u bloklamaz)
SPIClass tftSPI(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static int level = 8;             // VOLUME: LED barinda gosterilen seviye 0..16 (KONUMDAN bagimsiz, goreli sayac)
static uint32_t hidReleaseAt = 0;
static bool forcing = false;      // ARTIK KULLANILMIYOR (sync motoru surmez) — eski guard'lar icin false kalir
// HERSEY GORELI: motor mutlak konuma zorlanmaz, hareket delta'sindan adim uretir.
static float relAnchor = 0;       // son islenmis centik merkezi (goreli grid referansi)
static int   dispIdx = 0;         // LED'de gosterilen aktif index (HS'ten SP/TAB ile gelir; KOZMETIK)
static float volSpacing = 0.15f;  // VOLUME goreli centik araligi (~8.6°; 16 adim ~137° < 300° -> fiziksel araliga sigar)
// MOD GECISI ORTALAMA: mod degisince motor bir kerelik fiziksel ORTAYA doner -> iki yone de yer acilir.
// SABIT hedefe (orta) gider, DEGERE degil -> kovalanan hareketli hedef yok, forcing gibi savasamaz.
static bool     centering = false;
static uint32_t centerStart = 0;
static float    Kcenter = 6.0f;   // ortalama yayi (NAZIK -> kullanici kolayca override eder)

// ================= DEPLOY KASA KILIDI =================
// P2 -> sifre girisi (sag N tik, sol N tik, ... 4 hane) -> dogruysa KOL (sertlesen 120°) -> deploy
enum DeployState { DEP_OFF, DEP_ENTRY, DEP_POS, DEP_LEVER, DEP_RUNNING, DEP_LOCKED };
static DeployState depState = DEP_OFF;
static uint8_t  depCombo[4]   = {3, 5, 7, 1};   // panelden: PIN a b c d
static int      depDigit = 0, depCount = 0, depDir = 0, depLastTick = 0, depFirstDir = 0;
static float    depAnchor = 0;
static float    depTickRad = 0.22f;             // ~12.6° / tik (kadran hissi)
static uint8_t  depAttempts = 0;
static uint32_t depShakeUntil = 0, depLockUntil = 0, depIdleAt = 0;
static float    depLeverStart = 0, depLeverRange = 1.4f;    // ~80° hedef (mevcut bosluga gore kisilir)
static int      depLeverDir = 0;                            // kol yonu (ilk itiste belirlenir)
static float    depEffRange = 1.4f;                         // o yondeki gercek mesafe (duvara gore)
static uint32_t depHoldStart = 0;
static char     depStatus[20] = "";
static uint32_t depDoneAt = 0;                  // DONE/FAIL geldiginde: ekranda birkac sn kalsin
static float    depLeverProg = 0;               // 0..1 (ekran/LED)
static uint32_t forceStart = 0;   // forcing timeout icin
static int pendingHid = 0;        // gonderilecek net adim (cok-adimli)
static uint32_t lastUserMove = 0; // kullanici en son ne zaman cevirdi (VOL fighting engelle)
static uint32_t nextStepAt = 0;   // adimlar arasi min bekleme (spaces'te Mission Control yetissin)
static uint32_t modeSwitchAt = 0; // mod degisiminden sonra kisa guard (aci oynamasi HID'e cevrilmesin)

// MT6701 14-bit manyetik aci sensoru (I2C 0x06, reg 0x03=Angle[13:6], 0x04=Angle[5:0]<<2)
// AS5600 yerine — 14-bit (4x cozunurluk), daha saglam. SimpleFOC Sensor tabanindan turuyor.
class MT6701_I2C : public Sensor {
public:
  MT6701_I2C(TwoWire& w, uint8_t addr = 0x06) : _wire(&w), _addr(addr) {}
  void init(TwoWire* w = nullptr) { if (w) _wire = w; this->Sensor::init(); }
  float getSensorAngle() override {
    _wire->beginTransmission(_addr);
    _wire->write((uint8_t)0x03);
    if (_wire->endTransmission(false) != 0) return _last;   // I2C hata: son degeri koru
    _wire->requestFrom(_addr, (uint8_t)2);
    if (_wire->available() < 2) return _last;
    uint16_t hi = _wire->read(), lo = _wire->read();
    int raw = (hi << 6) | (lo >> 2);                        // 14-bit (0..16383)
    // GLITCH FILTRESI: bir okumada >~9° (400 count) sicrama fiziksel imkansiz (kHz'de) -> bozuk I2C, REDDET.
    // wrap-aware fark. Ard arda 3 kez farkli okursa GERCEK hareket kabul et (takilma yok).
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

MT6701_I2C        sensor = MT6701_I2C(Wire);
BLDCMotor         motor  = BLDCMotor(MOTOR_POLE_PAIRS);
BLDCDriver3PWM    driver = BLDCDriver3PWM(PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C);

enum Mode { M_FREE, M_DETENT, M_SPRING, M_ENDSTOP };
Mode  mode    = M_DETENT;
int   detents = 17;      // 17 pozisyon = 0..16 ses seviyesi (16 kademe gecisi = macOS)
float Kdet    = 22.0f;   // centik sertligi
float Kspring = 8.0f;    // yay
float Kwall   = 40.0f;   // duvar (sert)
float damping = 0.30f;   // hiz sonumleme (FILTRELI hizla)
float minA = 0, maxA = 1.0f;   // FIZIKSEL duvar acilari (homing ile bulunur)
float homingV = 2.2f;    // homing itme voltaji (nazik — duvara sert vurmasin)
float wallMargin = 0.14f;// duvar ihtiyat payi (rad ~8°): centikler bu payin ICINE yayilir

// seviye <-> aci + LED yardimcilari
static float midA() { return 0.5f * (minA + maxA); }
// SPACES: sabit aralikli centikler fiziksel aralik ORTASINA yerlesir (komple cevirmeye gerek yok).
// VOLUME: centikler tum kullanilabilir araliga yayilir.
// KURAL: VOLUME tum araliga yayilir; DIGER TUM modlar (spaces/tabs/...) ORTADA toplu, sabit aralikli
static float detentSpacing() {
  if (appMode == APP_SCROLL) return 0.055f;   // INCE -> responsive scroll (her ~3° 1 tekerlek tiki)
  return (appMode == APP_VOLUME || appMode == APP_BRIGHTNESS) ? volSpacing : spaceSpacing;
}
static float firstAngle() {   // ilk centigin (index 0) acisi
  if (appMode != APP_VOLUME) return midA() - 0.5f * (detents - 1) * spaceSpacing;  // ortala
  return minA + wallMargin;
}
static float levelAngle(int lv) { return firstAngle() + lv * detentSpacing(); }
static int   detentIndex(float a) {
  if (detents <= 1) return 0;
  int i = (int)roundf((a - firstAngle()) / detentSpacing());
  return constrain(i, 0, detents - 1);
}
static bool barMode() { return appMode == APP_VOLUME || appMode == APP_BRIGHTNESS; }  // dolan-bar modlari (ses/parlaklik)
static void updateLED(int lv) {
  CRGB base = colBase[appMode], act = colActive[appMode];
  if (barMode()) {                             // dolan bar: seviyeye kadar aktif renk
    for (int i = 0; i < NLED; i++) leds[i] = (i < lv) ? act : base;
  } else if (appMode == APP_SCROLL) {          // SCROLL: donen tek nokta (gorsel geri bildirim)
    fill_solid(leds, NLED, base);
    leds[((lv % NLED) + NLED) % NLED] = act;
  } else {                                     // SPACES: n centik ORTADA, esit aralikli, AYRI LED'ler (cakisma yok)
    fill_solid(leds, NLED, base);
    int spacing = ((detents - 1) * 2 <= NLED - 1) ? 2 : 1;      // aralik 2 LED (sigmazsa 1)
    int width   = (detents - 1) * spacing;
    int start   = (int)roundf((NLED - 1) / 2.0f - width / 2.0f); // ring ortasina yasla
    for (int k = 0; k < detents; k++) {
      int p = constrain(start + k * spacing, 0, NLED - 1);
      leds[p] = (k == lv) ? act : (base + CRGB(0,15,30));       // aktif centik = yesil, digerleri sonuk mavi
    }
  }
  FastLED.show();
}

// HYPER kombo (Ctrl+Alt+Cmd+Shift + harf) -> HS bunu yakalar (uygulama ac/odakla, parlaklik vb.)
static void sendHyper(char key) {
  Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_SHIFT);
  Keyboard.press(KEY_LEFT_ALT);  Keyboard.press(KEY_LEFT_GUI);
  Keyboard.press(key);
  hidReleaseAt = millis() + 8;   // loop birakir (releaseAll)
}

// Bir centik adimini moda gore HID olarak gonder (bloklamaz, hidReleaseAt ile birakilir)
static void sendStep(int dir) {
  if (appMode == APP_VOLUME) {
    Consumer.press(dir > 0 ? CONSUMER_CONTROL_VOLUME_INCREMENT : CONSUMER_CONTROL_VOLUME_DECREMENT);
    hidReleaseAt = millis() + 8;
  } else if (appMode == APP_SPACES) { // Ctrl+Sag/Sol = masaustu gecis
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press(dir > 0 ? KEY_RIGHT_ARROW : KEY_LEFT_ARROW);
    hidReleaseAt = millis() + 8;
  } else if (appMode == APP_BRIGHTNESS) { // HS: HYPER+U arttir / HYPER+N azalt (=/- macOS zoom ile cakisiyordu)
    sendHyper(dir > 0 ? 'u' : 'n');
  } else if (appMode == APP_SCROLL) {     // fare tekeri (native, her uygulamada calisir, HS yok)
    Mouse.move(0, 0, dir > 0 ? 1 : -1, 0);   // wheel +yukari / -asagi
  } else { // APP_TABS: Chrome-KILITLI -> HS'e F19(next)/F20(prev) bildir, HS AppleScript ile sekme degistirir
    Keyboard.press(dir > 0 ? KEY_F19 : KEY_F20);
    hidReleaseAt = millis() + 8;
  }
}

// Mod uygula (MODE komutu VE PCF butonu ayni yoldan kullanir)
static void applyMode(AppMode m) {
  appMode = m;
  if (m == APP_SPACES)     { detents = 3;  Kdet = 30.0f; }   // HS 'SP n idx' ile gunceller
  else if (m == APP_TABS)  { detents = 8;  Kdet = 30.0f; }   // HS 'TAB n idx' ile gunceller
  else                     { detents = 17; Kdet = 22.0f; }   // volume
  forcing = false;
  relAnchor = motor.shaft_angle;                 // goreli referans (ortalama bitince guncellenir)
  updateLED(appMode == APP_VOLUME ? level : dispIdx);
  pendingHid = 0;
  modeSwitchAt = millis();   // geçiş sonrası kisa guard (sahte navigasyon yok)
  // ORTALAMA sadece DUVARA YAKINSA (orta bolgedeysen gereksiz -> gecikme olmasin, aninda tepki).
  float half = (maxA - minA) * 0.5f;
  centering = (half > 0.15f) && (fabsf(motor.shaft_angle - midA()) > half * 0.55f);
  centerStart = millis();
  Serial.printf("MOD %d -> ortalama=%d\n", (int)m, centering ? 1 : 0);
}

// ---- DEPLOY: yardimcilar ----
static void depResetEntry() {
  depDigit = 0; depCount = 0; depDir = 0; depFirstDir = 0;
  depAnchor = motor.shaft_angle; depLastTick = 0; depIdleAt = millis();
}
static void depEnter() {                       // P2 -> kasa moduna gir
  if (millis() < depLockUntil) { depState = DEP_LOCKED; return; }
  depState = DEP_ENTRY; depResetEntry(); depStatus[0] = 0;
}
static void depWrong() {                       // yanlis hane -> titret, 3 hatada kilitle
  depShakeUntil = millis() + 600;
  if (++depAttempts >= 3) {
    depAttempts = 0; depState = DEP_LOCKED; depLockUntil = millis() + 180000;   // 3 dk
  } else depResetEntry();
}
static void depToLever() {                     // sifre dogru -> KOL (buradan itmeye basla)
  depState = DEP_LEVER;
  depLeverStart = motor.shaft_angle;           // password bittigi yerden basla (siçrama yok)
  depLeverDir = 0; depEffRange = depLeverRange;
  depHoldStart = 0; depLeverProg = 0; depIdleAt = millis();
}
// her tik gecisinde cagrilir. YON GORECELI: ilk hane yonu referans, sonraki haneler ALTERNATIF.
static void depTick(int step) {
  depIdleAt = millis();
  if (depDir == 0) {                                  // hanenin ilk tigi
    if (depDigit == 0) {
      depFirstDir = step;                             // ilk hane -> referans yon (sag/sol farketmez)
    } else {
      int expected = (depDigit % 2 == 0) ? depFirstDir : -depFirstDir;   // alternatif
      if (step != expected) { depWrong(); return; }
    }
    depDir = step; depCount = 1;
  } else if (step == depDir) {
    depCount++;
  } else {                                            // YON DEGISTI -> haneyi dogrula
    if (depCount == depCombo[depDigit]) {
      depDigit++;
      if (depDigit > 3) { depToLever(); return; }
      int expected = (depDigit % 2 == 0) ? depFirstDir : -depFirstDir;
      if (step != expected) { depWrong(); return; }
      depDir = step; depCount = 1;                    // yeni hane bu tikle basliyor
    } else { depWrong(); return; }
  }
  if (depDigit == 3 && depCount == depCombo[3]) depToLever();   // son hane: dogru sayida ACILIR
}

// Deploy modundayken torku uretir; true donerse normal haptik/HID ATLANIR
static bool depUpdate(float &t) {
  if (depState == DEP_OFF) return false;
  uint32_t now = millis();
  float a = motor.shaft_angle;

  if (now < depShakeUntil) {                        // YANLIS: hizli sag-sol titreme
    t = (((now / 25) % 2) ? 1.0f : -1.0f) * 2.5f;
    return true;
  }
  switch (depState) {
    case DEP_LOCKED:
      if (now >= depLockUntil) depState = DEP_OFF;
      t = 0; return true;

    case DEP_ENTRY: {
      if (now - depIdleAt > 12000) { depState = DEP_OFF; t = 0; return true; }   // 12sn bos -> iptal
      int idx = (int)roundf((a - depAnchor) / depTickRad);                        // tik gecisi
      if (idx != depLastTick) { int step = (idx > depLastTick) ? 1 : -1; depLastTick = idx; depTick(step); }
      float c = depAnchor + depLastTick * depTickRad;                             // net kadran centigi
      t = -28.0f * (a - c) - damping * motor.shaft_velocity;
      return true;
    }
    case DEP_POS: {                                 // kol baslangicina nazikce git
      float e = a - depLeverStart;
      t = -10.0f * e - 0.7f * motor.shaft_velocity;
      if (fabsf(e) < 0.06f && fabsf(motor.shaft_velocity) < 0.5f) {
        depState = DEP_LEVER; depHoldStart = 0; depIdleAt = now;
      }
      return true;
    }
    case DEP_LEVER: {
      float d = a - depLeverStart;
      if (depLeverDir == 0) {                                        // yon henuz yok: ilk anlamli itiste belirle
        if (fabsf(d) > 0.08f) {
          depLeverDir = (d > 0) ? 1 : -1;
          float room = (depLeverDir > 0) ? (maxA - wallMargin - depLeverStart)
                                         : (depLeverStart - (minA + wallMargin));
          depEffRange = fminf(depLeverRange, fmaxf(0.6f, room - 0.05f));  // duvara gore kis (her iki yon %100'e ulassin)
        }
        t = -3.0f * d - damping * motor.shaft_velocity;             // hafif merkez
        return true;
      }
      float dd = d * depLeverDir;                                    // itilen yonde pozitif
      depLeverProg = _constrain(dd / depEffRange, 0.0f, 1.0f);
      float k = 4.0f * (1.0f + depLeverProg * depLeverProg * 6.0f);  // sonlara dogru sertlesir ama TUTULABILIR (max ~28)
      t = -k * d - damping * motor.shaft_velocity;
      if (depLeverProg > 0.15f) depIdleAt = now;                     // itiyorsa canli tut
      if (depLeverProg >= 0.9f) {                                    // sona yaklastin -> kisa TUT -> ONAY
        if (!depHoldStart) depHoldStart = now;
        else if (now - depHoldStart > 150) {
          depState = DEP_RUNNING; depAttempts = 0;
          snprintf(depStatus, sizeof depStatus, "BASLIYOR");
          Keyboard.press(KEY_F13); delay(5); Keyboard.releaseAll();  // HS deploy'u calistirir
        }
      } else depHoldStart = 0;
      if (now - depIdleAt > 3000) depState = DEP_OFF;                // biraktin -> iptal
      return true;
    }
    case DEP_RUNNING:
      if (depDoneAt && now - depDoneAt > 4000) { depDoneAt = 0; depState = DEP_OFF; depStatus[0] = 0; }
      t = -8.0f * (a - depLeverStart) - damping * motor.shaft_velocity;   // yerinde tut
      return true;
    default: t = 0; return true;
  }
}

static float hapticTorque(float a) {
  if (mode == M_FREE) return 0;
  if (appMode == APP_SCROLL) return 0;   // SCROLL: serbest spin, centik YOK (tik emme tracking'de, ince)
  // HERSEY DUVARSIZ GORELI: sonsuz centik gridi -> her mod serbest, her centik = bir adim.
  // Yazilim duvari YOK (fiziksel ~300° mekanik stop tek sinir; ortalama bizi ondan uzak tutar).
  // Konum mutlak degil (relAnchor'a gore) -> mod gecisinde "konum=deger" cakismasi yok.
  if (mode == M_DETENT) {
    float sp = detentSpacing();
    float c  = relAnchor + roundf((a - relAnchor) / sp) * sp;
    return -Kdet * (a - c);
  }
  if (mode == M_SPRING) return -Kspring * (a - 0.5f * (minA + maxA));
  return 0;   // M_ENDSTOP/diger: serbest
}

// --- HOMING: fiziksel duvarlari bul (HIZ-SINIRLI yaklasim -> duvara NAZIK dayanir, slam yok) ---
static void findWall(int dir, float &out) {
  uint32_t t0 = millis(); uint32_t stable = 0;
  const float targetVel = dir * 4.0f;   // yavas sabit yaklasim hizi (rad/s) -> duvara az hizla carpar
  while (millis() - t0 < 5000) {
    motor.loopFOC();
    // hiz P-kontrol: hedef hiza yaklas, voltaj homingV ile sinirli.
    // serbest gezerken 4 rad/s, duvarda hiz 0'a duser -> voltaj homingV'de doyar (nazik BASKI, slam degil)
    float v = 0.6f * (targetVel - motor.shaft_velocity);
    v = _constrain(v, -homingV, homingV);
    motor.move(v);
    if (fabs(motor.shaft_velocity) < 0.2f) {
      if (!stable) stable = millis();
      else if (millis() - stable > 350) break;   // 350ms sabit = duvara dayandi
    } else stable = 0;
  }
  motor.move(0);
  out = motor.shaft_angle;
}
static void home() {
  Serial.println("Homing: duvarlar bulunuyor...");
  float w1, w2;
  findWall(+1, w1); delay(150);
  findWall(-1, w2); delay(150);
  motor.move(0);
  minA = fmin(w1, w2); maxA = fmax(w1, w2);
  Serial.printf("Homing bitti: min=%.1f max=%.1f aralik=%.1f deg\n",
                minA*180.0f/PI, maxA*180.0f/PI, (maxA-minA)*180.0f/PI);
}

static const char* modeName() {
  return mode==M_FREE?"free":mode==M_DETENT?"detent":mode==M_SPRING?"spring":"endstop";
}

static float lastT = 0;      // uygulanan son tork (telemetri)
static bool  stream = false; // yuksek hizli akis
static bool  dbgStep = false; // step-olay teshis logu

static String rx;
static void handleLine(String s) {
  s.trim(); if (!s.length()) return;
  if (s.startsWith("MODE")) {  // mod degistir: volume | spaces
    String m = s.substring(4); m.trim();
    if (m == "spaces") applyMode(APP_SPACES);
    else if (m == "volume") applyMode(APP_VOLUME);
    else if (m == "tabs") applyMode(APP_TABS);
    Serial.printf("appMode=%d detents=%d\n", (int)appMode, detents);
    return;
  }
  if (s.startsWith("SP")) {   // SP <n> <idx> : space sayisi + aktif index (Hammerspoon otomatik)
    int n = 0, idx = 0; sscanf(s.c_str() + 2, "%d %d", &n, &idx);
    if (n >= 1) {
      appMode = APP_SPACES; detents = n; dispIdx = constrain(idx, 0, n - 1);
      updateLED(dispIdx);   // motoru SURMEZ; sadece LED aktif space'i gosterir (kozmetik)
    }
    Serial.printf("SP n=%d idx=%d\n", n, idx);
    return;
  }
  if (s.startsWith("TAB")) {   // TAB <n> <idx> : chrome sekme sayisi + aktif index (Hammerspoon)
    int n = 0, idx = 0; sscanf(s.c_str() + 3, "%d %d", &n, &idx);
    if (n >= 1) {
      appMode = APP_TABS; detents = n; dispIdx = constrain(idx, 0, n - 1);
      updateLED(dispIdx);   // motoru SURMEZ; sadece LED aktif sekmeyi gosterir (kozmetik)
    }
    Serial.printf("TAB n=%d idx=%d\n", n, idx);
    return;
  }
  if (s == "DEPOFF") {         // deploy modundan cik / kilidi temizle (panel/debug)
    depState = DEP_OFF; depLockUntil = 0; depAttempts = 0; depStatus[0] = 0;
    Serial.println("deploy off");
    return;
  }
  if (s.startsWith("PIN")) {   // PIN <a> <b> <c> <d> : kasa sifresi (panelden)
    int a=0,b=0,c=0,d=0; sscanf(s.c_str()+3, "%d %d %d %d", &a,&b,&c,&d);
    if (a>0&&b>0&&c>0&&d>0&&a<20&&b<20&&c<20&&d<20) {
      depCombo[0]=a; depCombo[1]=b; depCombo[2]=c; depCombo[3]=d;
      Serial.printf("PIN ayarlandi: %d %d %d %d\n", a,b,c,d);
    } else Serial.println("PIN gecersiz (1-19 arasi 4 hane)");
    return;
  }
  if (s.startsWith("DST")) {   // DST <metin> : deploy asamasi (HS'ten) -> ekranda goster
    String v = s.substring(3); v.trim();
    snprintf(depStatus, sizeof depStatus, "%s", v.c_str());
    if (depState == DEP_OFF) depState = DEP_RUNNING;      // dis tetikle de gelse ekranda goster
    if (v.startsWith("DONE") || v.startsWith("TAMAM") || v.startsWith("FAIL") || v.startsWith("HATA")
        || v.startsWith("EXIT")) depDoneAt = millis();     // 4sn ekranda kalsin, sonra normale don
    Serial.printf("DST: %s\n", depStatus);
    return;
  }
  if (s.startsWith("COL")) { // COL <b|a> <r> <g> <b> : mevcut modun taban/aktif rengi
    char slot = s.length() > 4 ? s[4] : 'a';
    int r = 0, g = 0, b = 0; sscanf(s.c_str() + 5, "%d %d %d", &r, &g, &b);
    if (slot == 'b') colBase[appMode] = CRGB(r, g, b); else colActive[appMode] = CRGB(r, g, b);
    updateLED(level);
    Serial.printf("COL %c=%d,%d,%d mode=%d\n", slot, r, g, b, appMode);
    return;
  }
  if (s == "CFG?") {         // app baglaninca mevcut ayarlari cek
    Serial.printf("CFG mode=%d detents=%d Kdet=%.1f Kwall=%.1f damping=%.2f base=%u,%u,%u act=%u,%u,%u\n",
      appMode, detents, Kdet, Kwall, damping,
      colBase[appMode].r, colBase[appMode].g, colBase[appMode].b,
      colActive[appMode].r, colActive[appMode].g, colActive[appMode].b);
    return;
  }
  if (s.startsWith("VOL")) {   // Mac ses senkronu -> SADECE LED barini boyar (motoru SURMEZ, savas yok)
    if (appMode != APP_VOLUME) return;
    int n = constrain((int)s.substring(3).toInt(), 0, detents - 1);
    if (millis() - lastUserMove > 400) { level = n; updateLED(level); }   // kullanici cevirmiyorken senkronla
    return;
  }
  if (s.startsWith("BR")) {    // parlaklik senkronu (HS) -> LED bar (motoru SURMEZ)
    if (appMode != APP_BRIGHTNESS) return;
    int n = constrain((int)s.substring(2).toInt(), 0, detents - 1);
    if (millis() - lastUserMove > 400) { level = n; updateLED(level); }
    return;
  }
  if (s.startsWith("BTNMAP?")) {   // haritayi dok (UI cekmek icin)
    Serial.print("BTNMAP");
    for (int i = 0; i < 8; i++) Serial.printf(" %d", btnAction[i]);
    Serial.println();
    return;
  }
  if (s.startsWith("BTN ")) {  // BTN <bit 0..7> <act 0..8> : fiziksel butonu aksiyona esle (UI'dan)
    int bit = -1, act = -1; sscanf(s.c_str() + 3, "%d %d", &bit, &act);
    if (bit >= 0 && bit < 8 && act >= 0 && act < ACT_COUNT) {
      btnAction[bit] = (uint8_t)act;
      Serial.printf("BTN %d -> act %d\n", bit, act);
    }
    return;
  }
  char c = s[0];
  String v = s.substring(1); v.trim();
  if (c=='v') { motor.voltage_limit = v.toFloat(); Serial.printf("voltage_limit=%.2f\n", motor.voltage_limit); }
  else if (c=='d') { detents = max(1, (int)v.toInt()); Serial.printf("detents=%d\n", detents); }
  else if (c=='k') { Kdet = v.toFloat(); Serial.printf("Kdet=%.2f\n", Kdet); }
  else if (c=='b') { damping = v.toFloat(); Serial.printf("damping=%.3f\n", damping); }
  else if (c=='f') { motor.LPF_velocity.Tf = v.toFloat(); Serial.printf("LPF_Tf=%.3f\n", motor.LPF_velocity.Tf); }
  else if (c=='S') { stream = !stream; Serial.printf("stream=%d\n", stream); }
  else if (c=='D') { dbgStep = !dbgStep; Serial.printf("dbgStep=%d\n", dbgStep); }
  else if (c=='H') { home(); }
  else if (c=='L') { fill_solid(leds, NLED, CRGB(120,120,120)); FastLED.show(); Serial.println("LED test: hepsi beyaz"); }
  else if (c=='w') { wallMargin = v.toFloat() * PI / 180.0f; Serial.printf("wallMargin=%.1f deg\n", wallMargin*180.0f/PI); }
  else if (c=='W') { Kwall = v.toFloat(); Serial.printf("Kwall=%.1f\n", Kwall); }
  else if (c=='P') {   // pole pairs degistir + yeniden kalibre (dogru pp'yi bulmak icin)
    int pp = v.toInt();
    if (pp > 0) {
      motor.pole_pairs = pp;
      Serial.printf("pole_pairs=%d -> re-initFOC...\n", pp);
      motor.move(0);
      motor.initFOC();
      Serial.printf("bitti: offset=%.3f dir=%d\n", motor.zero_electric_angle, motor.sensor_direction);
    }
  }
  else if (c=='m') {
    if (v=="free") mode=M_FREE; else if (v=="detent") mode=M_DETENT;
    else if (v=="spring") mode=M_SPRING; else if (v=="endstop") mode=M_ENDSTOP;
    Serial.printf("mode=%s\n", modeName());
  }
  else if (c=='I') {   // MT6701 I2C teshis: 0x06 cevap + ham 14-bit aci
    Wire.beginTransmission(0x06);
    if (Wire.endTransmission() != 0) {
      Serial.println("I2C: 0x06 (MT6701) ACK YOK -> SDA/SCL/guc/GND kontrol et");
    } else {
      Wire.beginTransmission(0x06); Wire.write((uint8_t)0x03); Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)0x06, (uint8_t)2);
      uint16_t hi = Wire.read(), lo = Wire.read();
      uint16_t raw = (hi << 6) | (lo >> 2);
      Serial.printf("I2C: 0x06 OK  raw=%d/16384 (%.1f deg)\n", raw, raw * 360.0f / 16384.0f);
    }
  }
  else if (c=='?') Serial.printf("STATUS aci=%.3f rad (%.1f deg) mode=%s vlim=%.2f detents=%d\n",
                                 motor.shaft_angle, motor.shaft_angle*180.0f/PI, modeName(), motor.voltage_limit, detents);
  else Serial.printf("bilinmeyen: %s\n", s.c_str());
}

// --- EKRAN (core 0) — mod + seviye goster, degisince ciz, FOC'u bloklamaz ---
static void drawScreen() {
  // ---- DEPLOY ekranlari (kasa kilidi) ----
  if (depState != DEP_OFF) {
    tft.fillScreen(ST77XX_BLACK);
    char b[32];
    if (depState == DEP_LOCKED) {
      int sec = (int)((depLockUntil - millis()) / 1000); if (sec < 0) sec = 0;
      tft.setTextColor(ST77XX_RED); tft.setTextSize(3); tft.setCursor(20, 40); tft.print("KILITLI");
      tft.setTextColor(ST77XX_WHITE); tft.setTextSize(5);
      snprintf(b, sizeof b, "%d sn", sec); tft.setCursor(20, 110); tft.print(b);
    } else if (depState == DEP_ENTRY) {
      tft.setTextColor(tft.color565(255,180,0)); tft.setTextSize(3); tft.setCursor(20, 22); tft.print("DEPLOY KILIDI");
      // haneler: girilmisler * , simdiki canli, kalanlar _
      tft.setTextSize(4); tft.setCursor(20, 80); tft.setTextColor(ST77XX_WHITE);
      for (int i = 0; i < 4; i++) {
        if (i < depDigit)      tft.print("* ");
        else if (i == depDigit){ snprintf(b, sizeof b, "%d ", depCount); tft.print(b); }
        else                    tft.print("_ ");
      }
      tft.setTextSize(3); tft.setCursor(20, 150);
      tft.setTextColor(tft.color565(0,200,255));
      tft.print((depDigit % 2 == 0) ? "-> saga" : "<- sola");
    } else if (depState == DEP_POS) {
      tft.setTextColor(tft.color565(255,180,0)); tft.setTextSize(3); tft.setCursor(20, 90); tft.print("HAZIRLANIYOR");
    } else if (depState == DEP_LEVER) {
      tft.setTextColor(tft.color565(255,120,0)); tft.setTextSize(3); tft.setCursor(20, 30); tft.print("KOLU CEVIR");
      int pct = (int)(depLeverProg * 100);
      tft.setTextColor(ST77XX_WHITE); tft.setTextSize(6);
      snprintf(b, sizeof b, "%d%%", pct); tft.setCursor(110, 90); tft.print(b);
      uint16_t c = (depLeverProg > 0.9f) ? ST77XX_GREEN : tft.color565(255, 140, 0);
      tft.drawRect(19, 189, 282, 26, tft.color565(70,70,70));
      tft.fillRect(20, 190, (int)(280 * depLeverProg), 24, c);
    } else if (depState == DEP_RUNNING) {
      tft.setTextColor(tft.color565(0,200,255)); tft.setTextSize(3); tft.setCursor(20, 40); tft.print("DEPLOY");
      tft.setTextColor(ST77XX_WHITE); tft.setTextSize(4); tft.setCursor(20, 110);
      tft.print(depStatus[0] ? depStatus : "...");
    }
    return;
  }
  const char* label; uint16_t accent;
  switch (appMode) {
    case APP_VOLUME:     label = "SES";       accent = tft.color565(0, 220, 120); break;
    case APP_SPACES:     label = "MASAUSTU";  accent = tft.color565(0, 180, 255); break;
    case APP_BRIGHTNESS: label = "PARLAKLIK"; accent = tft.color565(240, 220, 0); break;
    case APP_SCROLL:     label = "SCROLL";    accent = tft.color565(180, 60, 240); break;
    default:             label = "SEKME";     accent = tft.color565(255, 150, 0); break;  // tabs
  }
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(accent);
  tft.setTextSize(3);
  tft.setCursor(20, 28);
  tft.print(label);

  int shown = barMode() ? level : dispIdx;   // ses/parlaklik=level bar, space/tabs=aktif index
  char buf[16];
  if (barMode()) { int pct = (detents > 1) ? shown * 100 / (detents - 1) : 0; snprintf(buf, sizeof buf, "%d%%", pct); }
  else     { snprintf(buf, sizeof buf, "%d/%d", shown + 1, detents); }
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(6);
  int w = (int)strlen(buf) * 36;              // size6 ~36px/karakter
  int x = (320 - w) / 2; if (x < 0) x = 0;    // 320 genislige gore ortala (YATAY)
  tft.setCursor(x, 95);
  tft.print(buf);

  int barw = (detents > 1) ? shown * 280 / (detents - 1) : 0;   // alt seviye cubugu (yatay 320)
  tft.drawRect(19, 199, 282, 22, tft.color565(60, 60, 60));
  tft.fillRect(20, 200, 280, 20, ST77XX_BLACK);
  tft.fillRect(20, 200, barw, 20, accent);
}

static void displayTask(void *pv) {
  int lastLevel = -999, lastDet = -1, lastMode = -1, lastIdx = -999;
  int lastDep = -1, lastDigit = -1, lastCount = -1, lastProg = -1; char lastSt[20] = "";
  int lastSec = -1;
  for (;;) {
    int prog = (int)(depLeverProg * 50);   // kaba adim (surekli cizim olmasin)
    int sec  = (depState == DEP_LOCKED) ? (int)((depLockUntil - millis()) / 1000) : -1;   // kilit geri sayimi
    bool depChanged = ((int)depState != lastDep || depDigit != lastDigit || depCount != lastCount
                       || prog != lastProg || sec != lastSec || strcmp(depStatus, lastSt) != 0);
    lastSec = sec;
    if (depChanged || (depState == DEP_OFF &&
        (level != lastLevel || dispIdx != lastIdx || (int)appMode != lastMode || detents != lastDet))) {
      lastLevel = level; lastIdx = dispIdx; lastMode = (int)appMode; lastDet = detents;
      lastDep = (int)depState; lastDigit = depDigit; lastCount = depCount; lastProg = prog;
      snprintf(lastSt, sizeof lastSt, "%s", depStatus);
      drawScreen();
    }
    vTaskDelay(40 / portTICK_PERIOD_MS);
  }
}

// Buton aksiyonu uygula (haritadan gelen act ID). Modlar appMode + F-tusu (HS senkron); app'ler HYPER kombo.
static void doAction(uint8_t act) {
  // DEPLOY disindaki her aksiyon once deploy'dan CIKAR (yanlislikla girince kilitli kalmasin).
  // Ceza suresi (depLockUntil) korunur -> tekrar deploy'a girisi depEnter yine engeller.
  if (act != ACT_DEPLOY && depState != DEP_OFF) depState = DEP_OFF;
  switch (act) {
    case ACT_VOLUME:     applyMode(APP_VOLUME);     Keyboard.press(KEY_F16); break;
    case ACT_SPACES:     applyMode(APP_SPACES);     Keyboard.press(KEY_F17); break;
    case ACT_BRIGHTNESS: applyMode(APP_BRIGHTNESS); Keyboard.press(KEY_F14); break;
    case ACT_CHROME:     applyMode(APP_TABS);       Keyboard.press(KEY_F18); break;
    case ACT_SCROLL:     applyMode(APP_SCROLL);     return;   // native fare tekeri (HS yok)
    case ACT_DEPLOY:     depEnter(); return;                 // kasa kilidi deploy menusu (sifre + kol)
    case ACT_CLAUDE:     sendHyper('j'); return;              // HS: Claude ac/odakla
    case ACT_FUSION:     sendHyper('g'); return;              // HS: Fusion ac/odakla
    default: return;
  }
  delay(5); Keyboard.releaseAll();
}

void setup() {
  Serial.begin(115200);
  delay(800);

  // FastLED'i USB/FOC'tan ONCE baslat
  FastLED.addLeds<WS2812B, PIN_RGB, GRB>(leds, NLED);
  FastLED.setBrightness(50);
  fill_solid(leds, NLED, CRGB(60,60,60)); FastLED.show();   // acilis testi: beyaz

  Serial.println();
  Serial.println("SmartKnob — CLOSED-LOOP FOC + haptik");

  // AS5600 I2C pin sirasini OTOMATIK algila (kablo capraz olsa da calissin — SDA/SCL karisinca dert cikmasin)
  auto probe = [](int sda, int scl) -> bool {
    Wire.end(); Wire.begin(sda, scl, 100000); delay(20);   // 100kHz: I2C glitch/bozulma azalir (uzun tel/gurultu)
    Wire.beginTransmission(0x06); return Wire.endTransmission() == 0;
  };
  if (probe(PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("MT6701: (SDA=5,SCL=6) normal baglanti.");
  } else if (probe(PIN_I2C_SCL, PIN_I2C_SDA)) {
    Serial.println("MT6701: TERS baglanti algilandi (SDA=6,SCL=5) — ters sirada devam.");
  } else {
    Serial.println("HATA: MT6701 0x06 bulunamadi! (guc/tel) — homing calismaz.");
  }
  sensor.init(&Wire);
  motor.linkSensor(&sensor);

  driver.voltage_power_supply = MOTOR_SUPPLY_V;   // 12V
  driver.voltage_limit        = 8.0f;   // align 7V'a izin ver
  driver.pwm_frequency        = 20000;  // 20kHz: isitilebilir ugultu azalir (L298N icin sinirda, isiyi izle)
  if (!driver.init()) { Serial.println("HATA driver.init"); return; }
  motor.linkDriver(&driver);

  motor.controller    = MotionControlType::torque;   // tork = voltaj (akim sensoru yok)
  motor.voltage_limit = 5.0f;                         // haptik tork tavani (L298N dususu icin biraz yuksek)
  motor.voltage_sensor_align = 7.0f;                  // KALIBRASYON voltaji yuksek: rotor elektriksel sifira SAGLAM otursun (tutarli offset)
  motor.velocity_limit = 100;
  motor.LPF_velocity.Tf = 0.03f;                      // hiz LPF: damping'i temizler (titreme->salinim yerine duz)
  motor.useMonitoring(Serial);                        // initFOC ayrintilarini goster
  motor.init();

  // KALIBRASYON OTOMATIK: initFOC her acilista offset+yonu kendi bulur (rewiring'e dayanikli).
  // (Motor kisaca oynar ~1s. Faz sirasi/kablo degisirse dogru degerler otomatik cikar.)
  Serial.println("initFOC (OTOMATIK kalibrasyon — motor kisaca oynayacak)...");
  motor.initFOC();
  Serial.printf("initFOC bitti. zero_offset=%.3f dir=%d  (sabit kod icin not al)\n",
                motor.zero_electric_angle, motor.sensor_direction);

  home();   // fiziksel duvarlari bul -> centikler bu aralaga yayilir

  // PCF8574 buton genisletici (I2C1 AYRI bus, AS5600'u yavaslatmaz)
  Wire1.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, 100000);
  Wire1.beginTransmission(0x20);
  bool pcf = (Wire1.endTransmission() == 0);
  if (pcf) { Wire1.beginTransmission(0x20); Wire1.write(0xFF); Wire1.endTransmission(); }  // hepsi giris
  Serial.printf("PCF8574 (0x20): %s\n", pcf ? "VAR — herhangi butona bas = mod degistir" : "yok");

  Consumer.begin();   // native USB HID (ses tusu)
  Keyboard.begin();   // klavye (Ctrl+ok = masaustu gecis)
  Mouse.begin();      // fare tekeri (scroll modu)
  USB.begin();

  level = detentIndex(motor.shaft_angle);   // baslangic seviyesi mevcut konumdan
  updateLED(level);

  // Ekran: ayri core'da (core 0) render — FOC loop core 1'de pürüzsüz kalir
  tftSPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240, 320); tft.setRotation(3);   // YATAY (landscape, 180 donuk) -> 320x240
  drawScreen();
  xTaskCreatePinnedToCore(displayTask, "disp", 4096, NULL, 1, NULL, 0);

  Serial.println("HAZIR. Komut: v m d k b f H S VOL<n> ?  | HID+LED+Mac senkron");
}

void loop() {
  motor.loopFOC();

  // Aciliste bir kez: HS'e "volume modu" bildir (F16) -> HS guncel Mac sesini yollar (boot sync)
  static bool bootSynced = false;
  if (!bootSynced && millis() > 2500) {   // USB enumere olsun diye bekle
    bootSynced = true;
    Keyboard.press(KEY_F16); delay(5); Keyboard.releaseAll();
  }

  // --- tork: normal haptik VEYA Mac senkronu (zorla-git) ---
  // Yaklasim: SmartKnob mantigi — KRITIK SONUMLU 2. derece setpoint (S-egrisi, jerk yok) +
  //           motorun setpoint'i takibi + DEAD ZONE (settle buzz'i yok) + VELOCITY CUTOFF (runaway yok).
  float t;
  if (depUpdate(t)) {
    // DEPLOY modu (kasa kilidi/kol) -> torku o uretir, normal haptik+HID atlanir
  } else if (fabsf(motor.shaft_velocity) > 40.0f) {   // VELOCITY CUTOFF (SmartKnob): runaway yok
    t = 0;
  } else if (centering) {
    // MOD GECISI ORTALAMA: sabit hedef=fiziksel orta. NAZIK yay (dusuk V) -> kullanici kolay override eder.
    float e = motor.shaft_angle - midA();
    t = -Kcenter * e;
    t -= damping * motor.shaft_velocity;
    bool reached     = fabsf(e) < 0.05f && fabsf(motor.shaft_velocity) < 0.5f;   // ortaya oturdu
    bool pushingAway = (e * motor.shaft_velocity) > 2.0f;                         // kullanici merkezden UZAGA itiyor -> DOKUNDU
    if (reached || pushingAway || millis() - centerStart > 700) {
      centering = false;
      relAnchor = motor.shaft_angle;   // goreli grid'i knob'un su anki yerinden baslat
      Serial.printf("ORTALAMA bitti (%s) aci=%.1f\n",
        reached ? "oturdu" : pushingAway ? "kullanici-dokundu" : "timeout", motor.shaft_angle*180.0f/PI);
    }
  } else {
    // TEK yol: yerel deterministik haptik. Sync ARTIK motoru surmez -> titreme/atlama yok.
    t = hapticTorque(motor.shaft_angle);
    t -= damping * motor.shaft_velocity;
  }
  float vmax = centering ? 2.5f : motor.voltage_limit;   // ortalama NAZIK (dusuk V, savasamaz)
  lastT = _constrain(t, -vmax, vmax);
  motor.move(lastT);

  if (hidReleaseAt && millis() >= hidReleaseAt) { Consumer.release(); Keyboard.releaseAll(); hidReleaseAt = 0; }

  // --- centik takibi + LED (forcing'de de LED guncelle, ama HID biriktirme) ---
  if (mode == M_DETENT && depState == DEP_OFF && !centering) {   // ortalama sirasinda sahte adim uretme
    // HERSEY GORELI: hareket delta'sindan adim (SNAP %60 histerezisi, flip-flop yok). Duvar/sinir yok.
    float sp = detentSpacing();
    float d  = motor.shaft_angle - relAnchor;
    int step = 0;
    if (d >  sp * 0.6f) step = +1;
    else if (d < -sp * 0.6f) step = -1;
    if (step != 0) {
      relAnchor += step * sp;                             // referansi bir centik kaydir
      if (millis() - modeSwitchAt > 150) {
        pendingHid += step; lastUserMove = millis();
        if (barMode()) {                                  // ses/parlaklik: LED bar seviyesi (0..detents-1)
          level = constrain(level + step, 0, detents - 1);
          updateLED(level);
        } else if (appMode == APP_SCROLL) {               // scroll: donen nokta (wrap)
          dispIdx = (dispIdx + step + NLED) % NLED;
          updateLED(dispIdx);
        } else {                                          // space/tabs: aktif index (iyimser; HS teyit eder)
          dispIdx = constrain(dispIdx + step, 0, detents - 1);
          updateLED(dispIdx);
        }
      }
    }
    if (step != 0 && dbgStep)
      Serial.printf("STEP %+d aci=%.1f hiz=%.2f mode=%d\n",
        step, motor.shaft_angle*180.0f/PI, motor.shaft_velocity, (int)appMode);
  }
  // --- PCF8574 buton (I2C1): herhangi butona basinca mod degistir (ses <-> masaustu) ---
  static uint32_t lastBtnPoll = 0;
  static uint8_t  lastBtn = 0xFF;
  if (millis() - lastBtnPoll > 40) {   // 40ms poll = debounce + FOC'a dokunmaz
    lastBtnPoll = millis();
    if (Wire1.requestFrom((uint8_t)0x20, (uint8_t)1) && Wire1.available()) {
      uint8_t b = Wire1.read();
      uint8_t pressed = lastBtn & ~b;   // dusen kenar: onceki 1, simdi 0 = yeni basildi
      if (pressed) {
        // Ilk basili biti bul (0..7) -> haritadan aksiyon. UI 'BTN <bit> <act>' ile esler.
        int bit = 0; while (bit < 8 && !(pressed & (1 << bit))) bit++;
        if (bit < 8) {
          Serial.printf("BTNPRESS %d\n", bit);   // UI 'ogren' bunu yakalar
          doAction(btnAction[bit]);
        }
      }
      lastBtn = b;
    }
  }

  // spaces/tabs'te birikimi tam-supurmeyle sinirla (turlarini KIRPMA, sadece runaway'i engelle)
  if (appMode == APP_SPACES || appMode == APP_TABS) pendingHid = constrain(pendingHid, -detents, detents);
  // biriken adimlari birer birer moda gore HID gonder (ses VEYA masaustu)
  // spaces'te adimlar arasi bekleme: Mission Control animasyonu yetissin
  if (pendingHid != 0 && !hidReleaseAt && !forcing && millis() >= nextStepAt) {
    sendStep(pendingHid);
    pendingHid += (pendingHid > 0) ? -1 : 1;
    nextStepAt = millis() + (appMode == APP_SPACES ? 380 : appMode == APP_TABS ? 70 : 0);
  }

  if (stream) {   // yuksek hizli telemetri: aci_deg hiz tork
    static uint32_t lp = 0;
    if (millis() - lp >= 6) { lp = millis();
      Serial.printf("%.1f %.2f %.2f\n", motor.shaft_angle*180.0f/PI, motor.shaft_velocity, lastT);
    }
  }

  while (Serial.available()) {
    char ch = Serial.read();
    if (ch=='\n'||ch=='\r') { handleLine(rx); rx=""; }
    else { rx += ch; if (rx.length()>40) rx=""; }
  }
}
