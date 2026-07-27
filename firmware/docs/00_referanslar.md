# SmartKnob — Referans Repolar & Kaynaklar

Tek bakışta hangi kaynak ne işe yarıyor. Önem sırasıyla.

## Firmware / algoritma (asıl kullanılacaklar)

| # | Kaynak | Ne için | Not / dikkat |
|---|--------|---------|--------------|
| 1 | **scottbez1/smartknob** | Virtual **detent + endstop** matematiği (torque = f(açı farkı)), closed-loop torque | Çekirdek: `firmware/src/motor_task.cpp`. Transport'u **protobuf** — BİZ KULLANMIYORUZ (bizde metin `SET_PROFILE`). Sürücü TMC6300 — bizde L298N + SimpleFOC. Sadece his matematiğini al. |
| 2 | **dmcke5/Hapticpad** | **Momentum** (kendi kendine dönme) modu, 3 haptik mod, forma birebir | RP2040 + TMC6300, C++, XML config (SD). MCU/sürücü farklı → momentum algoritmasını uyarla. PCB/STL/STEP hazır. |
| 3 | **cnfatal/esp32-cdc-keyboard** | ESP32-S3 **composite HID klavye + CDC** reçetesi | Bizim doğruladığımız USB mimarisiyle birebir. USB_MODE=0 (TinyUSB). |
| 4 | **katbinaris/zeroone** | Vue.js config arayüzü (ileride) | Kullanıcı Vue biliyor. |
| — | **katbinaris/nanofoc_devkit** | İleride PCB döktürülürse | ESP32-S3 + FOC devkit. |

## Kütüphane
- **Arduino-SimpleFOC** — motor sürüş (FOC), torque/voltage kontrol modu. L298N için `driver.voltage_limit` düşük başlat.

## 3D model / mekanik
- **scottbez1/AS5600Knob** (GitHub) — basit AS5600 knob, Fusion kaynak açık.
- **AS5600 Encoder Mount for 2208 Gimbal** (MakerWorld) — sensör braketi, mıknatısı merkezler, 2805'e uyarlanır.
- **mauroalfieri AS5600 Magnetic Knob** (Thingiverse) — STL + Fusion.
- **MaTouch SmartKnob** (Makerfabs) — tam kasa+knob STL (superdial türevi).
- **GrabCAD "FlyCat 2805"** — motorun birebir 3D modeli → Fusion'a import, case çiz.

## Mac tarafı (repo değil, araç)
- **Hammerspoon** — TEK Mac beyni. Aktif uygulamayı algılar → (a) seri porttan `SET_PROFILE` ile his profilini gönderir, (b) knob'un gönderdiği F16-18 tuşlarını aktif app'e göre gerçek kısayola çevirir.
- ~~Karabiner-Elements~~ — **ATILDI.** Gerekçe: firmware'i biz kontrol ediyoruz, knob "aptal klavye" değil. Karabiner'ın app-bazlı remap + çift/üç tık türetme özellikleri bizde gereksiz; bağlamı Hammerspoon veriyor, jestleri firmware algılıyor. Tek gerçek kazancı kernel-level remap sağlamlığıydı ama remap yapmıyoruz.
- Detaylar ayrı brief: `02_mac_companion_app_brief.md` (Hammerspoon-only olarak yazılacak).

## Knob tuşları (HID)
- **F16 = CW, F17 = CCW, F18 = tık.** Seçim nedeni: F1-F12 medya/sistem, F13=PrintScreen, F14/F15=parlaklık → hepsi dolu. F16-F18'in macOS'ta varsayılanı yok, temiz.
- **Jestler firmware'de:** çift/üç tık, uzun bas knob üstünde algılanıp ayrı tuş/olay olarak gönderilecek (Karabiner delayed_action'a gerek yok).

## Özet mimari
his algoritması **smartknob** · momentum **Hapticpad** · motor sürüş **SimpleFOC** · composite USB **cnfatal örneği** · transport (SET_PROFILE + F16-18 HID) **bize özgü** · bağlam + tuş çevirisi **sadece Hammerspoon** · jest algılama **firmware**.
