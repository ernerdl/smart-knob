// SmartKnob firmware — v0.3: Konumsal mod + cift-yon protokol (MOCK aci)
// ---------------------------------------------------------------------------
// Iki tur aksiyon:
//   - DURUMSUZ/RELATIF (ses/medya): detents=0 -> her adimda cw/ccw tusu, duvar yok.
//   - DURUMLU/ABSOLUT (sekme/liste): detents=N -> knob index tutar (0..N-1),
//       uclarda DUVAR, index degisince cw/ccw tusu DIREKT HID'den gider.
// Host (HS) dis degisimde `POS <i>` ile knob'u sessizce senkronlar (drift'siz).
//
// Komutlar (CDC / Serial Monitor):
//   +           mock aci +STEP (ince)      -           -STEP
//   >           bir tam centik CW          <           bir tam centik CCW
//   *           tik (click tusu)           ?           durum
//   (hareket komutlari SADECE +-><* karakterlerinden olusan satirda; ">>>>" = 4 CW)
//   POS <i>     index'i i yap (tus basmadan senkron)
//   PROFILE mode=<> detents=<> pos=<> color=<> cw=<> ccw=<> click=<>
//     mode  : detent | fine | spring | endstop
//     keys  : VOLUP VOLDN MUTE PLAY NEXT PREV  (medya, consumer control)
//             CTRL+TAB  CTRL+SHIFT+TAB  CTRL+RIGHT  RIGHT LEFT UP DOWN TAB
//             F16 F17 F18  tek harf(a-z)  NONE      (klavye)
//   ornek: PROFILE mode=detent detents=5 pos=0 color=1DB954 cw=CTRL+TAB ccw=CTRL+SHIFT+TAB click=NONE

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>

USBHIDKeyboard        Keyboard;
USBHIDConsumerControl Consumer;

// ---------------- Profil ----------------
enum HapticMode { MODE_DETENT, MODE_FINE, MODE_SPRING, MODE_ENDSTOP };

struct Profile {
  HapticMode mode = MODE_FINE;
  int   detents   = 0;        // 0 = durumsuz/relatif; >0 = konumsal (index'li)
  int   index     = 0;        // mevcut absolut pozisyon (detents>0 ise)
  uint32_t color  = 0xFFFFFF;
  String cwKey    = "VOLUP";
  String ccwKey   = "VOLDN";
  String clickKey = "MUTE";
};
Profile prof;

// ---------------- Mock durum ----------------
float mockAngle = 0.0f;
const float STEP        = 2.0f;   // ince adim (derece)
const float DEG_PER_DET = 12.0f;  // bir centigin acisi
int   lastFreeIdx = INT32_MIN;    // durumsuz modda adim sayaci

static float rangeMax() { return prof.detents > 0 ? (prof.detents - 1) * DEG_PER_DET : 1e6f; }
static float rangeMin() { return 0.0f; }

// ---------------- Tus sozlugu ----------------
static bool sendMedia(const String& t) {
  uint16_t u;
  if      (t == "VOLUP") u = CONSUMER_CONTROL_VOLUME_INCREMENT;
  else if (t == "VOLDN") u = CONSUMER_CONTROL_VOLUME_DECREMENT;
  else if (t == "MUTE")  u = CONSUMER_CONTROL_MUTE;
  else if (t == "PLAY")  u = CONSUMER_CONTROL_PLAY_PAUSE;
  else if (t == "NEXT")  u = CONSUMER_CONTROL_SCAN_NEXT;
  else if (t == "PREV")  u = CONSUMER_CONTROL_SCAN_PREVIOUS;
  else return false;
  Consumer.press(u); delay(8); Consumer.release();
  return true;
}

static uint8_t keyCode(const String& k) {
  if (k == "TAB")   return KEY_TAB;
  if (k == "RIGHT") return KEY_RIGHT_ARROW;
  if (k == "LEFT")  return KEY_LEFT_ARROW;
  if (k == "UP")    return KEY_UP_ARROW;
  if (k == "DOWN")  return KEY_DOWN_ARROW;
  if (k == "F16")   return KEY_F16;
  if (k == "F17")   return KEY_F17;
  if (k == "F18")   return KEY_F18;
  if (k.length() == 1) return (uint8_t)tolower(k[0]);
  return 0;
}
static uint8_t modCode(const String& m) {
  if (m == "CTRL")  return KEY_LEFT_CTRL;
  if (m == "SHIFT") return KEY_LEFT_SHIFT;
  if (m == "ALT")   return KEY_LEFT_ALT;
  if (m == "CMD" || m == "GUI") return KEY_LEFT_GUI;
  return 0;
}

// "CTRL+SHIFT+TAB" -> modifier'lar + tus. Medya token'i ise consumer'a gider.
static void sendToken(const String& token) {
  if (token.length() == 0 || token == "NONE") return;
  if (sendMedia(token)) { Serial.printf("HID(media) -> %s\n", token.c_str()); return; }

  uint8_t mods[4]; int nm = 0; uint8_t key = 0;
  int start = 0;
  while (true) {
    int p = token.indexOf('+', start);
    String part = (p < 0) ? token.substring(start) : token.substring(start, p);
    if (p < 0) { key = keyCode(part); break; }
    uint8_t mc = modCode(part); if (mc && nm < 4) mods[nm++] = mc;
    start = p + 1;
  }
  for (int i = 0; i < nm; i++) Keyboard.press(mods[i]);
  if (key) Keyboard.press(key);
  delay(8); Keyboard.releaseAll();
  Serial.printf("HID(key) -> %s\n", token.c_str());
}

// ---------------- Haptik tork (his) ----------------
const float K_DETENT = 0.15f, K_WALL = 0.80f, K_SPRING = 0.05f;

static float computeTorque(float a) {
  if (prof.detents > 0) {
    // konumsal: en yakin centige cek + uclarda duvar
    if (a <= rangeMin()) return  K_WALL * 2.0f;   // sol duvar
    if (a >= rangeMax()) return -K_WALL * 2.0f;   // sag duvar
    float center = roundf(a / DEG_PER_DET) * DEG_PER_DET;
    return -K_DETENT * (a - center);
  }
  if (prof.mode == MODE_SPRING) return -K_SPRING * (a - 0.0f);
  return 0.0f; // fine/durumsuz
}

// ---------------- Aci degisince ----------------
static int clampIndex(float a) {
  int i = (int)lroundf(a / DEG_PER_DET);
  return constrain(i, 0, prof.detents - 1);
}

static void onAngleChanged(bool silent) {
  if (prof.detents > 0) {
    // duvar: aci araligi disina cikamaz
    mockAngle = constrain(mockAngle, rangeMin(), rangeMax());
    int idx = clampIndex(mockAngle);
    if (idx != prof.index) {
      bool cw = idx > prof.index;
      prof.index = idx;
      if (!silent) sendToken(cw ? prof.cwKey : prof.ccwKey);
      Serial.printf("STATE pos=%d/%d torque=%+.2f\n", prof.index, prof.detents - 1, computeTorque(mockAngle));
    } else if (!silent) {
      // index degismedi ama uctaysak duvar
      bool wall = (mockAngle <= rangeMin() || mockAngle >= rangeMax());
      Serial.printf("STATE pos=%d/%d torque=%+.2f%s\n", prof.index, prof.detents - 1,
                    computeTorque(mockAngle), wall ? "  WALL" : "");
    }
  } else {
    // durumsuz: her DEG_PER_DET adiminda cw/ccw tusu
    int idx = (int)floorf(mockAngle / DEG_PER_DET);
    if (lastFreeIdx != INT32_MIN && idx != lastFreeIdx && !silent) {
      sendToken(idx > lastFreeIdx ? prof.cwKey : prof.ccwKey);
    }
    lastFreeIdx = idx;
  }
}

static void nudge(float d) { mockAngle += d; onAngleChanged(false); }

// ---------------- PROFILE parse (key=value) ----------------
static HapticMode parseMode(const String& s) {
  if (s == "detent")  return MODE_DETENT;
  if (s == "spring")  return MODE_SPRING;
  if (s == "endstop") return MODE_ENDSTOP;
  return MODE_FINE;
}

static void applyProfile(const String& line) {
  // ilk kelime "PROFILE", sonrasi key=value...
  int posSet = -1;
  int i = line.indexOf(' ');
  while (i >= 0 && i < (int)line.length()) {
    int sp = line.indexOf(' ', i + 1);
    String tok = (sp < 0) ? line.substring(i + 1) : line.substring(i + 1, sp);
    int eq = tok.indexOf('=');
    if (eq > 0) {
      String k = tok.substring(0, eq), v = tok.substring(eq + 1);
      if      (k == "mode")    prof.mode    = parseMode(v);
      else if (k == "detents") prof.detents = v.toInt();
      else if (k == "pos")     posSet       = v.toInt();
      else if (k == "color")   prof.color   = strtoul(v.c_str(), nullptr, 16);
      else if (k == "cw")      prof.cwKey   = v;
      else if (k == "ccw")     prof.ccwKey  = v;
      else if (k == "click")   prof.clickKey = v;
    }
    i = sp;
  }
  // pozisyonu uygula (sessiz)
  prof.index = (posSet >= 0 && prof.detents > 0) ? constrain(posSet, 0, prof.detents - 1) : 0;
  mockAngle  = prof.detents > 0 ? prof.index * DEG_PER_DET : 0.0f;
  lastFreeIdx = INT32_MIN;

  Serial.printf("OK mode=%d detents=%d pos=%d color=%06lX cw=%s ccw=%s click=%s\n",
                prof.mode, prof.detents, prof.index, (unsigned long)prof.color,
                prof.cwKey.c_str(), prof.ccwKey.c_str(), prof.clickKey.c_str());
}

static void printStatus() {
  Serial.printf("STATUS pos=%d/%d angle=%.1f detents=%d mode=%d color=%06lX cw=%s ccw=%s click=%s\n",
                prof.index, prof.detents > 0 ? prof.detents - 1 : 0, mockAngle,
                prof.detents, prof.mode, (unsigned long)prof.color,
                prof.cwKey.c_str(), prof.ccwKey.c_str(), prof.clickKey.c_str());
}

// ---------------- Serial ----------------
static String rxBuf;

// Sadece hareket karakterlerinden olusan satir mi?  (+ - > < *)
static bool isMotionLine(const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c != '+' && c != '-' && c != '>' && c != '<' && c != '*') return false;
  }
  return s.length() > 0;
}

static void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (isMotionLine(line)) {          // ">>>>" = 4 centik CW gibi toplu da olur
    for (size_t i = 0; i < line.length(); i++) {
      switch (line[i]) {
        case '+': nudge(+STEP);        break;
        case '-': nudge(-STEP);        break;
        case '>': nudge(+DEG_PER_DET); break;
        case '<': nudge(-DEG_PER_DET); break;
        case '*': sendToken(prof.clickKey); Serial.printf("CLICK -> %s\n", prof.clickKey.c_str()); break;
      }
    }
    return;
  }
  if (line.startsWith("PROFILE")) { applyProfile(line); return; }
  if (line.startsWith("POS")) {
    int n = line.substring(3).toInt();
    if (prof.detents > 0) {
      prof.index = constrain(n, 0, prof.detents - 1);
      mockAngle  = prof.index * DEG_PER_DET;
      Serial.printf("SYNC pos=%d/%d\n", prof.index, prof.detents - 1); // tus basmaz
    }
    return;
  }
  if (line == "?") { printStatus(); return; }
  Serial.printf("bilinmeyen: %s\n", line.c_str());
}

static void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { handleLine(rxBuf); rxBuf = ""; }
    else { rxBuf += ch; if (rxBuf.length() > 160) rxBuf = ""; }
  }
}

void setup() {
  Serial.begin(115200);
  Keyboard.begin();
  Consumer.begin();
  USB.begin();
  delay(1000);
  Serial.println();
  Serial.println("SmartKnob v0.3 — konumsal mod + cift-yon protokol");
  Serial.println("PROFILE ... | POS n | + - > < c ?");
  printStatus();
}

void loop() {
  pollSerial();
  // Donanimda: sensor.update(); motor.move(computeTorque(sensor.getAngle()));
}
