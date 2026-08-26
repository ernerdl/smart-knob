# Sürücü Donanımı, Tork Çözünürlüğü ve Akım Kontrolü


Sen güç elektroniği ve motor sürücü tasarımı konusunda derin araştırma yapan bir uzmansın.

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

1. **Voltaj-mod tork ile akım-mod tork farkı.** Akım döngüsü olmadan
   (voltaj = tork varsayımıyla) çalışmanın haptik his üzerindeki etkileri:
   sıcaklıkla direnç değişimi, geri-EMK'nın hızla torku bozması, endüktans
   kaynaklı faz gecikmesi. Hangi hız/tork aralığında bu varsayım kabul edilebilir?
2. **Minimum tork çözünürlüğü.** Sürücünün ölü zamanı (bizde 300 ns), gerilim düşümü
   (1A'de 1.8 V) ve PWM çözünürlüğü, üretilebilen **en küçük anlamlı torku** nasıl
   belirliyor? Bunu hesaplamak için bir yöntem ver. Merkeze yakın çok küçük tork
   gerektiğinde bu kabalık nasıl bir davranışa yol açar?
3. **Ölü zaman etkileri ve telafisi.** Dead-time distortion, sıfır geçiş bozulması,
   dead-time compensation algoritmaları. Haptik uygulamada bunun hissedilir etkisi olur mu?
4. **Sürücü karşılaştırması.** L6234 (entegre yarı-köprü, DMOS), TMC6300, DRV8313,
   DRV8316, MP6540, ayrık MOSFET + gate sürücü: haptik uygulama için
   ölü zaman, düşüm, gürültü, akım ölçüm desteği, minimum tork çözünürlüğü
   açısından tablo hâlinde karşılaştır. Neden birçok haptik knob projesi TMC6300 seçiyor?
5. **"Kötü sürücü daha kararlıydı" gözlemi.** Yüksek çıkış empedanslı, yavaş,
   kayıplı bir sürücünün (L298N) kapalı döngü kararlılığına katkısını fiziksel olarak
   açıkla. Bu etkiyi bilinçli olarak yeniden üretmenin yolları var mı
   (seri direnç, çıkış filtresi, kasıtlı fiziksel sönüm)? Ödünleşimleri neler?
6. **Akım ölçümü ekleme.** Faz akımını ölçmek (INA240 tipi yüksek taraf/inline
   akım-şönt amplifikatörü) haptik hissi nasıl değiştirir? Shunt değeri seçimi,
   yerleşim, bant genişliği, PWM senkronlu örnekleme gereksinimi.
   Akım döngüsü eklemek yukarıdaki kararlılık problemlerini çözer mi yoksa yeni mi ekler?

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
