# Manyetik Açı Sensörleri: Seçim, Montaj, Hata Kaynakları


Sen manyetik konum sensörleri ve hassas ölçüm konusunda derin araştırma yapan bir uzmansın.

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

Ek bilgi: MT6701 SSI modunda 24 bitlik çerçeve (14 bit açı, 4 bit alan durumu,
6 bit CRC) okunuyor, CRC doğrulanıyor, 108.585 okumada 0 hata. Rotor mekanik
olarak kilitliyken tam tork uygulandığında açı okuması hiç kaymıyor
(yani statordan sensöre manyetik geçiş tespit edilemedi).

**Araştırılacaklar:**

1. **Sensör karşılaştırması:** MT6701, AS5047P/AS5147, TLE5012B, MA732/MA702,
   AEAT-8800, iC-MU. Çözünürlük, gürültü, gecikme (propagation delay), arayüz,
   dahili filtreleme, hız çıkışı, kalibrasyon desteği, fiyat.
   **Gecikmeye özellikle dikkat et** — kapalı döngü haptikte sensör gecikmesi kritik.
2. **Dahili filtreleme tuzağı.** Bazı manyetik enkoderlerin dahili DSP'si
   açıyı filtreliyor veya hız tahmini yapıyor; bu ekstra gecikme demek.
   Hangi sensörlerde bu var, kapatılabiliyor mu, ne kadar gecikme ekliyor?
3. **Sistematik hatalar:** eksantriklik (magnet off-axis), eğiklik, mıknatıs
   homojensizliği, sıcaklık kayması. Bunların açı hatasına katkısı nasıl modellenir
   ve **kalibrasyonla** nasıl düzeltilir? (Örneğin bir tur boyunca hata haritası çıkarıp
   look-up table ile düzeltmek.) Haptik hissi ne kadar etkiler?
4. **Mıknatıs seçimi ve montaj:** diametral mıknatıs boyutu, malzeme (N35/N52),
   hava aralığı toleransı, mile merkezleme, yapıştırma. Üreticilerin önerdiği
   hava aralığı ve eksen kaçıklığı toleransları.
5. **Manyetik ekranlama:** stator alanının sensöre karışmasını önlemek için
   mumetal/çelik levha kullanımı, kalınlık, konum, yan etkileri (motorun kendi
   alanını bozma). Ne zaman gerekli, nasıl test edilir?
6. **Arayüz seçimi:** SSI vs SPI vs ABZ vs SENT vs PWM. Gürültü bağışıklığı,
   hız, kablo uzunluğu, CRC/parite desteği. Uzun kablo gerekiyorsa ne yapılmalı?
7. Enkoder verisinin doğruluğunu sahada doğrulamak için pratik test yöntemleri
   (referans enkoderle karşılaştırma, sabit hız testi, FFT ile harmonik analizi).

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
