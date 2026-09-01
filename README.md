# SmartKnob

Masaüstü için haptik bir döner düğme. Fırçasız bir gimbal motoru FOC ile sürülüyor; çentikler, duvarlar ve dokular yazılımla üretiliyor. Mac'e USB HID olarak bağlanıyor — ses, masaüstü geçişi, scroll, parlaklık ve bir de kasa kilidi mantığıyla çalışan deploy menüsü.

Scott Bezek'in [SmartKnob](https://github.com/scottbez1/smartknob) projesinden ilham alındı, ama kod ve donanım sıfırdan yazıldı.

**Durum:** çalışıyor, günlük kullanımda. Bitmemiş işler aşağıda dürüstçe listelendi.

---

## Donanım

| Parça | Model | Ölçülen değer |
|---|---|---|
| MCU | ESP32-S3-DevKitC-1 | — |
| Motor | 2805 gimbal BLDC, 7 kutup çifti | faz-faz **5.51 Ω**, 140 KV |
| Sensör | MT6701, 14 bit manyetik enkoder | SSI + CRC, 108.585 okumada 0 hata |
| Sürücü | L6234PD | 0.3 Ω/anahtar, 300 ns ölü zaman |
| Ekran | ST7789 2.0" 240×320 | GPIO 38-42 |
| Butonlar | PCF8574, 8 adet | I²C1 (GPIO 10/11) |
| LED | WS2812 halka, 16 LED | GPIO 4 |
| Besleme | 12V → buck → 5V/3.3V | |

Motor gücünün **%12'sini** kullanıyoruz. Sınır motor değil, kontrol kararlılığı.

---

## İki haftada öğrendiklerimiz

Bu bölüm reponun asıl değeri. Hepsi ölçümle bulundu, çoğu datasheet'te yazmıyor.

### Çentiğin gücünü ARALIK verir, kararsızlığı SERTLİK verir

Testere profilinde:

```
tepe kuvvet = Kdet × aralık / 2      → aralıkla orantılı
sınır eğimi = Kdet                    → aralıktan BAĞIMSIZ
```

Yani sertliği artırmak güç vermiyor, sadece geçiş eğimini dikleştirip kararsızlık yaratıyor. **Zayıf çentik → aralığı büyüt. Titreme → sertliği küçült.** İki gün boyunca yanlış düğmeyi çevirdik.

Referans: SimpleFOC'un resmi haptik örneği de aynı yerde — 45° aralık, P=10.

### Sönüm bu sistemde ters çalışıyor

Altı ölçüm, hepsi aynı yönü gösterdi:

| sönüm | sonuç |
|---|---|
| 0.05 | iki çentik arası gelgiti keser, kararlı |
| 0.15 | sınırda |
| 0.35 | 41-52 Hz salınım, sönmüyor |
| 0.55 | tork tavanına dayanan limit çevrimi |

Sebebi: hız tahmini gecikmeli. Gecikmiş hızla yapılan sönüm, sönüm olmaktan çıkıp **sertliğe** dönüşüyor (`B·ω·sin φ`), rezonansı yukarı itiyor ve kendini besliyor. SimpleFOC'un resmi haptik örneği de **D=0** kullanıyor.

Denedik, işe yaramadı: PLL takip gözlemleyicisi (salınım 44 → 74 Hz'e çıktı), merkez histerezisi (ölçüm lehine, his aleyhine), sinüs profili (kararlı ama tepe kuvvet üçte bire düşüyor).

### SimpleFOC'un `min_elapsed_time` tuzağı

`Sensor::getVelocity()` içinde şu satır var:

```cpp
if (Ts < min_elapsed_time) return velocity;   // ESKİ değeri döndürür
```

Varsayılan **100 µs**. Döngü hızınız oraya yaklaşırsa hız bazen **bayat** dönüyor ve sistem titremeye başlıyor.

Bizde: `loop()` içinde 133 µs (güvenli), ayrı çekirdekte 104 µs (**sınırda**). FOC'u ayrı çekirdeğe alınca hız sadece %27 arttı ama tam bu eşiği kesti. `min_elapsed_time = 500 µs` yapınca tuzak kapandı.

### Kalibrasyon sensör konumuna bağlı

`zero_electric_angle` sabit kodlu. Sensör veya braket **her söküldüğünde geçersiz** oluyor — braketi yenileyince değerimiz 0.227/CCW'den 1.308/CW'ye taşındı, motor vızıldadı. `P 7` ile yeniden kalibre etmek gerekiyor.

Tek ölçüme güvenmeyin: ilk deneme 4.839/CCW verdi, sonraki üç tutarlı ölçüm 1.30/CW dedi.

### Kötü sürücü bazı sorunları gizler

L298N ile de çalışıyordu — ve kısmen **kötü olduğu için**. Kayıpları ve yavaşlığı, kontrol döngüsündeki gürültüyü ve aşırı kazancı yumuşatıyordu. L6234 temiz olduğu için ne komut edersek onu birebir basıyor, gürültü dahil. Sürücü sorunu yaratmadı, gizlenen sorunu görünür yaptı.

### MCU çökerse motor durmuyor

L6234'ün EN pini 3.3V'a sabitti. Bir elektrik kesintisi sonrası ESP açılamadı ama beslenmeye devam etti; sürücü aktif kaldı, PWM girişleri boşta kaldı ve motor **iki gün** titredi. PLA braket eridi, LED halkasının ilk çipi öldü.

**Yapmanız gereken:** EN'i bir GPIO'ya bağlayın ve GND'ye 10k pull-down koyun. IN1/IN2/IN3'e de aynısı. MCU çökse de, hiç açılmasa da sürücü kapalı kalır. (DRV8316 gibi modern sürücülerde bu koruma çip içinde hazır geliyor.)

---

## Ölçüm araçları

Kör ayar yapmamak için firmware'e gömülü:

| komut | ne yapar |
|---|---|
| `B <hız>` / `G` | hız tetiklemeli, **tam döngü hızında** 2500 örnek RAM kaydı + döküm |
| `R` | FOC döngü hızını Hz olarak ölçer |
| `C` | sensör CRC istatistiği + manyetik alan durumu |
| `Z` | GPIO4 kare dalga (multimetreyle kablo testi) |
| `n` | ekran test deseni |
| `P <pp>` | FOC kalibrasyonunu zorla yeniler |

Seri telemetri 167 Hz'di ve 83 Hz üstünü takma frekans olarak yanlış gösteriyordu. Burst kaydı bunu çözüyor.

**Ölçüm tuzağı:** knob fiziksel duvara dayandığında "söndü" gibi görünüyor. Analiz betiği artık bunu işaretliyor. Bir kez bu yüzden yanlış sonuç çıkardık.

---

## Kurulum

```bash
# Firmware — gunluk kullanim (USB HID aktif)
cd firmware && pio run -e foc -t upload

# Firmware — ayar/olcum (Serial UART'a duser, HID KAPALI)
pio run -e foctune -t upload
```

Mac tarafı Hammerspoon ile çalışıyor. `docs/hammerspoon-init.lua.example` dosyasını `~/.hammerspoon/init.lua` olarak kopyalayın, sonra sunucu bilgilerinizi ayrı bir dosyaya yazın:

```lua
-- ~/.hammerspoon/knob_secrets.lua
return {
  ERP_URL     = "https://sizin.sunucunuz/durum",
  DEPLOY_HOST = "kullanici@sunucu",
}
```

---

## Mimari notları

**FOC `loop()` içinde**, ekran ayrı çekirdekte. FOC'u ayrı çekirdeğe taşımak denendi ve titremeye yol açtı (yukarıdaki `min_elapsed_time` tuzağı). Kod duruyor, `FOC_SEPARATE_CORE` ile açılabilir — ama ölçerek girin.

**Çentik gridi göreli.** Mutlak duvar yok; her mod serbest dönüyor. Motorun ~300°'lik fiziksel sınırı var, o yüzden mod geçişlerinde ortalama yapılıyor.

**Mac senkronu iki katmanlı.** Hammerspoon her modun durumunu gönderiyor, knob hepsini RAM'inde tutuyor ve mod değişince hazır olanı gösteriyor. Mod bilgisi HID F-tuşlarıyla taşınıyor; 10 saniyede bir `SYNC` el sıkışması iki tarafın kopmasını engelliyor.

---

## Bitmemiş işler

- **EN pini hâlâ 3.3V'a sabit.** Yukarıdaki iki günlük olayın koşulları duruyor. En öncelikli iş bu.
- Harici monitörün parlaklığı okunamıyor (DDC/CI gerekiyor). Dahili ekran `IOMFBBrightnessLevel` üzerinden okunuyor — ölçek doğrusal değil, 16 kademe tek tek ölçüldü.
- Chrome sekme sayısı sadece o moddayken güncelleniyor (AppleScript pahalı).
- LED halkasının ilk çipi ölü; fiziksel olarak atlandı.
- Uçtan uca test tamamlanmadı.

---

## Neden bu repo işinize yarayabilir

SimpleFOC ile haptik knob yapan çok kişi var ama çoğu "çalıştı" deyip bırakıyor. Burada **ne işe yaramadığı** da yazıyor: hangi ayarın neden ters teptiği, hangi ölçümün neden yanlış olduğu, hangi teorinin veriyle çeliştiği. Aynı duvarlara çarpmak zorunda kalmayın.

Sorularınız olursa issue açın.

---

## English

A desktop haptic rotary knob: BLDC gimbal motor driven with FOC, software-defined detents, USB HID to macOS. ESP32-S3 + SimpleFOC + MT6701 magnetic encoder (SSI) + L6234 driver.

The valuable part of this repo is the **measured findings**, not the code:

- **Detent strength comes from spacing, instability comes from stiffness.** For a sawtooth profile, peak torque = `Kdet × spacing / 2` but the boundary slope is just `Kdet`. Raising stiffness buys no strength and costs stability. SimpleFOC's own haptic example uses a 45° attractor distance with P=10.
- **Damping destabilizes this system.** Six measurements agree. Delayed velocity estimation turns the damping term into added stiffness, pushing the resonance up until it self-sustains. SimpleFOC's haptic example uses D=0.
- **SimpleFOC's `min_elapsed_time` (default 100 µs) returns a stale velocity** if your loop period approaches it. Moving FOC to its own core raised our rate only 27% but crossed that threshold and caused chatter. Setting it to 500 µs fixed it.
- **A hard-wired driver enable pin is dangerous.** After a power cut our MCU never booted but stayed powered; the L6234 stayed enabled with floating inputs and drove the motor for two days. Use a GPIO with a 10k pull-down, or a driver with internal pull-downs (DRV8316).
- Tried and rejected: PLL tracking observer (oscillation moved 44 → 74 Hz), detent-center hysteresis (better numbers, worse feel), sine torque profile (stable but 1/3 the peak force).

Firmware includes a full-loop-rate burst recorder (`B`/`G`) because 167 Hz serial telemetry aliases anything above 83 Hz.

Inspired by [scottbez1/smartknob](https://github.com/scottbez1/smartknob); code and hardware are independent.
