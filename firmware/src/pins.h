// SmartKnob — pin tanimlari (ESP32-S3-DevKitC-1)
// Detay + gerekce: docs/01_pinout.md
// GPIO numaralari kod icin gecerli; DevKitC-1'de fiziksel yer XIAO'dan farkli.
#pragma once

// Hepsi SOL sirada, bitisik ve derli toplu (bkz docs/01_pinout.md fiziksel harita)
// SOL sira: 3V3 3V3 RST | 4 5 6 7 15 16 17 18 8 3 46 9 10 11 12 13 14 | 5V GND

// --- Motor (L298N, 3PWM) — 3 pin YAN YANA ---
#define PIN_MOTOR_A   15   // GPIO15 -> L298N IN1 (phase U)
#define PIN_MOTOR_B   16   // GPIO16 -> L298N IN2 (phase V)
#define PIN_MOTOR_C   17   // GPIO17 -> L298N IN3 (phase W)
// L298N ENA + ENB -> sabit HIGH. IN4 kullanilmaz. Motor OUT1/2/3.

// --- Sensor + butonlar (I2C, ortak hat) — SDA/SCL yan yana ---
#define PIN_I2C_SDA   5    // GPIO5  -> AS5600 SDA + PCF8574 SDA
#define PIN_I2C_SCL   6    // GPIO6  -> AS5600 SCL + PCF8574 SCL
// AS5600 adres 0x36 | PCF8574 buton 0x20..0x27
// !!! AS5600 3V3'ten beslensin (ust SOL 3V3 pini) — 5V YAKAR !!!

// --- RGB (NeoPixel ring) ---
#define PIN_RGB       4    // GPIO14 -> WS2812 DIN (standalone testte calismisti)
// DevKitC-1 onboard WS2812 = GPIO48 (sag, hizli test icin)

// --- I2C1 (Wire1): PCF8574 buton genisletici — AYRI bus (AS5600 hattini yavaslatmaz) ---
#define PIN_I2C1_SDA  10   // GPIO10 -> PCF8574 SDA (yan yana)
#define PIN_I2C1_SCL  11   // GPIO11 -> PCF8574 SCL
// PCF8574 adres 0x20 (A0/A1/A2->GND). P0-P7 butonlar (diger uc GND).

// --- Opsiyonel ---
#define PIN_BTN_INT   18   // GPIO18 -> PCF8574 INT (kesme, istege bagli)

// --- Ekran (ST7789V 2.0" 240x320, SPI) — SAG sira bitisik blok (sol sira motor/I2C/LED dolu) ---
#define PIN_TFT_CS    42   // CS
#define PIN_TFT_DC    41   // DC
#define PIN_TFT_RST   40   // RST
#define PIN_TFT_MOSI  39   // SDA (MOSI)
#define PIN_TFT_SCK   38   // SCL (SCK)
// VCC -> 3.3V rayi, GND -> GND rayi, MISO gerekmez. 38-42 = DevKitC-1 sag sira, bitisik, bos.

// --- FOC baslangic degerleri (guvenli) ---
#define MOTOR_POLE_PAIRS   7      // 2805 ~7, initFOC dogrular / gerekirse elle
#define MOTOR_VOLTAGE_LIMIT 3.0f  // DUSUK basla, isinma izle, kademeli artir
#define MOTOR_SUPPLY_V     12.0f

// ==== DevKitC-1 KULLANMA / KACIN ====
//  GPIO0        : BOOT butonu (strapping) — kullanma
//  GPIO3,45,46  : strapping — kullanma
//  GPIO19,20    : native USB D-/D+ — HID icin AYIR, baska sey bagla ma
//  GPIO43,44    : UART0 TX/RX — CH340 (upload/serial) kullaniyor, ayir
//  GPIO26-37    : dahili flash/PSRAM — KULLANILAMAZ
//  Serbest      : 7-18, 21, 38-42, 47, 48 (onboard RGB)
