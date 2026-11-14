# ESP32 Cheap Yellow Display (CYD) HW-458 - Canlı Donanım Teşhis Aracı (Diagnostics)

Bu araç, **ESP32-2432S028R HW-458** kartının dokunmatik ekran kontrolcüsü olan **XPT2046** ve ekranının tüm donanım registerlarını saniyede 10 kez canlı olarak okuyup hem Seri Porttan (115200 baud) basan hem de LCD ekranda gösteren teşhis yazılımıdır.

## 🔍 Görüntülenen Parametreler
- **IRQ (Pin 36):** Dokunmatik donanım kesme pini durumu.
- **P.Z Basınç:** `XPT2046_Touchscreen` kütüphanesinin hesapladığı basınç değeri ($Z = Z_1 + 4095 - Z_2$).
- **P.X / P.Y:** Kütüphanenin okuduğu ham koordinatlar.
- **Ham Z1 ve Z2:** XPT2046 `0xB1` ve `0xC1` komutlarıyla okunan ham basınç registerları.
- **Ham RX ve RY:** XPT2046 `0x91` ve `0xD1` komutlarıyla okunan ham analog ADC değerleri.

## 🚀 Kullanım
1. `CYD_HW458_Diagnostics.ino` dosyasını Arduino IDE / Cloud ile yükleyin.
2. Seri Port Monitörünü 115200 baud hızında açın.
3. Ekrana dokunarak ve bırakarak tüm ham analog değerlerin değişimini anlık olarak izleyin.
