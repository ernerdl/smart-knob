# Motor Seçimi, Cogging ve Mekanik Tasarım


Sen elektrik makineleri ve hassas mekanik tasarım konusunda derin araştırma yapan bir uzmansın.

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

1. **Haptik için ideal motor.** Bir haptik döner düğmede motordan istenen özellikler:
   düşük cogging, düşük atalet, yüksek tork/atalet oranı, düşük sürtünme, düşük
   tork dalgalanması. Gimbal BLDC'ler neden tercih ediliyor? Alternatifler
   (coreless/ironless BLDC, frameless torque motor, DC motor + kapsül, galvanometre)
   hangi durumlarda daha iyi?
2. **Cogging torku.** Kaynağı, ölçüm yöntemi, harmonik yapısı (kutup–oluk sayısına
   bağlılığı). Haptik hissi nasıl bozar? **Yazılımla telafi** yöntemleri:
   açıya bağlı ileri besleme haritası çıkarma, otomatik kalibrasyon prosedürü,
   harmonik model uydurma. Gerçek uygulamalarda ne kadar iyileştirme sağlanıyor?
3. **Tork dalgalanması (torque ripple).** Kaynakları: cogging, geri-EMK'nın
   sinüzoidal olmaması, faz akımı dengesizliği, ölü zaman. Her birinin telafisi.
4. **Atalet ve rezonans.** Rotor ataleti + düğme kütlesi ile sanal yayın oluşturduğu
   rezonans frekansını hesaplama. Bu frekansın kontrol bant genişliğine göre nerede
   olması gerekir? Ataleti artırmak (daha ağır düğme) kararlılığı iyileştirir mi?
5. **Mekanik oyun (backlash) ve esneklik.** Mıknatıs–mil, mil–yatak, düğme–mil
   bağlantılarındaki oyunun kapalı döngüde nasıl limit cycle ürettiğini açıkla.
   Tespit yöntemi ve çözümleri.
6. **Yatak seçimi:** bilyeli, kaymalı, önyüklemeli. Sürtünmenin haptik histeki rolü —
   biraz fiziksel sürtünme kararlılığa yardım eder mi (bkz. pasiflik kriterindeki
   fiziksel sönüm terimi)? Kasıtlı sönüm eklemek (viskoz gres, manyetik fren) mantıklı mı?
7. Ticari haptik knob'larda (otomotiv konsol kontrolörleri, uçuş simülatörü
   kontrolleri, profesyonel ses ekipmanı jog wheel'ları) kullanılan mekanik yapılar.

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
