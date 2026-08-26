# Ekosistem Taraması: Açık Kaynak ve Ticari Haptik Kontrolörler


Sen teknoloji ekosistemi ve ürün mühendisliği konusunda derin araştırma yapan bir uzmansın.

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

**Araştırılacaklar:**

1. **Scott Bezek SmartKnob View.** Firmware mimarisini kod seviyesinde incele:
   kontrol döngüsünün yapısı ve hızı, PID/empedans parametreleri, ölü bölge,
   idle davranışı, sensör okuma stratejisi, çekirdek/görev bölüşümü, ekranla
   nasıl bir arada çalıştığı. Hangi tasarım kararları neden alınmış?
   Bilinen sorunları ve topluluk çözümleri neler?
2. **Diğer açık kaynak haptik knob projeleri.** (Örneğin: Haptic Knob varyantları,
   SmartKnob türevleri, Adafruit/Seeed örnekleri, akademik prototipler.)
   Her biri için: donanım seçimi, kontrol yaklaşımı, ulaşılan kalite, öne çıkan fikirler.
3. **Genel FOC firmware'leri.** ODrive, VESC, SimpleFOC, MotorGo, Mechaduino,
   TinyMovr, MJBots moteus: mimari, kontrol döngüsü hızı, gözlemleyici seçimi,
   kalibrasyon prosedürleri, haptik/empedans kontrolü desteği.
   **Bizim gibi düşük hızlı, yüksek çözünürlüklü tork uygulamasına** hangisi en uygun,
   hangi fikirleri ödünç alabiliriz?
4. **Ticari ürünler.** Otomotiv konsol kontrolörleri, DJ/ses ekipmanı jog wheel'ları,
   uçuş simülatörü kontrolleri, kamera odak kontrolleri, tıbbi cihaz döner kontrolleri.
   Yayımlanmış teknik detay, patent veya teardown bilgisi bulunan örnekleri incele.
   Hangi haptik efektler standart hâline gelmiş (çentik, sanal duvar, doku,
   viskoz sürtünme, atalet hissi, yay merkezleme)?
5. **Haptik efekt kütüphanesi.** Bir haptik knob'da uygulanabilecek efektlerin
   kapsamlı listesi ve her birinin matematiksel modeli. Efektler arası geçişte
   süreksizlik (jerk) nasıl önlenir?
6. **Ne yapmıyoruz?** Yukarıdaki kaynaklara bakarak, tarif edilen sistemin
   **eksik veya geliştirilebilir** yanlarını öncelik sırasıyla listele.
   Her madde için: beklenen kazanç, uygulama zorluğu, risk.

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
