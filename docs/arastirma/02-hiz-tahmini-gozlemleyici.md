# Nicemlenmiş Enkoderden Hız Tahmini ve Gözlemleyiciler


Sen hareket kontrolü (motion control) ve durum tahmini konusunda derin araştırma
yapan bir uzmansın.

**Sistem:** Masaüstü kullanım için haptik döner düğme (rotary haptic knob).
- MCU: ESP32-S3, arduino-esp32 2.0.17, SimpleFOC 2.3.3
- Motor: GBM2804/2805 sınıfı gimbal BLDC, 7 kutup çifti, düşük cogging, demir çekirdekli
- Sensör: MT6701, 14 bit manyetik enkoder, SSI arayüzü, CRC doğrulamalı (108.585 okumada 0 hata)
- Sürücü: L6234PD, 12V besleme, 1A'de ~1.8V toplam düşüm, 300 ns ölü zaman
- **Akım ölçümü YOK.** Tork komutu = doğrudan q ekseni voltajı (voltage-mode torque). Akım döngüsü yok.
- Kontrol: en yakın sanal çentik merkezine yay + merkezde ölü bölge + hıza orantılı sönüm
- Ayarlar: yay 22 V/rad, sönüm 0.15 V·s/rad, ölü bölge 0.012 rad, voltaj tavanı 4.5 V
- Hız filtresi: birinci derece LPF, Tf = 0.03 s
- Kontrol döngüsü Arduino `loop()` içinde; izole edildiğinde 9600 Hz ölçüldü

**Çözülememiş üç gözlem — mümkünse bunlara ışık tut:**
1. Sönümü artırmak titremeyi **artırıyor**: 0.15 → 2 çırpıntı, 0.35 → 14, 0.55 → 47 (aynı testte)
2. Kontrol döngüsünü 9600 Hz'e çıkarmak titreme yarattı; 4000 Hz daha iyiydi; **2000 Hz daha kötüydü** (monoton değil)
3. Aynı firmware daha kötü bir sürücüyle (L298N, ~3V düşüm, yavaş bipolar çıkış) daha kararlıydı

**Problem:** Hız, 14 bit mutlak açıdan basit fark alma (`Δθ/Δt`) ve birinci derece
alçak geçiren filtre (Tf = 0.03 s) ile hesaplanıyor. Yüksek döngü hızlarında tek bit
adımı (2π/16384 ≈ 3.83e-4 rad) çok büyük görünen hız sıçramalarına yol açıyor.
Filtre bunu bastırıyor ama gecikme ekliyor; gecikme de kapalı döngüde salınıma dönüşüyor.

**Araştırılacaklar:**

1. Nicemlenmiş konum ölçümünden hız tahmini yöntemlerini karşılaştır:
   sabit zaman farkı, sabit konum farkı (M/T yöntemi), en küçük kareler regresyonu,
   Kalman filtresi, Luenberger gözlemleyici, PLL/tracking observer, sliding mode observer.
   Her biri için: gecikme, gürültü bastırma, hesap yükü, ayar zorluğu.
2. **Tracking observer** (ikinci/üçüncü derece PLL tipi) yaklaşımını ayrıntılı anlat.
   Enkoder tabanlı servolarda neden endüstri standardı hâline geldi? Kazanç ayarı
   nasıl yapılır, bant genişliği–gecikme ödünleşimi nasıl yönetilir?
3. Nicemleme gürültüsünün varyansının örnekleme periyoduyla nasıl ölçeklendiğini
   türet. Basit fark alma için gürültü ~ 1/T ile büyürken, filtreleme ile net etki
   nasıl değişir? "Daha hızlı döngü her zaman daha iyi değildir" iddiasını
   nicel olarak destekle veya çürüt.
4. Haptik uygulamalarda hız geri beslemesi yerine **konum farkına dayalı sönüm**
   (örneğin bir sanal kuplaj yayı-sönümleyicisi) kullanmanın avantajı var mı?
5. Aynı problemi çözen açık kaynak firmware'lerin (ODrive, VESC, SimpleFOC,
   MotorGo, Mechaduino, TMC gömülü firmware) hız tahmininde ne yaptığını
   **kod seviyesinde** karşılaştır: hangi gözlemleyici, hangi kazançlar, hangi filtre.
6. SimpleFOC'un `LPF_velocity` uygulamasının tam matematiği nedir, `Tf` seçimi için
   literatürde/toplulukta önerilen aralık nedir, bizim 0.03 s değerimiz neye karşılık gelir?

---

## İSTENEN ÇIKTI

Cevabını şu yapıda ver:

1. **Kısa özet** — en fazla 10 madde, her biri tek cümle.
2. **Ana bulgular** — her bulgu için: iddia, dayanak (kaynak + neden güvenilir), ve yukarıdaki sisteme ne anlama geldiği. Formül varsa terimlerini açıkla.
3. **Somut öneriler** — uygulanabilir adımlar hâlinde, **öncelik sırasıyla**. Her öneri için: beklenen kazanç, uygulama maliyeti (süre/para/karmaşıklık), risk, ve **nasıl ölçüleceği** (hangi test, hangi metrik, başarı eşiği).
4. **Çelişkiler ve belirsizlikler** — kaynaklar arasında çelişki varsa açıkça belirt. Emin olmadığın yerleri "emin değilim" diye işaretle; uydurma.
5. **Kaynakça** — bağlantı, yazar/kurum, tarih. Akademik makale, üretici uygulama notu, datasheet ve ciddi mühendislik bloglarını önceliklendir; forum yorumlarını ancak başka kaynak yoksa ve etiketleyerek kullan.

**Kurallar:**
- Türkçe yaz. Teknik terimlerin İngilizcesini ilk geçtiği yerde parantez içinde ver.
- Sayı ver. "Daha iyi" değil, "%30 daha az" veya "0.005–0.01 s aralığında".
- Bir şeyi doğrulayamadıysan bunu yazmaktan çekinme. Boşluğu tahminle doldurma.
