# L6234 — Araştırma Sonucu ve Yarınki Test Planı

## A. ARAŞTIRMADA DOĞRULANANLAR (artık kesin, tekrar bakma)

Datasheet'ten (L6234, Doc ID 1107 Rev 10) birebir teyit edildi:

**1. Pin haritan %100 DOĞRU** (PowerSO20 numaralandırması):
| Sinyal | Datasheet PowerSO20 | Sende |
|---|---|---|
| OUT1/2/3 | 6 / 5 / 15 | ✓ aynı |
| IN1/2/3 | 7 / 4 / 14 | ✓ aynı |
| EN1/2/3 | 8 / 3 / 13 | ✓ aynı |
| VS | 9, 12 | ✓ |
| GND | 1, 10, 11, 20 | ✓ |
| VREF / VCP / VBOOT | 16 / 17 / 18 | ✓ |
| SENSE1 / SENSE2 | 2 / 19 | ✓ |

**2. Charge pump devren datasheet'in tipik uygulamasıyla aynı**
Datasheet blok şeması: 10nF (VCP) + 1µF (VREF) + 0.22µF (VBOOT) + 0.1µF + 100µF (VS) + 1N4148 ×2.
Sende: aynı — tek fark VREF'te 1µF yerine 10µF (zararsız, sadece yavaş dolar).

**3. Elektriksel seviyeler uygun**
- Giriş HIGH eşiği: **min 2V, max 7V** → 3.3V giriş ✓, 5V EN ✓
- Giriş akımı: **max 30µA** → seri 1kΩ dirençler sorun değil ✓
- VREF tipik **10V** → ölçtüğün 9.4V ✓
- RDS(on) 0.3Ω, dead-time 300ns, **termal kapanma ~160°C (20°C histerezis)**

**4. ÇOK ÖNEMLİ — önceki "kanal testi" GEÇERSİZDİ**
Datasheet: kapalı DMOS'un kaçak akımı **IDSS = 1mA'ya kadar**.
Multimetrenin iç direnci 10MΩ. Yani **çıkış tamamen kapalıyken (tri-state) bile** kaçak akım multimetrede 11.5V gösterir.
→ "Motor sökülüyken IN HIGH yapınca OUT 11.5V okudu" testi **hiçbir şey kanıtlamıyordu**.
→ Aynı şekilde LOW olan çıkışların 0V yerine 1.5V okuması da tri-state göstergesi.

**5. Çip CANLI ve VS DOĞRU BESLENİYOR (kanıt)**
VREF (9.4V) çipin **içindeki** regülatörden üretiliyor, VCP osilatörü çalışıyor, VBOOT 19.47V'a pompalanmış.
→ Bunlar ancak VS doğru pinlere geliyorsa olur. Çip ölü değil, ters lehimlenmemiş, besleme doğru.

## B. MANTIK ZİNCİRİ — GERİYE KALAN

Kesin bilinen: **akım hiç geçmiyor** (motor ısınmıyor, tork yok). Bu tek güvenilir kanıt.
Elenen: pinout, charge pump, besleme, giriş seviyeleri, motor (6Ω), kablolar (7.6-8Ω), MCU PWM'i, kütüphane sürümü (2.3.2/2.3.3 + core 2.0.17 uyumlu).
Çip canlı (VREF/VCP/VBOOT üretiyor) ama **çıkış katı iletime geçmiyor.**

Kalan 3 ihtimal (olasılık sırasıyla):
1. **EN çipte gerçekten HIGH değil** → tüm kanallar kapalı → çıkışlar tri-state → gözlemlenen HER ŞEY buna uyar (kaçak gerilimler, akım yok, ısınma yok). Kablo defalarca değişti (GPIO21 → 3.3V → 5V), lehim/temas kopmuş olabilir.
2. **Çıkış DMOS'ları hasarlı** — motorun aşırı ısındığı epizotta zarar görmüş olabilir (o sırada akım geçiyordu, sonra kesildi)
3. **Termal kapanma döngüsü** — reset sonrası kısa sürtünme sonra kaybolması buna uyuyor (çip ısınıp kapanıyor olabilir)

## C. YARIN — TEST SIRASI (multimetre yeterli, osiloskop gerekmez)

### TEST 1 — EN'i ÇİPİN PEDİNDE ölç (2 dakika, en yüksek öncelik)
12V açık. Siyah prob GND'de. **Ped 8, ped 3, ped 13** → üçü de 5V (veya 3.3V) olmalı.
- Biri düşük/0V ise → **sorun bulundu**, EN teli/lehimi. Düzelt, biter.
- Üçü de yüksekse → Test 2'ye geç.

### TEST 2 — ÇİP SICAK MI? (10 saniye)
Motoru 10 sn sür, sonra **L6234'e parmakla dokun**.
- **Sıcaksa** → içeride güç harcanıyor → termal kapanma/hasar → çip değişmeli
- **Buz gibiyse** → hiç akım geçmiyor, çip çalışmıyor → Test 3

### TEST 3 — BESLEME AKIMI (kesin ölçüm)
Multimetreyi **10A DC akım** moduna al, **12V hattına SERİ** bağla (artı uçtan L6234 VS'ye giden yolu kes, araya multimetre).
Motoru sür ve akıma bak:
- **~6.5mA** (boşta akım) → çıkışlardan hiç akım gitmiyor → çip sürmüyor
- **Yüzlerce mA / 1A+** → akım GEÇİYOR → sorun başka yerde (bu durumda motor ısınırdı, beklenmiyor)

### TEST 4 — LOW-SIDE testi (charge pump GEREKMEZ — kanalları ayırır)
Bu test high-side'ı devre dışı bırakır, sadece alt transistörleri sınar:
1. Motoru sök
2. Tüm IN'leri **LOW** yap (bende hazır komut var), EN HIGH
3. **VS (12V) ile OUT1 (ped 6) arasına ~100Ω/2W direnç** (veya 12V ampul) bağla
4. Ölç: OUT1 **~0V** kalmalı, direnç/ampul akım çekmeli (ampul yanar)
- **Çalışıyorsa** → low-side sağlam → sorun high-side/charge pump'ta
- **Çalışmıyorsa** → çip komple sürmüyor (EN veya hasar)

### TEST 5 — HIGH-SIDE testi
1. IN1 **HIGH**, EN HIGH
2. **OUT1 (ped 6) ile GND arasına ~100Ω/2W** (veya ampul)
3. Ölç: OUT1 **~12V'ta kalmalı**, akım geçmeli
- **OUT1 yük altında 0V'a çöküyorsa** → high-side akım süremiyor → charge pump/high-side hasarı
- **12V'ta kalıp akım veriyorsa** → high-side sağlam

> NOT: 100Ω'da 12V = 120mA. Bu, 1mA kaçak akımı **120 kat** aşar → ölçüm artık yanıltmaz. LED/multimetre gibi µA yükler bu yüzden yanıltıyordu.

### TEST 6 — VBOOT yük altında
Motor sürülürken **ped 18 (VBOOT)** ölç.
- **19V civarında kalıyorsa** → charge pump yeterli
- **VS'ye (12V) doğru çöküyorsa** → charge pump gate akımını besleyemiyor

## D. SONUÇ / KARAR AĞACI

- **Test 1'de EN düşük çıkarsa** → tek sorun oydu, düzelt, sistem çalışır
- **Test 4 (low-side) başarısızsa** → çip sürmüyor: EN veya kalıcı hasar → **L6234 değiştir**
- **Test 4 ✓ ama Test 5 (high-side) başarısızsa** → high-side/charge pump: önce C4'ü (0.22µF) ve diyotları kontrol et, düzelmezse **L6234 değiştir**
- **İkisi de ✓ ama motor dönmüyorsa** → sorun sürücüde değil; PWM zamanlaması/faz sırası tarafına bakarız

## E. HAZIRLIK ÖNERİSİ
- **Yedek L6234** bulundur (kuvvetli ihtimalle gerekecek; motorun aşırı ısındığı epizot çıkış katına zarar vermiş olabilir)
- Test için **100Ω/2W direnç veya küçük 12V ampul** (yük testi için şart)
- 1kΩ seri dirençleri koru (yeni ESP'yi korur)

---

# GÜNCELLEME (24 Ağu akşam — yeni bulgular sonrası)

**Yeni veriler:**
- EN pinleri çipte **doğru ölçüldü** → EN hipotezi ELENDİ
- Çip **neredeyse hiç ısınmıyor** → termal kapanma ELENDİ
- **Elle çevirip çentik hissedince çip hafif ısınıyor** → dışarıdan zorlanan akımı çip İLETİYOR (alt taraf/gövde diyotları sağlam)
- Ayarlı güç kaynağı mevcut → akım ölçümü bedava

**KALAN TEK GÜÇLÜ HİPOTEZ: HIGH-SIDE (üst DMOS) iletime geçmiyor.**
Alt taraf iletiyor, üst taraf sürülmüyor → VS'den motora akım basılamıyor → tork yok, ısınma yok.
Sebep ya charge pump'ın gate akımını besleyememesi ya üst DMOS hasarı.

## YARIN — YENİ SIRA (ayarlı güç kaynağıyla)

### ADIM 1 — Güç kaynağı akım göstergesi (bedava, sürekli izle)
12V, akım limiti **1A**'ya ayarla. Boşta ~**6.5mA** (çipin boş akımı) görmelisin.
Motoru sürerken akım hâlâ ~6.5mA'da kalıyorsa → **çıkışlardan hiç akım gitmiyor** (beklenen).

### ADIM 2 — HIGH-SIDE YÜK TESTİ (en kritik, önce bunu yap)
1. Motor **sökülü**
2. IN1 **HIGH**, EN HIGH (firmware'de hazır komut var)
3. **OUT1 (ped 6) ile GND arasına 100Ω/2W direnç** (veya küçük 12V ampul)
4. Bak:
   - **Güç kaynağı ~120mA çekiyor + OUT1 ~12V'ta kalıyor** → high-side SAĞLAM
   - **Akım ~0 + OUT1 yük altında çöküyor** → **high-side iletmiyor = ARIZA BURADA**
5. Aynısını OUT2 (ped 5, IN2 HIGH) ve OUT3 (ped 15, IN3 HIGH) için tekrarla → üç kanalı da ayrı ayrı gör

### ADIM 3 — LOW-SIDE YÜK TESTİ (karşılaştırma için)
1. Tüm IN'ler **LOW**, EN HIGH
2. **VS (12V) ile OUT1 arasına aynı 100Ω** direnci bağla
3. **~120mA akıyorsa** → low-side sağlam (beklenen)
→ Adım 2 başarısız + Adım 3 başarılı = **kesin teşhis: üst taraf ölü**

### ADIM 4 — VBOOT yük altında (Adım 2 başarısızsa)
Adım 2'deki yük bağlıyken **ped 18 (VBOOT)** ölç:
- **~19V** kalıyor ama akım yok → **üst DMOS hasarlı** → çip değiştir
- **12V'a (VS) çöküyor** → **charge pump yetersiz** → C4 (0.22µF) / D1 / D2 / C3 kontrol, gerekirse C4'ü 1µF yap

## NOT: Yeni ESP32 DevKit
Sorunun MCU'da olma ihtimali **düşük** — PWM'in çalıştığı ölçüldü (0.5-1V, doğru duty) ve bir ara motor gerçekten ısınmıştı (akım geçiyordu). Yine de yedek olarak iyi; 1kΩ seri dirençleri mutlaka koru.

---

# EK ARAŞTIRMA (GitHub / SimpleFOC topluluğu) + YENİ ANA ŞÜPHELİ

## Başkaları ne yapıyor?
- **SimpleFOC'un resmi shield'i (v2) zaten L6234 tabanlı** — yani çip doğru seçim, kanıtlanmış.
- **Fark: neredeyse herkes HAZIR BREAKOUT PCB kullanıyor** (Drotek L6234 breakout / SimpleFOC Shield). Ham PowerSO20'yi elle SO20→DIP adaptöre lehimleyen çok az kişi var.
  → Devre şemamız doğru; farkımız **el lehimi / temas riski**.
- **C4 (220nF) tartışması:** SimpleFOC shield'inde yanlışlıkla GND'ye bağlanmış; ST'ye sorulmuş, **doğrusu VS'ye** çıkmış (senin yaptığın gibi). Bizde doğru.
- EN'i doğrudan 5V'a bağlamak resmi dokümanda da onaylı yöntem. Bizde doğru.

## YENİ ANA ŞÜPHELİ: SENSE1 / SENSE2 bağlantısı

Akım yolu: `VS → üst DMOS → OUT → motor → OUT → alt DMOS → SENSE → GND`

**SENSE1 (ped 2) ve SENSE2 (ped 19), alt DMOS'ların KAYNAK (source) uçlarıdır.**
Bunlar GND'ye sağlam bağlı değilse (soğuk lehim / temassız):
- Alt taraf akım geçiremez → **devre kapanmaz** → akım 0, ısınma yok, tork yok ✓ (gözlemimiz)
- VS / VREF / VCP / VBOOT bunlardan bağımsız → çip "canlı" görünür ✓ (gözlemimiz)
- Elle çevirince geri-EMK üst gövde diyotlarından VS'ye akar → çipte hafif ısınma ✓ (gözlemimiz)

**ÜÇ GÖZLEMİ DE tek başına açıklıyor.**

### TEST 0 — EN ÖNCE BUNU YAP (2 dakika, güç kapalı)
Multimetre direnç/bip modunda:
| Ölç | Olması gereken |
|---|---|
| **ped 2 (SENSE1) ↔ GND** | **~0Ω / bip** |
| **ped 19 (SENSE2) ↔ GND** | **~0Ω / bip** |
| ped 1, 10, 11, 20 (GND pinleri) ↔ besleme GND | ~0Ω / bip (hepsi) |

Biri açıksa → **sorun bulundu.** Lehimle, biter.

> NOT: Bu ölçümü **çipin pedinde** yap, kablonun ucunda değil — soğuk lehim tam orada gizlenir.
> Ayrıca adaptörde çipin **tüm pinlerinin** lehimini gözle kontrol et (özellikle güç pinleri 9, 12 ve GND'ler).

---

# OSİLOSKOP İLE TESTLER (elde scopemeter var — en kesin yöntem)

Osiloskop, multimetrenin yapamadığı ayrımı yapar: **"anahtarlanıyor mu, sabit mi, boşta mı"**.
Bugünkü tüm kafa karışıklığı bundandı (ortalama okuma + 1mA kaçak).

**Prob kuralı:** toprak klipsi **GND'ye** (asla OUT gibi anahtarlanan bir düğüme bağlama). 12V sinyaller için uygun kademe seç.

### S1 — IN pini (MCU çıkışı) — ped 7 / 4 / 14
Motor sürülürken bak.
- **Beklenen:** ~20kHz kare dalga, 0↔3.3V, duty sürekli değişiyor (dönen alan)
- Düz çizgi görürsen → MCU PWM üretmiyor (kütüphane/pin sorunu)

### S2 — OUT pini (asıl test) — ped 6 / 5 / 15  ⭐ EN KRİTİK
Motor sürülürken bak.
- **Kare dalga 0↔12V görüyorsan** → sürücü ANAHTARLIYOR, çıkış katı çalışıyor
  → o zaman sorun akım yolunda (SENSE / lehim / motor bağlantısı)
- **Düz çizgi (sabit ~10V veya sabit 0V)** → sürücü anahtarlamıyor
  → çıkış katı / charge pump / EN sorunu
- **Kare dalga var ama tepe 12V'a ulaşmıyor / yamuk** → high-side zayıf sürülüyor (charge pump yetersiz)

### S3 — VCP (ped 17) — charge pump osilatörü
- **Beklenen:** sürekli kare dalga (birkaç yüz kHz, ~10V tepe)
- Düz çizgi → dahili osilatör çalışmıyor → çip arızalı

### S4 — VBOOT (ped 18) yük altında
Motor sürülürken bak.
- **~19-20V'ta düz duruyorsa** → charge pump yeterli
- **Testere gibi düşüp kalkıyorsa / 12V'a doğru çöküyorsa** → charge pump gate akımını besleyemiyor
  → C4'ü 1µF yap, C3'ü 22-47nF'ye çıkar

### S5 — SENSE pini (ped 2 / 19)
- **Beklenen:** GND'ye bağlıysa **düz 0V** (ölçülebilir gürültü olabilir)
- **Anahtarlanan bir dalga görüyorsan** → SENSE havada kalmış, alt DMOS kaynağı yüzüyor
  → **SORUN BU** (ana şüpheli)

## Osiloskopla karar tablosu
| S2 (OUT) | S5 (SENSE) | Teşhis |
|---|---|---|
| Kare dalga var | 0V düz | Sürücü OK → akım yolu/motor bağlantısına bak |
| Kare dalga var | dalgalı/yüzen | **SENSE bağlı değil** → lehimle |
| Düz çizgi | 0V düz | Çıkış katı sürülmüyor → S3/S4'e bak (charge pump) veya çip hasarlı |
| Düz çizgi | dalgalı | Hem SENSE hem sürüş sorunu |
