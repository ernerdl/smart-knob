# Derin Araştırma Promptları — FOC, Haptik Kontrol, Endüstriyel Uygulamalar

Her bölüm **ayrı bir oturuma** yapıştırılacak şekilde, kendi başına yeterli yazıldı.
Sıra önemli değil; paralel çalıştırılabilirler.

Her promptun sonunda ortak bir **çıktı sözleşmesi** var — araştırmaların sabah
karşılaştırılabilir olması için onu değiştirme.

---

## ORTAK BAĞLAM BLOĞU

> Aşağıdaki promptların çoğu bu bloğa atıf yapıyor. Prompta zaten gömülü, ayrıca
> yapıştırmana gerek yok. Buraya referans olsun diye konuldu.

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

**Çözülememiş üç gözlem (araştırmalar bunlara ışık tutmalı):**
1. Sönümü artırmak titremeyi **artırıyor**: 0.15 → 2 çırpıntı, 0.35 → 14, 0.55 → 47 (aynı testte)
2. Kontrol döngüsünü 9600 Hz'e çıkarmak titreme yarattı; 4000 Hz daha iyiydi; **2000 Hz daha kötüydü** (monoton değil)
3. Aynı firmware daha kötü bir sürücüyle (L298N, ~3V düşüm, yavaş bipolar çıkış) daha kararlıydı

---

## 1) Örneklenmiş Haptik Sistemlerde Kararlılık ve Pasiflik Sınırları

Sen kontrol teorisi ve haptik arayüzler konusunda derin araştırma yapan bir uzmansın.
Aşağıdaki sistem için literatürü tara ve **matematiksel olarak sağlam** bir cevap üret.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 2) Nicemlenmiş Enkoderden Hız Tahmini ve Gözlemleyiciler

Sen hareket kontrolü (motion control) ve durum tahmini konusunda derin araştırma
yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 3) Gerçek Zamanlı Kontrol Yazılımı Mimarisi (MCU / RTOS / Zamanlama)

Sen gömülü gerçek zamanlı sistemler ve motor kontrolü firmware mimarisi konusunda
derin araştırma yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 4) Sürücü Donanımı, Tork Çözünürlüğü ve Akım Kontrolü

Sen güç elektroniği ve motor sürücü tasarımı konusunda derin araştırma yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 5) Manyetik Açı Sensörleri: Seçim, Montaj, Hata Kaynakları

Sen manyetik konum sensörleri ve hassas ölçüm konusunda derin araştırma yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 6) Motor Seçimi, Cogging ve Mekanik Tasarım

Sen elektrik makineleri ve hassas mekanik tasarım konusunda derin araştırma yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 7) EMI, Güç Dağıtımı, Kart Tasarımı ve Güvenli Başlangıç Durumları

Sen karışık sinyalli kart tasarımı ve EMC konusunda derin araştırma yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## 8) Ekosistem Taraması: Açık Kaynak ve Ticari Haptik Kontrolörler

Sen teknoloji ekosistemi ve ürün mühendisliği konusunda derin araştırma yapan bir uzmansın.

**Sistem:** [ORTAK BAĞLAM BLOĞU'nu buraya yapıştır]

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

[ÇIKTI SÖZLEŞMESİ]

---

## ÇIKTI SÖZLEŞMESİ

> Bu bloğu her promptun sonuna aynen ekle.

Cevabını şu yapıda ver:

1. **Kısa özet** — en fazla 10 madde, her biri tek cümle. Sabah hızlı okumak için.
2. **Ana bulgular** — her bulgu için: iddia, dayanak (kaynak + neden güvenilir),
   ve bizim sistemimize ne anlama geldiği. Formül varsa terimlerini açıkla.
3. **Somut öneriler** — uygulanabilir adımlar hâlinde, **öncelik sırasıyla**.
   Her öneri için: beklenen kazanç, uygulama maliyeti (süre/para/karmaşıklık),
   risk, ve **nasıl ölçüleceği** (hangi test, hangi metrik, başarı eşiği).
4. **Çelişkiler ve belirsizlikler** — kaynaklar arasında çelişki varsa açıkça belirt.
   Emin olmadığın yerleri "emin değilim" diye işaretle; uydurma.
5. **Kaynakça** — bağlantı, yazar/kurum, tarih. Akademik makale, üretici uygulama notu,
   datasheet ve ciddi mühendislik bloglarını önceliklendir; forum yorumlarını
   ancak başka kaynak yoksa ve etiketleyerek kullan.

**Kurallar:**
- Türkçe yaz. Teknik terimlerin İngilizcesini ilk geçtiği yerde parantez içinde ver.
- Sayı ver. "Daha iyi" değil, "%30 daha az" veya "0.005–0.01 s aralığında".
- Bizim üç çözülememiş gözlemimize (ortak bağlam bloğunun sonunda) değinebiliyorsan değin.
- Bir şeyi doğrulayamadıysan bunu yazmaktan çekinme. Boşluğu tahminle doldurma.
