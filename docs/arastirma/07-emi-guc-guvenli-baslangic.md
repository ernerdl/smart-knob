# EMI, Güç Dağıtımı, Kart Tasarımı ve Güvenli Başlangıç Durumları


Sen karışık sinyalli kart tasarımı ve EMC konusunda derin araştırma yapan bir uzmansın.

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

Ek bilgi: Devre şu an delikli plaket (perfboard) üzerinde elle lehimli.
12V besleme, buck ile 5V/3.3V. Sürücünün girişleri MCU'ya 1k seri dirençlerle bağlı.
Sürücünün enable pini doğrudan 3.3V'a bağlı — bu yüzden MCU reset'teyken sürücü
aktif kalıyor ve girişler yüksek empedansta olduğu için motora tanımsız akım geçiyor
(güç kaynağı ~0.5 s akım sınırına giriyor).

**Araştırılacaklar:**

1. **Güvenli başlangıç durumu (safe state) tasarımı.** MCU reset, brownout, firmware
   yükleme ve besleme sırası (power sequencing) durumlarında motor sürücüsünün
   güvende kalması için standart uygulamalar. Pull-up/pull-down seçimi, enable
   mantığı, ayrı bir denetleyici (supervisor) veya watchdog kullanımı.
   Endüstriyel sürücülerde STO (Safe Torque Off) nasıl uygulanıyor, basit bir
   masaüstü cihaz için hangi kısmı anlamlı?
2. **MCU GPIO reset davranışı.** Neden yüksek empedans varsayılan? Strapping pinleri,
   dahili zayıf pull dirençleri, bootloader süresince pin durumları.
   Sürücü enable hatları için önerilen direnç değeri ve yerleşimi.
3. **Akım döngüsü alanı ve dönüş yolu.** Motor faz akımları ve PWM anahtarlamanın
   yaydığı gürültüyü azaltmak için yerleşim kuralları. Perfboard/elle lehim
   kısıtlarında uygulanabilir pratik teknikler (yıldız topraklama, bükülü çiftler,
   dönüş yolunu gidiş yolunun yanında tutmak).
4. **Besleme ayırma (decoupling).** Sürücü bulk kondansatörü seçimi ve yerleşimi,
   MCU ve sensör için ayırma stratejisi, ferrit boncuk kullanımı ve tuzakları
   (rezonans, DC direnç). Sensörü motor rayından ayırmanın doğru yolu.
5. **Anahtarlama gürültüsünün hassas sensöre kuplajı.** Kapasitif, endüktif ve
   iletken kuplajı ayırt etme yöntemleri. Hangisi hangi belirtiyi verir?
   Osiloskopla teşhis prosedürü (nereye prob, ne aranır).
6. **PWM frekansı seçimi:** duyulabilir uğultu (20 kHz üstü), anahtarlama kaybı,
   ölü zaman oranı, gürültü spektrumu arasındaki ödünleşim. Haptik uygulamada önerilen aralık.
7. **Perfboard'dan PCB'ye geçiş.** Bu tip bir karışık sinyal kartında iki katman
   yeterli mi, dört katman ne kazandırır? Kritik yerleşim kararları sıralı liste hâlinde.

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
