# L6234D Sürücü Arıza Raporu — BLDC Gimbal Motor Dönmüyor

## 1. SİSTEM

**Amaç:** Haptik knob (SmartKnob benzeri). Kapalı çevrim FOC ile gimbal motorda çentik/duvar hissi.

**Donanım:**
- **Motor:** GBM2804H-100T gimbal BLDC, 7 kutup çifti, **faz-faz 6Ω**. Kablolar içten geçtiği için **fiziksel olarak ~300° sınırlı** (sonsuz dönmüyor, uçlarda mekanik stop).
- **Sürücü:** **L6234D** (ST, PowerSO20), SO20→DIP adaptöre lehimli (adaptör ped no = çip pin no).
- **Sensör:** MT6701 (I2C, 14-bit) — şu anki testlerde bağlı değil.
- **MCU (şu an):** Klasik **ESP32-D0WD-V3**, 38-pin DevKit, CP2102.
- **Besleme:** 12V adaptör (sadece L6234 VS'ye). ESP **sadece USB**'den. Ortak olan tek şey GND.

**ÖNEMLİ TARİHÇE:** Bu sistem **L298N sürücüsüyle SORUNSUZ ÇALIŞIYORDU** — kapalı çevrim FOC, MT6701 sensör, 17 çentik, duvar hissi, homing, hepsi çalışıyordu. Sadece sürücü L298N → L6234D olarak değiştirildi (daha temiz sinüs, daha az gerilim düşümü için).

## 2. L6234 BAĞLANTI DETAYI

**Pin eşlemesi (adaptör pedi):**
| İşlev | Ped |
|---|---|
| OUT1 / OUT2 / OUT3 | 6 / 5 / 15 |
| IN1 / IN2 / IN3 | 7 / 4 / 14 |
| EN1 / EN2 / EN3 | 8 / 3 / 13 (üçü birleşik) |
| VS | 9, 12 |
| GND | 1, 10, 11, 20 |
| VCP | 17 |
| VBOOT | 18 |
| VREF | 16 |
| SENSE1 / SENSE2 | 2 / 19 → **doğrudan GND** (shunt yok, akım algılama yok) |

**Harici charge pump (AN1088 Figure 5'e göre kuruldu):**
- C3 = 10nF: ped 17 (VCP) ↔ A düğümü
- D1 = 1N4148: anot ped 9 (VS), katot A düğümü
- D2 = 1N4148: anot A düğümü, katot ped 18 (VBOOT)
- C4 = 220nF: ped 18 ↔ ped 12 (VS'ye, GND'ye değil)
- C5 = 10µF: ped 16 (VREF) ↔ GND
- Bulk: 470µF + 100nF, VS–GND

**MCU bağlantısı:**
- IN1/IN2/IN3 ← GPIO **25 / 26 / 27**, her biri **seri 1kΩ** üzerinden
- EN (birleşik) → **5V** (3.3V ve GPIO ile de denendi, fark yok)
- PWM 20kHz, SimpleFOC 2.3.2, open-loop velocity modu

## 3. ÖLÇÜLEN DEĞERLER (hepsi doğrulandı)

| Ölçüm | Değer | Durum |
|---|---|---|
| VS (besleme) | 11–12V | ✅ |
| **VBOOT (ped 18)** | **19.47V** | ✅ charge pump çalışıyor (VS + ~7.5V) |
| VREF (ped 16) | 9.4V | ✅ |
| VCP (ped 17) | 4.4V | ✅ |
| EN | 5V | ✅ |
| Motor sargıları (3 çift, motor ucunda) | **hepsi 6Ω** | ✅ motor sağlam |
| **OUT ped ↔ OUT ped (motor takılıyken)** | **7.6–8Ω (üç çift de)** | ✅ **tüm elektriksel yol kapalı** (ped→lehim→kablo→konnektör→sargı) |

## 4. YAPILAN TESTLER VE SONUÇLARI

### Test A — Statik kanal testi (motor SÖKÜLÜ, yüksüz)
Her IN sırayla HIGH, diğerleri LOW:
| Giriş | Çıkış ölçümü | Sonuç |
|---|---|---|
| IN1 (GPIO25) HIGH | OUT1 = **11.58V** | ✅ |
| IN2 (GPIO26) HIGH | OUT2 = **11.5V** | ✅ |
| IN3 (GPIO27) HIGH | OUT3 = **11.5V** | ✅ |
(HIGH olmayan çıkışlar ~1.5V — yüksüz olduğu için)
→ **Üç kanal da yüksüz koşulda doğru anahtarlıyor.**

### Test B — PWM doğrulaması (MCU tarafı)
Open-loop sürüş sırasında GPIO25/26/27 ortalaması **0.5–1V** arası, sürekli değişiyor.
Beklenen: `voltage_limit 3V ÷ 12V = %25 duty` → 3.3V × 0.25 ≈ **0.83V**. ✅ **PWM doğru üretiliyor.**

### Test C — Open-loop sürüş (motor TAKILI)
`voltage_limit` 3V, 5V, 6V ile; hedef ±4 rad/s; ileri/geri; kısa darbeler ve uzun süreli.
**Sonuç: motor DÖNMÜYOR ve ISINMIYOR.** (6V/8Ω ≈ 1A akması, saniyeler içinde ısınması beklenirdi.)
Sürüş sırasında motor uçlarında GND'ye göre **üç uçta da ~10.5V** ölçülüyor (aralarında fark yok → akım yok).

### Test D — Statik enerjilendirme (motor TAKILI) — EN KRİTİK TEST
IN1 HIGH + IN2/IN3 LOW, **2 saniye** (12V'un ~8Ω'a binmesi, ~1.5A, motorun kilitlenmesi beklenir).
Sonra aynısı IN2 ve IN3 için tekrarlandı.
**Sonuç: motor KİLİTLENMEDİ, ZIPLAMADI, ISINMADI. Hiçbir şey olmadı.**

### Test E — Reset gözlemi (İPUCU)
**ESP'ye reset atıldıktan hemen sonra motoru elle çevirince ÇOK HAFİF bir sürtünme/çentik hissi var; birkaç saniye içinde tamamen kayboluyor.**

## 5. ÇELİŞKİ (asıl soru)

- Yüksüz statik testte çıkışlar **11.5V** veriyor (Test A) ✅
- Elektriksel devre tam kapalı, **7.6–8Ω** (motor dahil) ✅
- MCU doğru PWM üretiyor ✅
- Charge pump/VBOOT/VREF/EN değerleri doğru ✅
- **AMA yük altında akım geçmiyor** — ne dönme, ne tutma torku, ne ısınma (Test C, D)

Yani: **gerilim var, akım yok.** Çıkışlar yüksüz koşulda yükseliyor ama gerçek yük bağlanınca akım süremiyorlar.

## 6. ÖNEMLİ EK TARİHÇE (akım BİR ARA GEÇİYORDU)

Daha erken bir aşamada, motor pinleri GPIO 15/16/17'deyken:
- Sürüş sırasında ölçüm: **OUT1 = 6.08V, OUT3 = 5.64V, OUT2 = 9.90V** (OUT2 takılı görünüyordu)
- **Motor ÇOK ISINMIŞTI** (dönmüyordu ama akım çekiyordu — stall)
- Yani o aşamada **akım gerçekten akıyordu**

Sonrasında (kablolama düzeltildikten, GPIO'lar 25/26/27'ye taşındıktan sonra) **akım tamamen kesildi.**
→ O aşırı ısınma sırasında **L6234'ün çıkış katı hasar görmüş olabilir** (yüksüz gerilim verebiliyor ama akım süremiyor).

## 7. ELENENLER (kesin doğrulandı, bunlara bakmaya gerek yok)

- ❌ Motor arızası — sargılar 6Ω, sağlam
- ❌ Kablo/lehim kopukluğu — OUT-OUT 7.6-8Ω, devre kapalı
- ❌ MCU/PWM sorunu — PWM ölçüldü, doğru duty
- ❌ EN/enable sorunu — EN 5V, kanallar yüksüz anahtarlıyor
- ❌ Charge pump kurulum hatası — VBOOT 19.47V (statik)
- ❌ Besleme sorunu — VS 11-12V, bulk kondansatörler var
- ❌ Ortak GND eksikliği — ortak, doğrulandı
- ❌ Yazılım/pin karmaşası — pinler tek tek LED+multimetre ile doğrulandı

**NOT:** Süreçte iki büyük hata yaşandı (rapora dahil, sebep-sonuç için):
1. **ESP32-S3 yandı:** multimetre probu **ped 6 (OUT1, 12V) ile ped 7 (IN1, GPIO)** arasında kaydı → 12V lojiğe bindi → S3'ün 3.3V rail'i kısa devre oldu. (L6234 pinout'unda her OUT bir IN'in yanında: 4-5, 6-7, 13-14-15.)
2. **GPIO11'e "GND" diye kablo bağlandı:** 38-pin ESP32'de GPIO 6-11 dahili flash hattı → flash bozulması, bootloop, `invalid header`, `csum err`, anlamsız ölçümler. Düzeltildi.

## 8. ARAŞTIRILMASI İSTENEN

**Ana soru:** L6234 yüksüz koşulda çıkışlarını 11.5V'a çekebiliyor ama yük altında (8Ω, ~1.5A) hiç akım süremiyorsa sebebi ne olabilir?

Değerlendirilmesi istenen hipotezler:
1. **High-side DMOS hasarı** — aşırı ısınma/stall sırasında bozulmuş olabilir; gate sürülüyor ama kanal akım geçirmiyor
2. **Charge pump yük altında çöküyor** — statik 19.47V ölçülüyor ama gerçek gate şarj akımında VBOOT sağalıyor olabilir (statik DMM ölçümü bunu yakalamaz). C4=220nF yeterli mi? D1/D2 (1N4148) uygun mu? AN1088 Fig 5 doğru mu uygulanmış?
3. **Termal koruma / undervoltage lockout** devreye giriyor olabilir (reset sonrası kısa süre çalışıp kesilmesi bunu düşündürüyor — Test E)
4. **SENSE1/SENSE2'nin doğrudan GND'ye bağlanması** bir sorun yaratır mı?
5. Reset sonrası kısa süreli sürtünmenin (Test E) anlamı ne olabilir?

**Elde olan ölçüm imkânları:** multimetre (osiloskop YOK). Bu yüzden PWM dalga formu, VBOOT'un yük altındaki davranışı gibi dinamik ölçümler yapılamıyor — osiloskopsuz doğrulanabilecek testler tercih edilmeli.
