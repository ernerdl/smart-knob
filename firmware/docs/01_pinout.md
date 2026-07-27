# SmartKnob — Pinout (ESP32-S3-DevKitC-1)

Yükleme: **CH340 UART portundan** (`/dev/cu.usbserial-110`) → otomatik reset.
HID+CDC: **native USB portundan** (final knob işlevi).

## v1 pin ataması — hepsi SOL sırada, derli toplu

Fiziksel SOL sıra (yukarıdan aşağı):
```
3V3  ● ← AS5600 VCC (3.3V!)
3V3  ●
RST  ●
 4   ●   (boş)
 5   ● ── I2C SDA   ┐ AS5600 + PCF8574
 6   ● ── I2C SCL   ┘
 7   ● ── RGB WS2812
15   ● ── Motor A (IN1) ┐
16   ● ── Motor B (IN2) ├ 3 pin YAN YANA → L298N
17   ● ── Motor C (IN3) ┘
18   ● ── (ops. PCF8574 INT)
 8   ●   (boş)
 3   ●   strapping — kullanma
46   ●   strapping — kullanma
9-14 ●   (boş)
5V   ●
GND  ● ← ortak GND
```

| Fonksiyon | GPIO | Bağlantı |
|-----------|------|----------|
| **I2C SDA** | 5  | AS5600 SDA + PCF8574 |
| **I2C SCL** | 6  | AS5600 SCL + PCF8574 |
| **RGB WS2812** | 7 | harici veri (onboard = GPIO48) |
| **Motor PWM A** | 15 | L298N IN1 (phase U) |
| **Motor PWM B** | 16 | L298N IN2 (phase V) |
| **Motor PWM C** | 17 | L298N IN3 (phase W) |
| PCF8574 INT (ops.) | 18 | buton kesme |

Motor 3 fazı → L298N **OUT1/OUT2/OUT3**. ENA+ENB → **sabit HIGH**. IN4 boş.
Butonlar **PCF8574** ile aynı I2C hattında → 0 ekstra pin.
**Native USB (HID) = sağ taraf GPIO19/20** — boş bırak.

## ⚠ DevKitC-1'de KULLANMA / KAÇIN
| GPIO | Neden |
|------|-------|
| 0 | BOOT butonu (strapping) |
| 3, 45, 46 | strapping pinleri |
| **19, 20** | **native USB D-/D+ → HID için AYIR** |
| **43, 44** | UART0 TX/RX → CH340 (upload/serial) |
| 26–37 | dahili flash / PSRAM — kullanılamaz |

**Serbest pinler:** 7–18, 21, 38–42, 47, 48. Ekran/ek çevre buradan.

## Güç mimarisi (değişmedi)
```
UART USB (CH340) → upload + debug serial (auto-reset)
Native USB       → HID + CDC (knob tuşları, Mac'e)
12V adaptör → L298N +12V (motor gücü) + onboard 5V reg → L298N lojik
ORTAK GND: L298N vidalı GND = 12V(−) = DevKit GND ← ŞART
```

## ⚠ Kritik uyarılar
1. **AS5600 → 3V3'ten besle** (5V I2C'yi ve pini yakar).
2. **Ortak GND** L298N vidalı klemensten (header GND yok). 5V-EN jumper takılı olmalı (güç LED yanar).
3. **12V hattına 470µF+ kondansatör** (vidalı klemense) — yük altında reset'i önler.
4. **İlk çalıştırma:** `voltage_limit` ≈ 2-3V başla; L298N ~2-3V düşürür, 4-5V'a çıkman gerekir. Isınmayı izle.
5. Motor sınırlı dönüşlü (~300°) — uçlarda mekanik duruş (haptik duvar olarak kullanılacak).

## FOC kalibrasyon notu
- 2805 ≈ **7 kutup çifti** (initFOC doğrular / gerekirse elle).
- AS5600 mıknatıs mesafesi AGC ile: 3.3V'ta ideal ~64. Diametrik mıknatıs (doğrulandı).
