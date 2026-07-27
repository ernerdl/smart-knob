# AS5600 Braketi — baskı & montaj notları

Motorun arka yüzüne vidalanan, AS5600'ü mıknatıs karşısına tutan 3 katlı ara parça.

## Dosyalar (hepsi CC2, ASA/PETG)
| Dosya | Hava boşluğu | Çip yüksekliği | Ne zaman |
|-------|-------------|------------------|----------|
| `as5600_braket_gap1.5.stl` | **1.5mm** | 5.55mm | **Önce bunu bas** (ideal) |
| `as5600_braket_gap1.0.stl` | 1.0mm | 5.05mm | Sensör "çok uzak" derse |
| `as5600_braket_gap2.0.stl` | 2.0mm | 6.05mm | Sensör "çok yakın" derse |

## Motor tarafı → M2 VİDA + üstten ERİŞİM kanalı
- Tabanda 4 delik **2.4mm** (13.30mm kare, M2 geçer) → motora vidalanır.
- Her deliğin üstünde **4.8mm erişim kanalı** (tornavida + vida üstten geçer).
- **Montaj sırası:** ÖNCE tabanı motora vidala (erişim kanalından tornavidayla), SONRA AS5600'ü snap ile tak. Vida kafası taban üstüne oturur, kanal içinde kalır.

**AS5600 tarafı → RİM + GÖMÜK CEP + SNAP-FIT** (kart altındaki çip+parçalar için boşluk):
- Kart **düz yüzeye oturmaz** — sadece **1.3mm ince çevre kenarına (rim)** oturur.
- Ortada **2.2mm derin gömük cep (recess)** var → kartın alt yüzündeki çip + SMD parçalar burada boşlukta kalır, hiçbir yere değmez.
- Ortada **14mm şaft/mıknatıs boşluğu** (tüm yükseklik boyunca).
- Şaft deliği etrafında **1.5mm kalın halka (kolar)** — 14mm iç, 17mm dış, 1.5mm yüksek → merkez takviye/şaft hizası.
- Kart: **23.29 × 22.70mm** (dikdörtgen), kalınlık 1.6mm varsayıldı.
- Snap klipsler **+X, −X, +Y**; **−Y açık** (pin header + kablo). Header −Y'de değilse söyle.
- **Çip aşağı (mıknatısa)** bakacak şekilde tak.
- Kart takarken hafif bastır → klipsler esner, kart rim'e oturur, dudaklar üstten tutar. Vida yok.

## ⚠ Baskı yönü (önemli)
- Çubuklar aşağı bakıyor (−Z). Slicer'da **ters çevir: çubuklar YUKARI** baksın → çubuklar dik, desteksiz basar.
- Bu yönde taban (32mm) platformdan (24mm) ~4mm taşar, 1.55mm boşluk üstünde köprü olur → CC2 genelde köprüler; gerekirse **sadece o bölgeye hafif destek**.

## Ölçüler (parametrik, hepsi mm)
- Taban: 32mm çap disk, 2mm kalın, merkez 14mm boşluk (mıknatıs+şaft için)
- Motor vida delikleri: **13.30mm kare**, delik 2.2mm (M2 rahat geçer)
- 4 kolon: 16mm kare düzende, 4×4mm kesit
- Platform: 2mm kalın, 24mm kare, AS5600 delikleri **16mm kare** 2.2mm, merkez 11mm boşluk
- Kablo boşluğu: −Y kenarında 6mm slot

## Kritik yükseklik mantığı
- Mıknatıs motor gövdesinden **4.05mm** çıkıntılı.
- Çip mıknatısa **1.5mm** olmalı → çip yüzü motordan **5.55mm** yukarıda.
- **AS5600, çip mıknatısa BAKACAK şekilde takılacak** (çip alt yüzde). Kolon yüksekliği buna göre.
- Yükseklik türetildi: `chip_z = mıknatıs(4.05) + hava(1.5)`; kolon = chip_z − taban(2) − platform(2).

## Montaj + kalibrasyon
1. gap1.5'i bas, taban altındaki 4 çubuğu motorun arka M2 deliklerine **bastırarak otur** (vida yok).
2. AS5600'ü platforma vidala, çip aşağı (mıknatısa) baksın.
3. Kod ile **AS5600 AGC register** oku:
   - AGC ~128 civarı (orta) ideal.
   - AGC düşük/min → mıknatıs çok yakın → gap2.0 bas.
   - AGC yüksek/max → çok uzak → gap1.0 bas.
4. Mıknatıs merkez hizası önemli — kolonlar mil ekseninde tutuyor.

## Yeniden üretim
Fusion Python script parametrik (MCP ile üretildi). Ölçü değişirse (motor farklı gelirse) `air_gap`, `motor_off`, delik çapları gibi parametreleri güncelleyip yeniden bas.
