# Gerçek Zamanlı Kontrol Yazılımı Mimarisi (MCU / RTOS / Zamanlama)


Sen gömülü gerçek zamanlı sistemler ve motor kontrolü firmware mimarisi konusunda
derin araştırma yapan bir uzmansın.

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

Ek bilgi: Kontrol döngüsü şu an Arduino `loop()` içinde, aynı sırada USB HID,
buton okuma ve seri komut işleme de var. Ekran (SPI TFT) ayrı bir FreeRTOS
görevinde, ikinci çekirdekte. Kontrol döngüsünü kendi çekirdeğine taşıma denemesi
titreme yarattı ve geri alındı.

**Araştırılacaklar:**

1. **Zamanlama jitter'ı.** Kontrol döngüsünde periyot düzensizliğinin (jitter)
   türev/sönüm terimlerine etkisini nicel olarak açıkla. Ne kadar jitter kabul edilebilir?
   Sabit periyot neden bu kadar önemli, ve "hızlı ama düzensiz" ile "yavaş ama düzenli"
   arasındaki tercihi literatür nasıl yapıyor?
2. **Doğru mimari.** Endüstriyel servo sürücülerde kontrol döngüsü nasıl kurulur?
   Timer tetiklemeli ISR mi, RTOS görevi mi, bare-metal süper döngü mü?
   ADC örneklemesinin PWM'e senkronlanması (center-aligned PWM, ADC trigger)
   neden standart? Bizim gibi akım ölçümü olmayan bir sistemde bu senkronizasyon
   yine de değerli mi?
3. **ESP32/ESP32-S3 özelinde:** çift çekirdek kullanımında motor kontrolü için
   önerilen desen nedir? Çekirdekler arası paylaşılan değişkenlerde bellek tutarlılığı,
   önbellek, `volatile` yeterliliği, atomik erişim. Kesme gecikmesi ve WiFi/USB
   yığınının kesmeleri bloke etmesi konusunda bilinen tuzaklar.
4. **FreeRTOS tuzakları:** öncelik ters çevrilmesi, idle watchdog, `vTaskDelay`
   granülaritesi (tick 1 ms), yüksek öncelikli sıkı döngünün diğer görevleri açlığa
   sokması. Motor kontrolü için doğru öncelik ve yield stratejisi.
5. SimpleFOC'un tek döngü (`loopFOC()` + `move()`) modeli, gerçek zamanlı kısıtlar
   açısından ne kadar sağlam? Toplulukta bilinen sınırlamalar ve önerilen düzeltmeler.
6. Aynı MCU üzerinde ekran çizimi (SPI), USB HID ve motor kontrolünü bir arada
   çalıştıran projeler nasıl bölüştürüyor? DMA kullanımı, çizimi parçalara bölme,
   çift tamponlama.

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
