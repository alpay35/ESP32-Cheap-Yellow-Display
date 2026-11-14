# ESP32 Cheap Yellow Display (CYD) HW-458 / CYD3 - Dokunmatik Kalibrasyon ve Çizim Paneli

Bu örnek, **ESP32-2432S028R Revizyon HW-458 (TPM408-2.8)** ekranlı Cheap Yellow Display kartı için özel olarak geliştirilmiş resmi ve kararlı dokunmatik kalibrasyon ve çizim uygulamasıdır.

## 🌟 Özellikler
- **Bulut & Yerel IDE Uyumluluğu:** `Adafruit_GFX`, `Adafruit_ILI9341` ve `XPT2046_Touchscreen` standart kütüphaneleriyle çalışır. `User_Setup.h` gibi yerel dosya düzenlemelerine ihtiyaç duymaz, Arduino Cloud Web Editor'de doğrudan derlenir.
- **Donanımsal Çift SPI Ayrımı:**
  - **TFT Ekran:** Donanımsal HSPI (`SCLK=14`, `MISO=12`, `MOSI=13`, `CS=15`, `DC=2`, `BL=21`)
  - **XPT2046 Dokunmatik:** Donanımsal VSPI (`CLK=25`, `MISO=39`, `MOSI=32`, `CS=33`)
  - **MicroSD:** Pasif (`CS=5`, HIGH)
- **V4 Anti-Ghosting & Donanım Filtreleri:**
  - **4095 Açık Devre Tersinirlik Çözümü:** Panel boşta açık devre iken $Z = 4095$ verir; gerçek temasta $Z$ değeri $2800..3750$ seviyesine düşer.
  - **Out-of-Bounds Rejection:** Ekran dışı parazit ADC değerleri $(0,0)$ pikseline yuvarlanmaz, doğrudan filtrelenir.
  - **Bodmer Kararlılık Testi:** 4ms aralıklı çift örnek doğrulaması ile tek döngülük parazit atlamaları elenir.
  - **Anti-Jump Filtresi:** Çizim esnasında 45 pikselden fazla olan parazit sıçramalarında istenmeyen bağlantı çizgisi çekilmez.
- **Canlı Çizim Paneli:**
  - `[TEMIZLE]` butonu (Kırmızı)
  - `[KALIBRE]` butonu (Mavi - 2 saniye geri sayımlı 4 köşe kalibrasyon sihirbazı)
  - Kart üzerindeki `BOOT` butonuna basılarak da her an kalibrasyon yeniden başlatılabilir.
- **Kalıcı Hafıza (NVS):** Yapılan kalibrasyon ESP32'nin flash hafızasına kaydedilir (`Preferences` / `cyd_touch_v4`).

## 🚀 Kullanım
1. `CYD_HW458_Touch_Calibration.ino` dosyasını Arduino Cloud veya Arduino IDE'de açın.
2. Kart olarak **ESP32 Dev Module** seçin.
3. Karta yükleyin (`Upload`).
4. Ekran doğrudan çizim paneliyle açılır ve kalemi birebir takip eder. İnce kalibrasyon için ekrandaki `[KALIBRE]` butonuna dokunabilirsiniz.
