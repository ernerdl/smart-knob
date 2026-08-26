# Örneklenmiş Haptik Sistemlerde Kararlılık ve Pasiflik Sınırları


Sen kontrol teorisi ve haptik arayüzler konusunda derin araştırma yapan bir uzmansın.
Aşağıdaki sistem için literatürü tara ve **matematiksel olarak sağlam** bir cevap üret.

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

1. **Pasiflik kriteri.** Colgate & Schenkel'in örneklenmiş veri haptik gösterimler için
   pasiflik koşulunu (tipik olarak `b > K·T/2 + B/2` formundaki bağıntı) tam olarak,
   türetimiyle ve terimlerin fiziksel karşılıklarıyla açıkla. Burada fiziksel sönüm,
   sanal sertlik, örnekleme periyodu ve sanal sönüm nasıl yer alıyor?
2. **Sanal sönümün kararsızlaştırıcı olması.** Yukarıdaki 1 numaralı gözlem
   (sönüm arttıkça titreme artıyor) literatürde nasıl açıklanıyor? Gecikmeli veya
   filtrelenmiş hız geri beslemesinin faz payına etkisi nedir? Hangi koşullarda
   sönüm terimi enerji **enjekte eder**?
3. **Örnekleme hızının monoton olmaması.** 2 numaralı gözlem: örnekleme hızını
   artırmak bazen kötüleştiriyor. Zero-order hold gecikmesi, hız türevindeki
   nicemleme gürültüsü ve filtre zaman sabiti arasındaki üç yönlü ödünleşimi
   modelle. Optimum örnekleme hızı için analitik bir ifade veya tasarım kuralı var mı?
4. **Z-width.** Bir haptik cihazın gösterebileceği empedans aralığı (Z-width) kavramını
   ve onu genişleten yöntemleri (fiziksel sönüm ekleme, akım geri beslemesi,
   sanal kuplaj, enerji gözlemcisi/pasiflik kontrolörü) karşılaştır.
5. **Enerji tabanlı yaklaşımlar.** Passivity observer / passivity controller (Hannaford
   & Ryu), time-domain passivity, energy bounding algorithm — bunlar bizim gibi
   akım ölçümü olmayan, voltaj-mod tork üreten basit bir sistemde uygulanabilir mi?
   Uygulanabilirse minimum gereksinimler ne?
6. **Ölü bölge.** Çentik merkezinde ölü bölge kullanmak kararlılığı nasıl etkiliyor?
   Literatürde limit cycle önleme yöntemi olarak nasıl geçiyor, dezavantajları neler?

**Ayrıca:** BMW iDrive, Audi MMI, Mercedes COMAND gibi ticari döner haptik
kontrolörlerin yayımlanmış patentlerinde veya teknik makalelerinde bu problemleri
nasıl çözdüklerine dair bilgi varsa çıkar.

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
