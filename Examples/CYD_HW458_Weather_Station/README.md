# ESP32 Cheap Yellow Display (CYD) HW-458 - Canlı Hava Durumu İstasyonu

Bu proje, **ESP32 Cheap Yellow Display (CYD) HW-458 / ESP32-2432S028R** geliştirme kartı üzerinde çalışan, internetten canlı veri çeken interaktif bir **Hava Durumu İstasyonu ve Canlı Saat** uygulamasıdır.

## 🌟 Öne Çıkan Özellikler
- **100% Ücretsiz ve Anahtarsız:** `Open-Meteo` hava durumu API'si kullanılmıştır. Kayıt olmak veya API anahtarı (API Key) almak gerekmez, anında çalışır.
- **Canlı İnternet Saati & Tarihi (NTP):** Türkiye yerel saatine (UTC+3) göre saniye saniye canlı saat ve tarih göstergesi.
- **Kapsamlı Anlık Veriler:**
  - Anlık Sıcaklık (°C) ve Hissedilen Sıcaklık
  - Bağıl Nem Oranı (%)
  - Rüzgar Hızı (km/h)
  - Atmosferik Yüzey Basıncı (hPa)
  - Canlı Vektörel Hava Simgeleri (Güneş, Bulut, Yağmur, Kar, Şimşek, Sis)
  - Türkçe Hava Durumu Tanımı (Örn: *Parçalı Bulutlu*, *Güneşli*, *Sağanak Yağış*)
- **3 Günlük Tahmin Kartları:**
  - Bugün, Yarın ve Sonraki Gün için Min / Max sıcaklıklar ve durum simgeleri.
- **Dokunmatik Şehir Seçici:**
  - Ekranın altındaki dokunmatik butonlarla tek dokunuşla şehirler arası geçiş:
    - `[ IZMIR ]` *(Varsayılan)*
    - `[ ISTANBUL ]`
    - `[ ANKARA ]`
    - `[ ↻ ]` Anlık manuel yenileme butonu.
- **Otomatik Güncelleme:** Her 15 dakikada bir otomatik yenileme. Kart üzerindeki `BOOT` tuşuna basılarak da anında yenilenebilir.
- **V4 Anti-Ghosting Dokunmatik Filtresi:** Donanımsal parazitleri ve hayalet tıklamaları engelleyen optimize edilmiş filtre.

## 🛠 Donanım ve Pin Bağlantıları (HW-458)
- **TFT LCD:** Donanımsal HSPI (`SCLK=14`, `MISO=12`, `MOSI=13`, `CS=15`, `DC=2`, `BL=21`)
- **XPT2046 Dokunmatik:** Donanımsal VSPI (`CLK=25`, `MISO=39`, `MOSI=32`, `CS=33`)
- **SD Kart CS:** GPIO 5 (Pasif = HIGH)

## 📦 Gerekli Kütüphaneler
Arduino Cloud Web Editor veya Arduino IDE Kütüphane Yöneticisi üzerinden kurulabilir:
1. `Adafruit GFX Library`
2. `Adafruit ILI9341`
3. `XPT2046_Touchscreen` (Paul Stoffregen)
4. `ArduinoJson` (v6 veya v7)

## 🚀 Kurulum ve Çalıştırma
1. `CYD_HW458_Weather_Station.ino` dosyasını açın.
2. Kodun başındaki WiFi ayarlarını kendi ağınıza göre güncelleyin:
   ```cpp
   const char* WIFI_SSID     = "WIFI_ADINIZ";
   const char* WIFI_PASSWORD = "WIFI_SIFRENIZ";
   ```
3. Kart olarak **ESP32 Dev Module** seçin ve yükleyin (`Upload`).
4. Kart WiFi'ye bağlandıktan sonra saati eşitleyecek ve hava durumu panelini ekrana getirecektir.
