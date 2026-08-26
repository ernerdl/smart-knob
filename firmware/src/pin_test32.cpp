// Klasik ESP32 — PIN TESTI: istenen GPIO'yu HIGH/LOW yapar, multimetreyle olcersin.
// Amac: hangi GPIO gercekten cikis verebiliyor (GPIO16 WROVER'da PSRAM olabilir).
// GUVENLIK: motor/EN bagliyken tek faz HIGH birakma -> sargidan akim gecer, ISINIR.
//           Test icin MOTORU veya EN'i AYIR, sadece ESP pinini olc.
//
// Komutlar:  h <gpio>  = HIGH | l <gpio> = LOW | a = hepsi LOW | c = otomatik tur | ? = durum
#include <Arduino.h>

// GUVENLI adaylar: 6-11 FLASH, 34-39 input-only, 0/2/12 strapping, 1/3 UART -> haric
static const int PINS[] = {15, 16, 17, 25, 26, 27, 4, 5, 13, 14, 18, 19, 21, 22, 23, 32, 33};
static const int NP = sizeof(PINS)/sizeof(PINS[0]);
static String rx;

static void allLow() {
  for (int i = 0; i < NP; i++) { pinMode(PINS[i], OUTPUT); digitalWrite(PINS[i], LOW); }
  Serial.println("hepsi LOW");
}
static void status() {
  Serial.print("durum: ");
  for (int i = 0; i < NP; i++) Serial.printf("GPIO%d=%d  ", PINS[i], digitalRead(PINS[i]));
  Serial.println();
}
static void cycle() {   // her pini sirayla 4sn HIGH yapar (olcmeye vakit kalsin)
  Serial.println("OTOMATIK TUR: her pin 4sn HIGH (olc!)");
  for (int i = 0; i < NP; i++) {
    allLow();
    digitalWrite(PINS[i], HIGH);
    Serial.printf(">>> GPIO%d = HIGH (4sn) — SIMDI OLC\n", PINS[i]);
    delay(4000);
  }
  allLow();
  Serial.println("tur bitti, hepsi LOW");
}
static void handle(String s) {
  s.trim(); if (!s.length()) return;
  char c = s[0];
  int gpio = s.substring(1).toInt();
  if (c=='h' || c=='l') {
    bool ok = false;
    for (int i = 0; i < NP; i++) if (PINS[i]==gpio) ok = true;
    if (!ok) { Serial.printf("GPIO%d listede yok (15,16,17,25,26,27)\n", gpio); return; }
    pinMode(gpio, OUTPUT);
    digitalWrite(gpio, c=='h' ? HIGH : LOW);
    Serial.printf("GPIO%d -> %s\n", gpio, c=='h' ? "HIGH (3.3V bekle)" : "LOW (0V bekle)");
  }
  else if (c=='a') allLow();
  else if (c=='c') cycle();
  else if (c=='?') status();
  else Serial.println("komut: h <gpio> | l <gpio> | a | c | ?");
}

void setup() {
  Serial.begin(115200);
  delay(600);
  // BOOT'TA OTOMATIK HIGH (USB/seri olmadan da test edilebilsin).
  // GPIO16/17 HARIC -> onlar bootloop yapiyordu (WROVER'da PSRAM).
  delay(1500);   // once sistem otursun
  for (int i = 0; i < NP; i++) {
    int g = PINS[i];
    if (g == 16 || g == 17) continue;
    pinMode(g, OUTPUT); digitalWrite(g, HIGH);
  }
  Serial.println();
  Serial.println("=== PIN TESTI (klasik ESP32) ===");
  Serial.println("BOOTTA OTOMATIK HIGH (16/17 haric). LED ile tara. Komut: h/l <gpio>, a, ?");
  Serial.println("Komut: h <gpio> HIGH | l <gpio> LOW | a hepsi LOW | c otomatik tur | ? durum");
  Serial.println("!! MOTOR/EN bagliysa AYIR: tek faz HIGH kalirsa motor isinir !!");
}
void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch=='\n' || ch=='\r') { handle(rx); rx=""; }
    else { rx += ch; if (rx.length()>20) rx=""; }
  }
}
