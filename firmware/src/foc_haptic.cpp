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
#include "MagneticSensorMT6701SSI.h"
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
uint8_t tftRot = 1;      // ekran yonu (0-3). 3 idi; kasaya ters vidalanmis, 180 cevrildi.
float volSpacing = 0.220f; // 12.6 deg — 25 centik. ARALIK = GUC (tepe kuvvet Kdet*sp/2 = 2.4V).
                           // CENTIGIN GUCUNU ARALIK VERIR: tepe kuvvet = Kdet*sp/2 (testere).
                           // 0.15'te 1.65V idi, 0.327'de 3.6V. Sertligi artirmak guc VERMEZ,
                           // sadece sinir egimini dikleştirip kararsizlik yaratir.  // VOLUME goreli centik araligi (~8.6°; 16 adim ~137° < 300° -> fiziksel araliga sigar)
// MOD GECISI ORTALAMA: mod degisince motor bir kerelik fiziksel ORTAYA doner -> iki yone de yer acilir.
// SABIT hedefe (orta) gider, DEGERE degil -> kovalanan hareketli hedef yok, forcing gibi savasamaz.
static bool     centering = false;
static uint32_t centerStart = 0;
static float    Kcenter = 6.0f;   // ortalama yayi (NAZIK -> kullanici kolayca override eder)

// ================= DEPLOY KASA KILIDI =================
// P2 -> sifre girisi (sag N tik, sol N tik, ... 4 hane) -> dogruysa KOL (sertlesen 120°) -> deploy
enum DeployState { DEP_OFF, DEP_CENTER, DEP_ENTRY, DEP_POS, DEP_LEVER, DEP_RUNNING, DEP_LOCKED };
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
static uint32_t modeSwitchAt = 0;
static bool pcfPresent = false;   // PCF8574 yoksa I2C'yi hic yoklama (hata spam'i seri portu boguyordu) // mod degisiminden sonra kisa guard (aci oynamasi HID'e cevrilmesin)

// MT6701 — SSI (SPI benzeri) arayuz. I2C'den ~20x HIZLI okuma -> FOC dongusu hizlanir,
// yuksek centik sertliginde kararsizlik (salinim) kalkar. Kablolar ayni: A=DO, B=CLK, Z=CSN.
SPIClass sensorSPI(FSPI);                       // ekran HSPI'de, sensore ayri bus

// MT6701 SSI + CRC DOGRULAMA
// Datasheet: 24-bit cerceve = 14-bit aci | 4-bit alan durumu | 6-bit CRC (X^6+X+1).
// Kullandigimiz hazir surucu CRC'yi ATIYORDU. Motor EMI'si okumalari bozunca FOC yanlis
// tork uygulayip saccmaliyordu (olculdu: motor kapali 0 sicrama, acik 672).
// Artik her cerceve CRC ile dogrulaniyor; bozuksa REDDEDILIP son gecerli aci korunuyor.
class MT6701SSI_CRC : public Sensor {
public:
  MT6701SSI_CRC(int cs) : _cs(cs) {}
  void init(SPIClass* spi) {
    _spi = spi;
    pinMode(_cs, OUTPUT); digitalWrite(_cs, HIGH);
    this->Sensor::init();
  }
  float getSensorAngle() override {
    _spi->beginTransaction(_set);
    digitalWrite(_cs, LOW);
    uint8_t b0=_spi->transfer(0), b1=_spi->transfer(0), b2=_spi->transfer(0), b3=_spi->transfer(0);
    digitalWrite(_cs, HIGH);
    _spi->endTransaction();
    uint32_t f = ((uint32_t)b0<<24)|((uint32_t)b1<<16)|((uint32_t)b2<<8)|b3;
    total++;
    // Cerceve basindaki onek bit sayisi bilinmiyor -> CRC'yi tutturan kaymayi OTOMATIK bul, sonra kilitle.
    if (shift < 0) {
      for (int L = 0; L <= 8; L++) {
        uint16_t a =(f >> (18-L)) & 0x3FFF;
        uint8_t  m =(f >> (14-L)) & 0x0F;
        uint8_t  c =(f >> (8-L))  & 0x3F;
        if (crc6((((uint32_t)a)<<4)|m) == c) { shift = L; break; }
      }
      if (shift < 0) { bad++; return _last; }
    }
    uint16_t ang =(f >> (18-shift)) & 0x3FFF;
    uint8_t  mg  =(f >> (14-shift)) & 0x0F;
    uint8_t  crcR=(f >> (8-shift))  & 0x3F;
    if (crc6((((uint32_t)ang)<<4)|mg) != crcR) { bad++; return _last; }
    _last = (float)ang / 16384.0f * _2PI;
    mgStatus = mg;
    return _last;
  }
  int shift = -1;
  uint32_t total = 0, bad = 0;
  uint8_t  mgStatus = 0;
private:
  static uint8_t crc6(uint32_t d18) {          // X^6+X+1, MSB first, 18 bit
    uint8_t c = 0;
    for (int i = 17; i >= 0; i--) {
      uint8_t inb = (d18 >> i) & 1, msb = (c >> 5) & 1;
      c = (uint8_t)((c << 1) & 0x3F);
      if (inb ^ msb) c ^= 0x03;
    }
    return c;
  }
  SPIClass* _spi = nullptr;
  SPISettings _set = SPISettings(500000, MSBFIRST, SPI_MODE2);
  int _cs; float _last = 0;
};

MT6701SSI_CRC sensor(PIN_MT_CSN);

BLDCMotor         motor  = BLDCMotor(MOTOR_POLE_PAIRS);
BLDCDriver3PWM    driver = BLDCDriver3PWM(PIN_MOTOR_A, PIN_MOTOR_B, PIN_MOTOR_C);

enum Mode { M_FREE, M_DETENT, M_SPRING, M_ENDSTOP };
Mode  mode    = M_DETENT;   // acilista centik (sabit kalibrasyon guvenilir) — 'm detent' ile centikleri ac (titreme USB'yi dusurmesin)
int   detents = 17;      // 17 pozisyon = 0..16 ses seviyesi (16 kademe gecisi = macOS)
float Kdet    = 22.0f;   // centik SERTLIGI = sinir egimi. Kararsizligi BU belirler, gucu degil.
float Kspring = 8.0f;    // yay
float Kwall   = 40.0f;   // duvar (sert)
float damping = 0.05f;   // COK KUCUK. Eski degerlerin (0.15-0.30) ucte biri.
                         // Isi: savurma sonrasi cinlamayi sondurmek DEGIL — knob'un iki centik
                         // ARASINDA gelgit yapip takilmasini engellemek. O is icin bu kadari yeter.
                         // Buyutme durtusune diren (27.08, alti olcum:
                         // 0.15->2, 0.35->14, 0.55->47 cirpinti; est modda da ayni).
                         // SimpleFOC'un resmi haptik ornegi de D=0 kullaniyor.
                         // ESKI DEGER 0.15/0.30 idi; sonum ekleme durtusune direnelim.   // hiz sonumleme — DUSUK TUT. Olculdu: 0.15->2 cirpinti, 0.35->14, 0.55->47.
                         // Hiz tahmini gecikmeli oldugu icin yuksek sonum titremeyi SONDURMEZ, BESLER.
float minA = 0, maxA = 1.0f;   // FIZIKSEL duvar acilari (homing ile bulunur)
bool  homed = false;           // homing yapildi mi? Bootta YAPILMIYOR: duvara dayanmak stall = guc
                               // kaynagi CC'ye giriyor + motor isiniyor. Gunluk kullanim (ses/space/
                               // scroll) goreli grid kullandigi icin duvarlari BILMEK ZORUNDA DEGIL.
                               // Sadece deploy modu duvarlara ihtiyac duyar -> ilk girişte yapilir.
float homingV = 4.5f;    // homing itme voltaji (nazik — duvara sert vurmasin)
// --- BURST KAYIT: tam dongu hizinda RAM'e yaz, sonra dok. Seri telemetri 167 Hz'di,
// yani 83 Hz uzeri her seyi TAKMA FREKANS olarak yanlis gosteriyordu. Salinim 50-500 Hz
// bandinda oldugu icin o olcumler gecersizdi. Bu, gercek dongu hizinda kayit alir.
#define BURST_N 2500
static int16_t  bAng[BURST_N];   // aci, 0.01 deg birim
static int16_t  bVel[BURST_N];   // hiz, 0.01 rad/s birim
static int16_t  bTrq[BURST_N];   // tork, 0.001 V birim
static volatile int bIdx = -1;   // -1 = kapali, -2 = TETIK BEKLIYOR
static float bTrigV = 8.0f;      // tetik hizi (rad/s)
static uint32_t bT0 = 0, bT1 = 0;   // bT1 = SON ornegin zamani (dolum bitisi)
float fwdThrK = 0.85f;   // adim/HID tetikleme esigi (x centik araligi). 'F <k>' ile ayarlanir.
int   knobDir = -1;      // kullaniciya donuk cevirme yonu (+1/-1). 'N' ile ters cevrilir.
                         // 31.08: sensor yeniden monte edilince yon tersine dondu.
float centerHyst  = 0.0f;    // haptik merkez histerezisi (x centik araligi).
                             // DENENDI ve KAPATILDI (27.08): olcum lehineydi (sinir gecisi/derece
                             // 0.53 -> 0.13, baskin frekans 47 -> 26 Hz) ama HIS daha kotuydu.
                             // Kod duruyor, 'X <k>' ile acilabilir. Karar hisle verildi.
static float hapCenter = 0;  // mevcut centik merkezi — artik DURUM, anlik hesaplanmiyor
// --- ESTIMATED CURRENT MODU (deneysel) ---
// phase_resistance girilirse SimpleFOC target'i AKIM olarak yorumlar:
//   voltage.q = target*R + voltage_bemf      (BLDCMotor.cpp:426)
//   voltage_bemf = hiz/(KV*sqrt3)/RPM_TO_RADS   (sadece KV girilirse)
// Yani KV girilince geri-EMK ILERI BESLEMEYLE telafi edilir -> tork hizdan bagimsiz
// olur AMA motorun dogal elektriksel sonumu (b_elek = Ke, voltaj biriminde ~0.04)
// da iptal olur. Bizim tek gercek sonumumuz o oldugu icin RISKLI. Test icin.
// Sonum artik SERTLIGE BAGLI olabilir: B = dampRatio * sqrt(Kdet).
// Gerekce: kritik sonum B_krit = 2*zeta*sqrt(K*J) -> sertligin KAREKOKUYLE olcekler.
// dampRatio = 0 -> sabit 'damping' kullanilir (eski davranis).
float relW = 0.10f;      // profil 2: sinir yumusatma penceresi (x centik araligi)
int   detProfile = 0;    // 0 = testere. Sinus (1) denendi: kararli ama tepe kuvvet 1/3.    // 0 = testere (mevcut), 1 = sinus (surekli). 'p <0|1>'
float dampRatio = 0.0f;
float phaseR  = 2.75f;    // OLCULDU: faz-faz 5.51 ohm (uc kombinasyon da ayni) -> yildiz sarim, /2
float motorKV = 140.0f;   // Motor etiketi: 2805, 140KV -> Ke = 1/(KV*sqrt3*RPM_TO_RADS) = 0.0394 V.s/rad
bool  estMode = false;    // KAPALI (kararli ayar bunsuz bulundu). Olculdu: 305 derece uctan uca savurma -> 238ms'de TAM durdu
                          // (son 80ms hiz std = 0.000). Est kapaliyken cok daha az enerjiyle
                          // hic durmuyordu. 'E' ile kapatilabilir.
float revThrK = 0.6f;    // yon degistirme esigi (x centik araligi). 0.6=eski simetrik, 1.0=yeni
float vHalf   = 1.2f;    // sonum devreye girme hizi (rad/s). |v|<<vHalf -> sonum ~0 (durakta vizilti yok)
                         // |v|>>vHalf -> tam sonum (cevirip birakinca cinlamayi yutar)
float deadZone = 0.0f;   // gerekmiyor (sonum 0 iken merkez zaten sakin) // ~0.7 deg olu bolge — L6234'un kaba minimum torku burada devreye girmesin
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
// LED0 ARIZALI (31.08): ilk WS2812'nin ISIK kati olu, ama VERI aktarma kati saglam
// (arkasindaki 15 LED normal yaniyor). Bu yuzden fiziksel index 0 KULLANILMIYOR;
// gosterge 1..NLED-1 arasina yayiliyor. Cip degistirilirse LED_OFF'u 0 yap.
#define LED_OFF   1                 // ilk kullanilabilir fiziksel index
#define NLED_USE  (NLED - LED_OFF)  // kullanilabilir LED sayisi (15)
static inline CRGB& L(int i) { return leds[LED_OFF + ((i % NLED_USE) + NLED_USE) % NLED_USE]; }

static void updateLED(int lv) {
  CRGB base = colBase[appMode], act = colActive[appMode];
  leds[0] = CRGB::Black;   // arizali LED: her zaman kapali
  if (barMode()) {                             // dolan bar: seviyeye kadar aktif renk
    for (int i = 0; i < NLED_USE; i++) L(i) = (i < lv) ? act : base;
  } else if (appMode == APP_SCROLL) {          // SCROLL: donen tek nokta (gorsel geri bildirim)
    for (int i = 0; i < NLED_USE; i++) L(i) = base;
    L(lv) = act;
  } else {                                     // SPACES: n centik ORTADA, esit aralikli, AYRI LED'ler (cakisma yok)
    for (int i = 0; i < NLED_USE; i++) L(i) = base;
    int spacing = ((detents - 1) * 2 <= NLED_USE - 1) ? 2 : 1;   // aralik 2 LED (sigmazsa 1)
    int width   = (detents - 1) * spacing;
    int start   = (int)roundf((NLED_USE - 1) / 2.0f - width / 2.0f);
    for (int k = 0; k < detents; k++) {
      int p = constrain(start + k * spacing, 0, NLED_USE - 1);
      L(p) = (k == lv) ? act : (base + CRGB(0,15,30));           // aktif centik = yesil, digerleri sonuk mavi
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
  if (m == APP_SPACES)     { detents = 3;  Kdet = 22.0f; }   // HS 'SP n idx' ile gunceller
  else if (m == APP_TABS)  { detents = 8;  Kdet = 22.0f; }   // HS 'TAB n idx' ile gunceller
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
static void home();   // ileri bildirim (tembel homing)
static void depEnter() {                       // P2 -> kasa moduna gir
  if (millis() < depLockUntil) { depState = DEP_LOCKED; return; }
  if (!homed) home();                          // TEMBEL homing: duvarlar sadece deploy icin gerekli
  depState = DEP_CENTER; depStatus[0] = 0; depIdleAt = millis();   // once ORTALA -> sifre tiklarina iki yone yer
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
    case DEP_CENTER: {                               // sifre oncesi: knob'u fiziksel ORTAYA al (iki yone yer ac)
      float e = a - midA();
      t = _constrain(-5.0f * e - 0.35f * motor.shaft_velocity, -2.2f, 2.2f);  // NAZIK + DUSUK sonum -> titremez
      // yeterince ortada + DURGUN olunca sifreye gec (hareketliyken gecerse sahte tik olur -> kilit)
      if ((fabsf(e) < 0.13f && fabsf(motor.shaft_velocity) < 0.30f) || now - depIdleAt > 2200) {
        depState = DEP_ENTRY; depResetEntry();       // merkezden basla, tik saymaya gec
      }
      return true;
    }
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
    // --- PROFIL 1: SINUS (surekli, SICRAMA YOK) ---
    // Mevcut testere profilinde tork sinirda aninda isaret degistiriyor:
    //   sinir oncesi +Kdet*sp/2, sonrasi -Kdet*sp/2  ->  Kdet*sp buyuklugunde SICRAMA.
    //   Kdet=22, sp=0.15 rad icin 3.3 V (tavanin %73'u!). Olculen 41-52 Hz salinimin kaynagi bu.
    //   Sonum bunu sonduremez: sonum surekli bir kuvvet, sureksizlige carey degil.
    // Sinus profilinde merkezde 0, ceyrekte tepe, sinirda yine 0 — gercek mekanik centik gibi.
    // Merkez sertligi dtau/dtheta|0 = A*2pi/sp; A = Kdet*sp/(2pi) secilince MERKEZDEKI HIS AYNI
    // kalir ama sinir sureksizligi tamamen kaybolur -> Kdet'i yukseltmek guvenli hale gelir.
    if (detProfile == 1) {
      float A = Kdet * sp / _2PI;
      return -A * sinf(_2PI * (a - relAnchor) / sp);
    }
    // --- PROFIL 2: YUMUSATILMIS TESTERE ---
    // Testerenin tik hissi sinirdaki ANI BIRAKMA'dan geliyor; sinus onu tamamen siliyor
    // (Kdet 44'te bile tepe kuvvet 1.05 V < testere Kdet 22'nin 1.65 V'u).
    // Burada centigin (1-2w) kadarlik kismi BIREBIR testere; son w'lik pencerede kuvvet
    // sifira dogru iniyor. Boylece tepe kuvvet ~Kdet*sp*(0.5-w) korunuyor ama sinirdaki
    // SONSUZ egim, sonlu ve AYARLANABILIR bir egime donusuyor (relW ile).
    // relW=0 -> tam testere (eski davranis), relW=0.5 -> ucgen.
    if (detProfile == 2) {
      float u = (a - relAnchor) / sp;
      u -= floorf(u + 0.5f);                    // [-0.5, 0.5) araligina sar
      float w  = _constrain(relW, 0.001f, 0.49f);
      float uc = 0.5f - w;                      // dogrusal bolgenin sonu
      float au = fabsf(u), sgn = (u > 0) ? 1.0f : -1.0f;
      float f;
      if (au <= uc) f = Kdet * sp * au;                       // birebir testere
      else          f = Kdet * sp * uc * (0.5f - au) / w;     // sifira yumusak inis
      return -sgn * f;
    }
    // MERKEZ SECIMINDE HISTEREZIS (27.08 olcumu).
    // Eskiden: c = relAnchor + roundf((a-relAnchor)/sp)*sp -> merkez tam ortada ANINDA
    // fliplenirdi. Knob sinira yakinken en ufak titresim hedefi BIR TAM CENTIK ileri-geri
    // ziplatiyor; her flipte Kdet*sp buyuklugunde tork basamagi = ENERJI ENJEKSIYONU.
    // Olculdu: sonum 0.35'te 474ms icinde 91 sinir gecisi, salinim 47 Hz'e kilitlenip
    // tork tavanina dayaniyordu. Sonum ARTIRMAK kotulestiriyor cunku knob'u sinir
    // civarinda daha uzun tutuyor. Artik merkez bir DURUM; degismesi icin
    // (0.5+centerHyst)*sp gerekiyor. centerHyst=0 -> eski davranis.
    if (fabsf(a - hapCenter) > 3.0f * sp)                  // grid kaydiysa (mod gecisi) hizala
      hapCenter = relAnchor + roundf((a - relAnchor) / sp) * sp;
    float thr = sp * (0.5f + centerHyst);
    float e = a - hapCenter;
    while (e >  thr) { hapCenter += sp; e -= sp; }
    while (e < -thr) { hapCenter -= sp; e += sp; }
    // OLU BOLGE: centik merkezine cok yakinken tork UYGULAMA. Merkez etrafindaki
    // mikro-salinimi (parmakla supururken sekme) kokten keser. 'z' ile ayarlanir.
    if (fabsf(e) < deadZone) return 0;
    e -= (e > 0 ? deadZone : -deadZone);     // surekli gecis (sicrama yok)
    return -Kdet * e;
  }
  if (mode == M_SPRING) return -Kspring * (a - 0.5f * (minA + maxA));
  return 0;   // M_ENDSTOP/diger: serbest
}

// FOC dongu periyodu. 0 = maksimum hiz (~9600 Hz).
// DIKKAT: cok hizli dongu hiz tahminini GURULTULENDIRIR — hiz = daci/dt, dt kuculdukce
// sensorun tek bit adimi (14 bit) buyuk hiz sicramasi gibi gorunur. Sonum bunu torka cevirir.
static volatile uint32_t focPeriodUs = 0;    // 0 = sinirlama yok (eski davranis)
static volatile float    kickDir   = 1.0f; // tekme yonu (baslangicta bir kez sabitlenir)
static volatile uint32_t kickUntil = 0;   // 'K' testi: rotoru dur, salinim sonme suresini olc
static volatile uint32_t focIters = 0;   // FOC dongu sayaci (hiz olcumu icin)
static void focPause();   // FOC gorevini duraklat (asagida tanimli)
static void focResume();  // ve devam ettir


// Hiza bagli sonum: durakta ~0, harekette tam.
// Neden: SABIT sonum iki isi ayni anda yapamiyor — cinlamayi yutacak kadar buyutunce
// durakta gurultuyu buyutup vizildatiyor (olculdu: b0.6 -> 3.7sn cinlama, b1.0 -> 142 yon degisimi).
// Bu form dissipasyonu sadece gercek hareket varken uyguluyor.

// ================= TAKIP GOZLEMLEYICISI (PLL / tracking observer) =================
// Neden: hiz su an sonlu fark + 1. derece LPF (Tf) ile bulunuyor. LPF gecikme-gurultu
// takasinda kotu: gecikmeyi dusurmek icin kesimi acinca nicemleme gurultusu geliyor.
// Gozlemleyici 2. derece: ayni GECIKMEDE cok daha hizli suzuyor (40 dB/dec vs 20).
//   V_est/V = ki / (s^2 + kp*s + ki),  kp = 2*bw, ki = bw^2  -> kritik sonumlu (zeta=1)
//   Faz gecikmesi = -2*atan(w/bw)
// Ornek 50 Hz'de: LPF(Tf=0.01) -> -72 derece;  gozlemleyici bw=2000 -> -18 derece.
// Kararlilik: kp*dt < 1 sart (ODrive ayni korumayi koyar).
// Kaynak: Sachs "Estimate Encoder Velocity Without Stupid Mistakes" II; ODrive encoder.cpp
float obsBW  = 800.0f;    // rad/s — 'o <bw>'
bool  useObs = true;      // 'O' ile ac/kapa
static float    obsPos = 0, obsVel = 0;
static uint32_t obsTs  = 0;

static inline void obsUpdate(float angle) {
  uint32_t now = micros();
  float dt = (now - obsTs) * 1e-6f;
  obsTs = now;
  if (dt <= 0 || dt > 0.01f) { obsPos = angle; obsVel = 0; return; }   // ilk cagri / uzun bosluk
  float kp = 2.0f * obsBW;
  float ki = obsBW * obsBW;
  if (kp * dt > 0.8f) { kp = 0.8f / dt; ki = 0.25f * kp * kp; }        // kararlilik korumasi
  float d = angle - obsPos;
  obsPos += dt * (obsVel + kp * d);
  obsVel += dt * ki * d;
}
static inline float shaftVel() { return useObs ? obsVel : motor.shaft_velocity; }

static inline float dampTorque(float vel) {
  float b = (dampRatio > 0) ? dampRatio * sqrtf(Kdet) : damping;   // sertlige bagli VEYA sabit
  float av = fabsf(vel);
  return b * vel * (av / (av + vHalf));
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
  minA = fmin(w1, w2); maxA = fmax(w1, w2); homed = true;
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
  else if (c=='u') { tftRot = (uint8_t)(v.toInt() & 3); tft.setRotation(tftRot); tft.fillScreen(ST77XX_BLACK); Serial.printf("ekran yonu=%d (ekran bir sonraki degisimde cizilir)\n", tftRot); }
  else if (c=='n') {   // EKRAN TESTI: kablolamayi/rotasyonu dogrula
    Serial.println("Ekran testi: renk barlari + metin");
    tft.fillScreen(ST77XX_BLACK);
    const uint16_t cols[6] = { ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE,
                               ST77XX_YELLOW, ST77XX_CYAN, ST77XX_MAGENTA };
    for (int i = 0; i < 6; i++) tft.fillRect(i*53, 0, 53, 90, cols[i]);
    tft.drawRect(0, 0, 320, 240, ST77XX_WHITE);           // cerceve: tam sigmali
    tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3);
    tft.setCursor(10, 110); tft.print("SMARTKNOB");
    tft.setTextSize(2);
    tft.setCursor(10, 150); tft.print("sol ust = KIRMIZI");
    tft.setCursor(10, 175); tft.print("cerceve tam gorunmeli");
    tft.setCursor(10, 205); tft.print("320x240 yatay");
    Serial.println("Beklenen: 6 renk bari, beyaz cerceve kenarlara tam otursun.");
  }
  else if (c=='s') { volSpacing = v.toFloat(); Serial.printf("volSpacing=%.3f rad (%.1f deg) -> sinus tepe kuvvet %.2f V\n", volSpacing, volSpacing*57.3f, Kdet*volSpacing/6.283f); }
  else if (c=='d') { detents = max(1, (int)v.toInt()); Serial.printf("detents=%d\n", detents); }
  else if (c=='k') { Kdet = v.toFloat(); Serial.printf("Kdet=%.2f\n", Kdet); }
  else if (c=='b') { damping = v.toFloat(); Serial.printf("damping=%.3f\n", damping); }
  else if (c=='z') { deadZone = v.toFloat(); Serial.printf("deadZone=%.4f rad (%.2f deg)\n", deadZone, deadZone*180.0f/PI); }
  else if (c=='h') { homingV = v.toFloat(); Serial.printf("homingV=%.2f\n", homingV); }
  else if (c=='f') { motor.LPF_velocity.Tf = v.toFloat(); Serial.printf("LPF_Tf=%.3f\n", motor.LPF_velocity.Tf); }
  else if (c=='S') { stream = !stream; Serial.printf("stream=%d\n", stream); }
  else if (c=='D') { dbgStep = !dbgStep; Serial.printf("dbgStep=%d\n", dbgStep); }
  else if (c=='H') { focPause(); home(); focResume(); }
  else if (c=='W') {   // SUREKLI LED yenileme (15 sn) — pede dokunarak test icin
    Serial.println("LED surekli yenileniyor (15 sn). Veri kablosunu pedlere dokundurup izle.");
    uint32_t t0 = millis();
    while (millis() - t0 < 15000) {
      fill_solid(leds, NLED, CRGB(120,120,120));
      FastLED.show();
      delay(20);   // ~50 Hz yenileme
    }
    Serial.println("bitti");
  }
  else if (c=='Z') {   // GPIO4 KARE DALGA: multimetreyle pin+kablo testi (1 Hz, 10 sn)
    Serial.println("GPIO4 yavas yanip sonuyor (10 sn). DIN ucunda DC volt olc:");
    Serial.println("  0V <-> 3.3V arasi gidip geliyorsa PIN ve KABLO SAGLAM.");
    Serial.println("  sabit 0V ise kablo kopuk/yanlis pin. sabit 3.3V ise kisa devre.");
    pinMode(PIN_RGB, OUTPUT);
    for (int i = 0; i < 20; i++) { digitalWrite(PIN_RGB, i & 1); delay(500); }
    digitalWrite(PIN_RGB, LOW);
    Serial.println("bitti — LED'leri tekrar surmek icin karti RESETLE");
  }
  else if (c=='L') { fill_solid(leds, NLED, CRGB(120,120,120)); FastLED.show(); Serial.println("LED test: hepsi beyaz"); }
  else if (c=='w') { wallMargin = v.toFloat() * PI / 180.0f; Serial.printf("wallMargin=%.1f deg\n", wallMargin*180.0f/PI); }
  else if (c=='W') { Kwall = v.toFloat(); Serial.printf("Kwall=%.1f\n", Kwall); }
  else if (c=='P') {   // pole pairs degistir + yeniden kalibre (dogru pp'yi bulmak icin)
    int pp = v.toInt();
    if (pp > 0) {
      motor.pole_pairs = pp;
      Serial.printf("pole_pairs=%d -> ZORLA yeniden kalibrasyon...\n", pp);
      focPause();
      motor.move(0);
      // Sabit kodlu degerleri TEMIZLE, yoksa SimpleFOC kalibrasyonu ATLAR.
      // Sensor yeniden monte edildiginde (braket degisimi) eski offset gecersizdir:
      // yanlis elektriksel aci -> tork yanlis yonde -> vizilti + bozuk centik.
      motor.zero_electric_angle = NOT_SET;
      motor.sensor_direction    = Direction::UNKNOWN;
      motor.initFOC();
      Serial.printf("bitti: offset=%.3f dir=%d\n", motor.zero_electric_angle, motor.sensor_direction);
      focResume();
    }
  }
  else if (c=='m') {
    if (v=="free") mode=M_FREE; else if (v=="detent") mode=M_DETENT;
    else if (v=="spring") mode=M_SPRING; else if (v=="endstop") mode=M_ENDSTOP;
    Serial.printf("mode=%s\n", modeName());
  }
  else if (c=='C') { Serial.printf("CRC: toplam=%lu bozuk=%lu (%%%.2f)  kayma=%d  alan=%u\n", sensor.total, sensor.bad, sensor.total?100.0*sensor.bad/sensor.total:0.0, sensor.shift, sensor.mgStatus); sensor.total=sensor.bad=0; }
  else if (c=='I') { sensor.update(); Serial.printf("SENSOR aci=%.2f rad (%.1f deg)\n", sensor.getAngle(), sensor.getAngle()*180.0f/PI); }
  else if (c=='T') { focPeriodUs = v.toInt(); Serial.printf("focPeriod=%lu us (%s)\n", (unsigned long)focPeriodUs, focPeriodUs ? String(1000000.0f/focPeriodUs,0).c_str() : "max"); }
  else if (c=='B') {   // burst kayit baslat
    if (v.length()) bTrigV = v.toFloat();
    bIdx = -2;
    Serial.printf("BURST silahli — tetik hizi %.1f rad/s. SAVUR.\n", bTrigV);
  }
  else if (c=='G') {   // burst dok
    uint32_t dur = bT1 - bT0;   // dolum suresi (bekleme haric)
    int n = bIdx < 0 ? 0 : bIdx;
    Serial.printf("BURST n=%d sure_us=%lu hz=%.0f\n", n, (unsigned long)dur, n ? n*1e6f/dur : 0);
    for (int i = 0; i < n; i++) Serial.printf("%d %d %d\n", bAng[i], bVel[i], bTrq[i]);
    Serial.println("BURST son");
    bIdx = -1;
  }
  else if (c=='o') { obsBW = v.toFloat(); Serial.printf("obsBW=%.0f rad/s (%.0f Hz kesim; 50Hz'de faz %.0f derece)\n", obsBW, obsBW/6.283f, -2*atanf(314.0f/obsBW)*57.3f); }
  else if (c=='O') { useObs = !useObs; Serial.printf("hiz kaynagi = %s\n", useObs ? "GOZLEMLEYICI" : "SimpleFOC LPF"); }
  else if (c=='j') { relW = v.toFloat(); Serial.printf("relW=%.3f (tepe kuvvet ~%.2f V)\n", relW, Kdet*detentSpacing()*(0.5f-relW)); }
  else if (c=='F') { fwdThrK = v.toFloat(); Serial.printf("fwdThrK=%.2f (HID centigin %%%.0f'inde tetiklenir)\n", fwdThrK, fwdThrK*100); }
  else if (c=='N') { knobDir = -knobDir; Serial.printf("knobDir=%+d (cevirme yonu %s)\n", knobDir, knobDir>0?"NORMAL":"TERS"); }
  else if (c=='p') { detProfile = v.toInt(); Serial.printf("detProfile=%d (%s)\n", detProfile, detProfile ? "SINUS - surekli" : "testere - mevcut"); }
  else if (c=='r') { dampRatio = v.toFloat(); Serial.printf("dampRatio=%.4f -> Kdet=%.0f'de etkin sonum=%.3f (0=sabit '%s')\n", dampRatio, Kdet, dampRatio>0?dampRatio*sqrtf(Kdet):damping, "b"); }
  else if (c=='e') { phaseR = v.toFloat(); if (estMode) motor.phase_resistance = phaseR; Serial.printf("phaseR=%.2f ohm\n", phaseR); }
  else if (c=='Q') { motorKV = v.toFloat(); if (estMode) motor.KV_rating = motorKV; Serial.printf("KV=%.0f\n", motorKV); }
  else if (c=='E') {
    estMode = !estMode;
    if (estMode) {
      motor.phase_resistance = phaseR;
      motor.KV_rating        = motorKV;
      motor.current_limit    = motor.voltage_limit / phaseR;
    } else {
      motor.phase_resistance = NOT_SET;
      motor.KV_rating        = NOT_SET;
    }
    Serial.printf("estMode=%d  R=%.2f KV=%.0f  Ke=%.4f V.s/rad  (ilim=%.2f A)\n",
      estMode, phaseR, motorKV, 1.0f/(motorKV*1.732f*0.10472f), motor.voltage_limit/phaseR);
  }
  else if (c=='X') { centerHyst = v.toFloat(); Serial.printf("centerHyst=%.2f (merkez %.2f x centik gecince degisir)\n", centerHyst, 0.5f+centerHyst); }
  else if (c=='Y') { revThrK = v.toFloat(); Serial.printf("revThrK=%.2f (yon degistirme esigi = %.2f x centik)\n", revThrK, revThrK); }
  else if (c=='V') { vHalf = v.toFloat(); Serial.printf("vHalf=%.2f rad/s\n", vHalf); }
  else if (c=='K') {   // TEKME testi: duvardan uzaga sabit darbe, sonra serbest birak
    relAnchor = motor.shaft_angle;
    kickDir   = (motor.shaft_angle > midA()) ? -1.0f : 1.0f;   // yon BIR KEZ sabitlenir
    kickUntil = millis() + (v.length() ? v.toInt() : 60);
    Serial.printf("KICK yon=%+.0f\n", kickDir);
  }
  else if (c=='R') {   // FOC dongu hizi (Hz) — 500ms ornek
    uint32_t n0 = focIters, t0 = millis();
    delay(500);
    Serial.printf("FOC dongu = %lu Hz\n", (unsigned long)((focIters - n0) * 1000UL / (millis() - t0)));
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
    if (depState == DEP_CENTER) {
      tft.setTextColor(tft.color565(255,180,0)); tft.setTextSize(3); tft.setCursor(20, 90); tft.print("ORTALANIYOR");
    } else if (depState == DEP_LOCKED) {
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


// ================= FOC GOREVI (core 0'da TEK basina) =================
// Neden: ekran SPI'i, HID, buton okumasi ve seri yazdirma FOC'un arasina girince
// hiz tahmini gecikiyor ve sonum titreme enjekte ediyor. FOC'u ayirinca
// donguyu hicbir sey bolmuyor. (Orijinal SmartKnob de boyle yapiyor.)
// FOC ayri cekirdekte mi? 0 = ESKI davranis (loop() icinde, kanitlanmis his).
// 1 = core 0'da ayri gorev. Deney: ayri cekirdek + yuksek dongu hizi TITREME getirdi.
#define FOC_SEPARATE_CORE 0
static TaskHandle_t   focTaskH = NULL;
static volatile bool  focRun   = true;    // false -> gorev duraklar
static volatile bool  focIdle  = false;   // gorev gercekten durakladi mi

static void focPause() {                  // homing/kalibrasyon oncesi: motoru loop'a devret
  if (!focTaskH) return;
  focRun = false;
  while (!focIdle) delay(1);
}
static void focResume() {
  if (!focTaskH) return;
  relAnchor = motor.shaft_angle;          // bayat referansla jerk olmasin
  focRun = true;
}

static void focStep() {
    if (focPeriodUs) {                       // sabit periyot: duzenli dt -> temiz hiz tahmini
      static uint32_t nextUs = 0;
      uint32_t now = micros();
      if ((int32_t)(nextUs - now) > 0) return;
      nextUs = now + focPeriodUs;
    }
    focIters++;
    motor.loopFOC();
    obsUpdate(motor.shaft_angle);
    if (millis() < kickUntil) {   // TEKME: sabit tork darbesi, sonra birak
      lastT = kickDir * motor.voltage_limit;
      motor.move(lastT);
      return;
    }
  // --- tork: normal haptik VEYA Mac senkronu (zorla-git) ---
    // Yaklasim: SmartKnob mantigi — KRITIK SONUMLU 2. derece setpoint (S-egrisi, jerk yok) +
    //           motorun setpoint'i takibi + DEAD ZONE (settle buzz'i yok) + VELOCITY CUTOFF (runaway yok).
    float t;
    // Deploy'dan (herhangi yoldan) cikinca goreli grid'i TAZELE -> bayat referansla jerk/titreme olmasin
    static DeployState prevDep = DEP_OFF;
    if (prevDep != DEP_OFF && depState == DEP_OFF) { relAnchor = motor.shaft_angle; centering = false; }
    prevDep = depState;
    if (depUpdate(t)) {
      // DEPLOY modu (kasa kilidi/kol) -> torku o uretir, normal haptik+HID atlanir
    } else if (fabsf(shaftVel()) > 40.0f) {   // VELOCITY CUTOFF (SmartKnob): runaway yok
      t = 0;
    } else if (centering) {
      // MOD GECISI ORTALAMA: sabit hedef=fiziksel orta. NAZIK yay + GUCLU SONUM (overshoot/titreme YOK).
      float e = motor.shaft_angle - midA();
      t = -Kcenter * e;
      t -= 0.35f * shaftVel();   // DUSUK sonum: gurultu buyutup titretmez (yuksek sonum titreme enjekte eder)
      bool reached     = fabsf(e) < 0.05f && fabsf(shaftVel()) < 0.5f;   // ortaya oturdu
      bool pushingAway = (e * shaftVel()) > 2.0f;                         // kullanici merkezden UZAGA itiyor -> DOKUNDU
      if (reached || pushingAway || millis() - centerStart > 700) {
        centering = false;
        relAnchor = motor.shaft_angle;   // goreli grid'i knob'un su anki yerinden baslat
        Serial.printf("ORTALAMA bitti (%s) aci=%.1f\n",
          reached ? "oturdu" : pushingAway ? "kullanici-dokundu" : "timeout", motor.shaft_angle*180.0f/PI);
      }
    } else {
      // TEK yol: yerel deterministik haptik. Sync ARTIK motoru surmez -> titreme/atlama yok.
      t = hapticTorque(motor.shaft_angle);
      t -= dampTorque(shaftVel());   // dampTorque icinde etkin sonum secilir
    }
    float vmax = centering ? 2.5f : motor.voltage_limit;   // ortalama NAZIK (dusuk V, savasamaz)
    lastT = _constrain(t, -vmax, vmax);
    motor.move(estMode ? lastT / phaseR : lastT);   // est modda target = AKIM
    if (bIdx == -2 && fabsf(motor.shaft_velocity) > bTrigV) { bIdx = 0; bT0 = micros(); }   // tetik
    if (bIdx >= 0 && bIdx < BURST_N) {   // kayit (tam dongu hizinda)
      int i = bIdx;
      bAng[i] = (int16_t)_constrain(motor.shaft_angle*18000.0f/PI, -32000, 32000);
      bVel[i] = (int16_t)_constrain(shaftVel()*100.0f,             -32000, 32000);
      bTrq[i] = (int16_t)_constrain(lastT*1000.0f,                 -32000, 32000);
      bIdx = i + 1; bT1 = micros();
    }
}

static void focTask(void *pv) {
  for (;;) {
    if (!focRun) { focIdle = true; vTaskDelay(1); continue; }
    focIdle = false;
    focStep();
  }
}

void setup() {
  // EN ILK IS: motor PWM hatlarini LOW'a cek.
  // EN pini 3V3'e sabit oldugu icin L6234 reset boyunca AKTIF; IN hatlari bosta kalinca
  // cikislar tanimsiz olup motora DC basiyor (guc kaynagi CC'ye giriyor, ~1-2sn).
  // Uc IN de LOW = uc alt DMOS acik = motor uclari GND'ye kisa = akim YOK, guvenli fren.
  // KALICI COZUM DONANIMDA: IN1/IN2/IN3'e 10k pull-down (GND'ye). Bu satirlar sadece
  // firmware baslayana kadarki pencereyi kisaltir; ROM boot suresini kapatamaz.
  pinMode(PIN_MOTOR_A, OUTPUT); digitalWrite(PIN_MOTOR_A, LOW);
  pinMode(PIN_MOTOR_B, OUTPUT); digitalWrite(PIN_MOTOR_B, LOW);
  pinMode(PIN_MOTOR_C, OUTPUT); digitalWrite(PIN_MOTOR_C, LOW);

  Serial.begin(115200);
  delay(800);

  // FastLED'i USB/FOC'tan ONCE baslat
  FastLED.addLeds<WS2812B, PIN_RGB, GRB>(leds, NLED);
  FastLED.setBrightness(50);
  fill_solid(leds, NLED, CRGB(60,60,60)); FastLED.show();   // acilis testi: beyaz

  Serial.println();
  Serial.println("SmartKnob — CLOSED-LOOP FOC + haptik");

  // Sensor: SSI (CLK=GPIO6, DO=GPIO5, CSN=GPIO7). MOSI kullanilmiyor (-1).
  sensorSPI.begin(PIN_I2C_SCL, PIN_I2C_SDA, -1, PIN_MT_CSN);
  sensor.init(&sensorSPI);
  Serial.printf("MT6701 SSI: CLK=%d DO=%d CSN=%d\n", PIN_I2C_SCL, PIN_I2C_SDA, PIN_MT_CSN);

  motor.linkSensor(&sensor);

  driver.voltage_power_supply = MOTOR_SUPPLY_V;   // 12V
  driver.voltage_limit        = 8.0f;   // align 7V'a izin ver
  driver.pwm_frequency        = 20000;  // 20kHz: isitilebilir ugultu azalir (L298N icin sinirda, isiyi izle)
  if (!driver.init()) { Serial.println("HATA driver.init"); return; }
  motor.linkDriver(&driver);

  motor.controller    = MotionControlType::torque;   // tork = voltaj (akim sensoru yok)
  motor.voltage_limit = 5.0f;                         // haptik tork tavani (L6234 ~1.8V dususu hesaba katilmis)
  // Kalibrasyon SABIT kodlu (offset+yon asagida) -> hizalamaya yuksek voltaj GEREKMIYOR.
  // 5V idi: bootta 1-2sn boyunca motora DC basip guc kaynagini CC'ye sokuyordu.
  motor.voltage_sensor_align = 1.0f;
  motor.velocity_limit = 100;
  // --- Hiz tahmini (27.08 arastirma sonrasi) ---
  // 1) min_elapsed_time: SimpleFOC Sensor::getVelocity() icinde
  //      if (Ts < min_elapsed_time) return velocity;   // ESKI degeri dondurur
  //    Varsayilan 100us. 9600 Hz dongude periyot 104us -> tam sinirda; bazi turlarda
  //    100us'nin altina dusup BAYAT hiz donuyordu. 500us'ye cekince hem tuzak kapaniyor
  //    hem hiz daha uzun pencerede hesaplanip kuantizasyon gurultusu ~5 kat azaliyor.
  //    (14 bit -> 1 bit adimi = 2pi/16384 = 3.83e-4 rad; hiz kuantumu = adim/pencere)
  sensor.min_elapsed_time = 0.0005f;
  // 2) Tf: 0.03s -> kesim 5.3 Hz, ~30ms faz gecikmesi. Gecikmis hiz ile yapilan sonum
  //    enerji sondurmek yerine enjekte ediyor. 0.01s -> kesim 16 Hz, ~10ms.
  //    Pasiflik acisindan guvenli: voltaj biriminde b_elektriksel ~= Ke (0.04-0.07),
  //    gereken ise KT/2 + B*T/(2Tf+T) ~= 0.003 -> hala 15+ kat marj.
  motor.LPF_velocity.Tf = 0.01f;
  motor.useMonitoring(Serial);                        // initFOC ayrintilarini goster
  motor.init();

  // KALIBRASYON OTOMATIK: initFOC her acilista offset+yonu kendi bulur (rewiring'e dayanikli).
  // (Motor kisaca oynar ~1s. Faz sirasi/kablo degisirse dogru degerler otomatik cikar.)
  // SABIT KALIBRASYON: otomatik hizalama guvenilmezdi (bazen PP check fail -> FOC olu, centik yok).
  // Iki temiz olcumun degeri: offset 0.214 ve 0.240, yon CCW -> ortalama 0.227.
  // Boylece acilista motor OYNAMAZ ve her acilis AYNI his. ('P 7' ile otomatige donulebilir.)
  // 31.08: braket yenilendi, sensor yeniden monte edildi -> ESKI degerler (0.227/CCW)
  // GECERSIZ kaldi. Yanlis elektriksel aci = tork yanlis yonde = vizilti + bozuk centik.
  // Uc ardisik 'P 7' kalibrasyonu: 1.304 / 1.304 / 1.315 (yayilim 0.011), yon CW.
  // KURAL: sensor/braket her sokuldugunde 'P 7' ile yeniden kalibre et ve buraya yaz.
  motor.zero_electric_angle = 1.308f;
  motor.sensor_direction    = Direction::CW;
  Serial.println("initFOC (SABIT kalibrasyon: offset=1.308 dir=CW)...");
  motor.initFOC();
  if (estMode) {   // estimated current: target = AKIM, voltage.q = target*R + geri-EMK telafisi
    motor.phase_resistance = phaseR;
    motor.KV_rating        = motorKV;
    motor.current_limit    = motor.voltage_limit / phaseR;
    Serial.printf("EST modu ACIK: R=%.2f ohm KV=%.0f -> Ke=%.4f V.s/rad\n",
                  phaseR, motorKV, 1.0f/(motorKV*1.732f*0.10472f));
  }
  Serial.printf("initFOC bitti. zero_offset=%.3f dir=%d  (sabit kod icin not al)\n",
                motor.zero_electric_angle, motor.sensor_direction);

  // Bootta homing YOK (stall = CC + isinma). Nominal aralik: olculen ~318 derece, mevcut konum ortada varsayilir.
  // Gercek duvarlar ilk deploy girisinde (veya 'H' komutuyla) bulunur.
  { float a0 = motor.shaft_angle, half = 159.0f * PI / 180.0f;
    minA = a0 - half; maxA = a0 + half;
    Serial.println("Homing ATLANDI (bootta stall yok). Deploy'a girince veya 'H' ile yapilir."); }

  // PCF8574 buton genisletici (I2C1 AYRI bus, AS5600'u yavaslatmaz)
  Wire1.begin(PIN_I2C1_SDA, PIN_I2C1_SCL, 100000);
  Wire1.beginTransmission(0x20);
  bool pcf = (Wire1.endTransmission() == 0);
  pcfPresent = pcf;
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
  tft.init(240, 320); tft.setRotation(tftRot);   // YATAY 320x240 ('u <0-3>' ile cevrilir)
  drawScreen();
  xTaskCreatePinnedToCore(displayTask, "disp", 4096, NULL, 1, NULL, FOC_SEPARATE_CORE ? 1 : 0);

  // FOC: core 0'da tek basina, yuksek oncelik. Tight loop oldugu icin core 0 WDT kapali.
#if FOC_SEPARATE_CORE
  disableCore0WDT();
#endif
#if FOC_SEPARATE_CORE
  xTaskCreatePinnedToCore(focTask, "foc", 4096, NULL, 5, &focTaskH, 0);
#else
  (void)focTask;   // loop() icinden focStep() cagriliyor
#endif

  Serial.println("HAZIR. Komut: v m d k b f H S VOL<n> ?  | HID+LED+Mac senkron");
}

void loop() {
#if !FOC_SEPARATE_CORE
  focStep();
#endif

  // Aciliste bir kez: HS'e "volume modu" bildir (F16) -> HS guncel Mac sesini yollar (boot sync)
  static bool bootSynced = false;
  if (!bootSynced && millis() > 2500) {   // USB enumere olsun diye bekle
    bootSynced = true;
    Keyboard.press(KEY_F16); delay(5); Keyboard.releaseAll();
  }


  if (hidReleaseAt && millis() >= hidReleaseAt) { Consumer.release(); Keyboard.releaseAll(); hidReleaseAt = 0; }

  // --- centik takibi + LED (forcing'de de LED guncelle, ama HID biriktirme) ---
  if (mode == M_DETENT && depState == DEP_OFF && !centering) {   // ortalama sirasinda sahte adim uretme
    // HERSEY GORELI: hareket delta'sindan adim (SNAP %60 histerezisi, flip-flop yok). Duvar/sinir yok.
    float sp = detentSpacing();
    float d  = motor.shaft_angle - relAnchor;
    int step = 0;
    // YON DUYARLI HISTEREZIS (27.08 olcumu):
    // Simetrik 0.6*sp esigi ile adim attiktan sonra knob yeni merkeze gore -0.4*sp'de
    // kaliyor; ters esik -0.6*sp oldugu icin sadece 0.2*sp (=1.7 deg) geri hareket
    // SAHTE bir ters adim uretiyordu. Supurmeyi bitirirken elin dogal geri gelisi
    // her seferinde bunu tetikliyordu -> "sesi 5 artir 1 azalt" + hissedilen atlama.
    // Cozum: ayni yonde devam UCUZ, yon degistirmek PAHALI.
    // 'Y' komutu ile ESKI simetrik davranisa donulebilir (A/B karsilastirma icin).
    static int lastStepDir = 0;
    // fwdThrK: adimin (ve HID'in) hangi noktada tetiklenecegi, centik araliginin kati.
    // 0.5'te haptik merkez zaten atliyor, 1.0'da knob yeni centige OTURUYOR.
    // 0.6 ise komut, tik tamamlanmadan gidiyordu -> "erken" hissi (31.08).
    // 0.85 = tik neredeyse bitmisken tetikle.
    float thrFwd = sp * fwdThrK;                     // ayni yonde devam
    float thrRev = sp * revThrK;                     // yon degistir ('Y <k>' ile ayarlanir; 0.6=eski simetrik)
    float thrP = (lastStepDir >= 0) ? thrFwd : thrRev;   // ileri icin esik
    float thrN = (lastStepDir <= 0) ? thrFwd : thrRev;   // geri icin esik
    if (d >  thrP) step = +1;
    else if (d < -thrN) step = -1;
    if (step != 0) lastStepDir = step;
    if (step != 0) {
      relAnchor += step * sp;                             // referansi bir centik kaydir (FIZIKSEL yon)
      // Kullaniciya donuk yon AYRI: 31.08'de sensor yeniden monte edilince
      // sensor_direction CCW -> CW dondu ve cevirme yonu tersine gecti. FOC
      // kalibrasyonu (olculmus deger) DOGRU, o yuzden ona dokunmuyoruz; sadece
      // sayim/HID/LED yonunu ceviriyoruz. 'Y' degil, 'N' komutu ile degistirilir.
      int uStep = step * knobDir;
      if (millis() - modeSwitchAt > 150) {
        pendingHid += uStep; lastUserMove = millis();
        if (barMode()) {                                  // ses/parlaklik: LED bar seviyesi (0..detents-1)
          level = constrain(level + uStep, 0, detents - 1);
          updateLED(level);
        } else if (appMode == APP_SCROLL) {               // scroll: donen nokta (wrap)
          dispIdx = (dispIdx + uStep + NLED) % NLED;
          updateLED(dispIdx);
        } else {                                          // space/tabs: aktif index (iyimser; HS teyit eder)
          dispIdx = constrain(dispIdx + uStep, 0, detents - 1);
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
  if (pcfPresent && millis() - lastBtnPoll > 40) {   // 40ms poll = debounce + FOC'a dokunmaz
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
