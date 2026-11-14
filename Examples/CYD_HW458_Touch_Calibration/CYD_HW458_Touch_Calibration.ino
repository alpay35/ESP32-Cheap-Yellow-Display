/***************************************************************************************
 * ESP32 CHEAP YELLOW DISPLAY (CYD) HW-458 / CYD3
 * RESMI XPT2046 DOKUNMATIK KALIBRASYON & CANLI CIZIM PANELI (V4 - ANTI-PARAZIT & GHOST FILTRELI)
 * 
 * Donanim / Kart: ESP32-2432S028R (Revizyon: HW-458 / TPM408-2.8)
 * Ekran Surucusu: ILI9341 (Donanimsal HSPI: SCLK=14, MISO=12, MOSI=13, CS=15, DC=2, BL=21)
 * Dokunmatik    : XPT2046 (Donanimsal VSPI: CLK=25, MISO=39, MOSI=32, CS=33)
 * 
 * V4 ANTI-GHOST FILTRELEME:
 * 1. Out-of-Bounds Discard: Ekran disindaki acik devre ADC degerleri (0,0)'a yuvarlanmaz,
 *    tamamen cope atilir.
 * 2. Cift Ornek Kararlilik Filtresi: Ardışık 2 okuma arasi fark > 30 ADC ise (gurultu ziplamasi)
 *    nokta cizilmez.
 * 3. Mesafe Kontrolu (Anti-Jump): Kalem bir anda 40 pikselden fazla ziplarsa hayalet
 *    cizgi baglantisi yapilmaz.
 * 4. Baslangic Debounce: Cizime baslamak icin en az 2 dongu kararli temas aranir.
 ***************************************************************************************/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

// ==========================================
// PIN TANIMLAMALARI (HW-458 / CYD3)
// ==========================================
#define TFT_BL     21   // Ekran Arka Isigi (HIGH = Acik)
#define TFT_CS     15   // Ekran Chip Select
#define TFT_DC      2   // Ekran Data / Command
#define TFT_MOSI   13   // Ekran MOSI
#define TFT_SCLK   14   // Ekran Clock
#define TFT_MISO   12   // Ekran MISO
#define TFT_RST    -1   // Reset (ESP32 EN pinine bagli)
#define SD_CS       5   // SD Kart CS (MISO cakismasini onlemek icin pasif)

#define XPT2046_CLK   25  // Dokunmatik SPI Saat
#define XPT2046_MOSI  32  // Dokunmatik SPI MOSI (DIN)
#define XPT2046_MISO  39  // Dokunmatik SPI MISO (DOUT)
#define XPT2046_CS    33  // Dokunmatik SPI Chip Select
#define BOOT_BTN       0  // Kart uzerindeki BOOT butonu

// ==========================================
// NESNELER VE DEGISKENLER
// ==========================================
SPIClass tftSPI(HSPI);
SPIClass touchSPI(VSPI);

Adafruit_ILI9341 tft = Adafruit_ILI9341(&tftSPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen ts(XPT2046_CS);
Preferences prefs;

// Donanim Katsayilari:
int cal_h_min = 1750;
int cal_h_max = 2380;
int cal_v_min = 2650;
int cal_v_max = 3050;
bool cal_swap_xy = false;

// Cizim Durumu ve Filtreleri
int lastDrawX = -1;
int lastDrawY = -1;
bool isDrawing = false;
int stableTouchStreak = 0;
unsigned long lastSerialLog = 0;

// ==========================================
// ANTI-GHOST GELISMIS DOKUNMA FILTRESI
// ==========================================
bool checkTouch(TS_Point &p) {
  TS_Point p1 = ts.getPoint();

  // 1. Basinc Kontrolu (Bosta 4095 cikar, basildiginda 2800..3700 araligina duser)
  if (p1.z < 200 || p1.z > 3750) {
    return false;
  }

  // 2. Fiziksel Ekran Sinirlari (Out-of-Bounds Discard)
  // Ekranin aktif temas alani X: 1680..2450, Y: 2550..3150 araligindadir.
  // Bu sinirlarin disindaki okumalar acik devre veya kenar gerilim parazitidir!
  if (p1.x < 1680 || p1.x > 2450 || p1.y < 2550 || p1.y > 3150) {
    return false;
  }

  // 3. Bodmer Kararlilik Testi (4ms sonra 2. ornek al)
  delay(4);
  TS_Point p2 = ts.getPoint();

  if (p2.z < 200 || p2.z > 3750) {
    return false;
  }
  if (p2.x < 1680 || p2.x > 2450 || p2.y < 2550 || p2.y > 3150) {
    return false;
  }

  // 4. Parazit Titreme Filtresi:
  // Gercek bir kalemde 4ms icinde koordinat en fazla 30 birim oynar.
  // Eger ADC yuzlerce birim zipliyorsa bu havadan kapilan parazittir!
  if (abs(p1.x - p2.x) > 35 || abs(p1.y - p2.y) > 35) {
    return false;
  }

  // Iki ornegin ortalamasini alarak yumusat
  p.x = (p1.x + p2.x) / 2;
  p.y = (p1.y + p2.y) / 2;
  p.z = (p1.z + p2.z) / 2;
  return true;
}

// Ham ADC degerini ekran pikseline cevir (Gecersizse false dondurur)
bool getPixelCoord(int rawX, int rawY, int &px, int &py) {
  int inX = cal_swap_xy ? rawY : rawX;
  int inY = cal_swap_xy ? rawX : rawY;

  // Gecerli kalibrasyon araliginda mi?
  int minX = min(cal_h_min, cal_h_max) - 100;
  int maxX = max(cal_h_min, cal_h_max) + 100;
  int minY = min(cal_v_min, cal_v_max) - 100;
  int maxY = max(cal_v_min, cal_v_max) + 100;

  if (inX < minX || inX > maxX || inY < minY || inY > maxY) {
    return false; // Ekran disi paraziti sifira (0,0) yuvarlama, yok say!
  }

  px = map(inX, cal_h_min, cal_h_max, 30, 290);
  py = map(inY, cal_v_min, cal_v_max, 30, 210);

  px = constrain(px, 0, 319);
  py = constrain(py, 0, 239);
  return true;
}

// ==========================================
// HEDEF (+) GRAFIGI CIZIMI
// ==========================================
void drawTarget(int x, int y, uint16_t color) {
  tft.drawCircle(x, y, 16, color);
  tft.drawCircle(x, y, 8, color);
  tft.drawFastHLine(x - 20, y, 41, color);
  tft.drawFastVLine(x, y - 20, 41, color);
}

// ==========================================
// 4 KOSE KALIBRASYON SIHIRBAZI (GERI SAYIMLI)
// ==========================================
void runCalibration() {
  Serial.println("\n==========================================");
  Serial.println(">>> 4 KOSE KALIBRASYON SIHIRBAZI BASLADI <<<");
  Serial.println("==========================================");

  tft.fillScreen(ILI9341_BLACK);

  tft.fillRect(0, 0, 320, 36, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE, ILI9341_NAVY);
  tft.setTextSize(2);
  tft.setCursor(18, 10);
  tft.print("DOKUNMATIK KALIBRASYON");

  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 80);
  tft.print("4 Kose Kalibrasyonu");
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, 115);
  tft.print("Sirayla belirecek kirmizi hedeflere basili tutun.");
  tft.setCursor(20, 135);
  tft.print("Hedef yesil olunca kalemi ekrandan kaldirin.");
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(20, 170);
  tft.print("Hazirlanin, 2 saniye icinde basliyor...");

  delay(2000);

  const int targetX[4] = { 30, 290, 290, 30 };
  const int targetY[4] = { 30,  30, 210, 210 };
  const char* names[4] = {
    "1/4: SOL-UST",
    "2/4: SAG-UST",
    "3/4: SAG-ALT",
    "4/4: SOL-ALT"
  };

  int sampleX[4];
  int sampleY[4];

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(ILI9341_BLACK);

    tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
    tft.setTextSize(2);
    tft.setCursor(65, 85);
    tft.print(names[i]);

    tft.setTextColor(ILI9341_LIGHTGREY, ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(65, 115);
    tft.print("Hedefe kalemi basili tutun...");

    drawTarget(targetX[i], targetY[i], ILI9341_RED);

    Serial.printf("\n[HEDEF %s] Dokunma bekleniyor...\n", names[i]);

    int validSamples = 0;
    long accX = 0, accY = 0;
    TS_Point p;

    while (validSamples < 5) {
      if (checkTouch(p)) {
        accX += p.x;
        accY += p.y;
        validSamples++;

        tft.fillRect(65, 140, 190, 24, ILI9341_DARKGREY);
        tft.setTextColor(ILI9341_GREEN, ILI9341_DARKGREY);
        tft.setTextSize(2);
        tft.setCursor(75, 144);
        tft.printf("ORNEK: %d / 5", validSamples);

        Serial.printf("  -> [HEDEF %s] Ornek %d/5: X:%d Y:%d Z:%d\n", names[i], validSamples, p.x, p.y, p.z);
        delay(60);
      } else {
        delay(20);
      }
    }

    drawTarget(targetX[i], targetY[i], ILI9341_GREEN);
    sampleX[i] = accX / 5;
    sampleY[i] = accY / 5;

    Serial.printf("[HEDEF %s ONAYLANDI] Ortalama X:%d, Y:%d\n", names[i], sampleX[i], sampleY[i]);

    tft.fillRect(20, 180, 280, 45, ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(45, 185);
    tft.print("ONAYLANDI! Kalemi ekrandan KALDIRIN.");

    while (checkTouch(p)) {
      delay(20);
    }
    Serial.println("  -> Kalem ekrandan kaldirildi.");

    if (i < 3) {
      for (int count = 2; count >= 1; count--) {
        tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
        tft.setCursor(45, 205);
        tft.printf("Sonraki hedefe geciliyor (%d sn)...   ", count);
        delay(750);
      }
    }
  }

  // Eksen Hesaplama
  int delta_x_horiz = sampleX[1] - sampleX[0];
  int delta_y_horiz = sampleY[1] - sampleY[0];
  bool new_swap = (abs(delta_y_horiz) > abs(delta_x_horiz));

  int new_h_min, new_h_max, new_v_min, new_v_max;
  if (!new_swap) {
    new_h_min = (sampleX[0] + sampleX[3]) / 2;
    new_h_max = (sampleX[1] + sampleX[2]) / 2;
    new_v_min = (sampleY[0] + sampleY[1]) / 2;
    new_v_max = (sampleY[2] + sampleY[3]) / 2;
  } else {
    new_h_min = (sampleY[0] + sampleY[3]) / 2;
    new_h_max = (sampleY[1] + sampleY[2]) / 2;
    new_v_min = (sampleX[0] + sampleX[1]) / 2;
    new_v_max = (sampleX[2] + sampleX[3]) / 2;
  }

  int spanX = abs(new_h_max - new_h_min);
  int spanY = abs(new_v_max - new_v_min);

  if (spanX > 200 && spanY > 150) {
    cal_h_min = new_h_min;
    cal_h_max = new_h_max;
    cal_v_min = new_v_min;
    cal_v_max = new_v_max;
    cal_swap_xy = new_swap;

    prefs.begin("cyd_touch_v4", false);
    prefs.putInt("h_min", cal_h_min);
    prefs.putInt("h_max", cal_h_max);
    prefs.putInt("v_min", cal_v_min);
    prefs.putInt("v_max", cal_v_max);
    prefs.putBool("swap_xy", cal_swap_xy);
    prefs.putBool("valid", true);
    prefs.end();

    Serial.println("\n[BASARILI] Yeni kalibrasyon katsayilari kaydedildi.");
  } else {
    Serial.println("\n[UYARI] Hedefler cok yakin algilandi! Varsayilan donanim degerleri korundu.");
  }

  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 320, 34, ILI9341_DARKGREEN);
  tft.setTextColor(ILI9341_WHITE, ILI9341_DARKGREEN);
  tft.setTextSize(2);
  tft.setCursor(20, 9);
  tft.print("KALIBRASYON TAMAM!");

  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 50);  tft.printf("H_MIN  : %d", cal_h_min);
  tft.setCursor(20, 75);  tft.printf("H_MAX  : %d", cal_h_max);
  tft.setCursor(20, 100); tft.printf("V_MIN  : %d", cal_v_min);
  tft.setCursor(20, 125); tft.printf("V_MAX  : %d", cal_v_max);
  tft.setCursor(20, 150); tft.printf("SWAP_XY: %s", cal_swap_xy ? "EVET" : "HAYIR");

  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, 190);
  tft.print("Cizim Paneli 2 saniye icinde aciliyor...");

  delay(2000);
}

// ==========================================
// CANLI CIZIM EKRANI ARAYUZU
// ==========================================
void initDrawingScreen() {
  tft.fillScreen(ILI9341_BLACK);

  // Ust Bilgi Cubugu (Mavi)
  tft.fillRect(0, 0, 320, 34, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE, ILI9341_NAVY);
  tft.setTextSize(2);
  tft.setCursor(10, 9);
  tft.print("CIZIM");

  // TEMIZLE Butonu (Kirmizi)
  tft.fillRoundRect(135, 4, 85, 26, 4, ILI9341_RED);
  tft.drawRoundRect(135, 4, 85, 26, 4, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE, ILI9341_RED);
  tft.setTextSize(1);
  tft.setCursor(150, 13);
  tft.print("TEMIZLE");

  // KALIBRE Butonu (Mavi)
  tft.fillRoundRect(225, 4, 85, 26, 4, ILI9341_BLUE);
  tft.drawRoundRect(225, 4, 85, 26, 4, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setTextSize(1);
  tft.setCursor(240, 13);
  tft.print("KALIBRE");

  // Koordinat Bilgi Cubugu
  tft.fillRect(0, 35, 320, 16, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_WHITE, ILI9341_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 39);
  tft.print("X: ---  Y: ---  Z: --- | Dokunarak cizin");

  lastDrawX = -1;
  lastDrawY = -1;
  isDrawing = false;
  stableTouchStreak = 0;

  Serial.println("\n[CIZIM PANELI HAZIR] Dokunarak cizim yapabilirsiniz.");
}

// ==========================================
// ARDUINO SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n========================================================");
  Serial.println("  ESP32 CYD HW-458 DOKUNMATIK SISTEM BASLATILIYOR (V4)  ");
  Serial.println("========================================================");

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(BOOT_BTN, INPUT_PULLUP);

  tftSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.begin(touchSPI);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.setRotation(1);

  prefs.begin("cyd_touch_v4", true);
  bool valid = prefs.getBool("valid", false);
  if (valid) {
    cal_h_min   = prefs.getInt("h_min", 1750);
    cal_h_max   = prefs.getInt("h_max", 2380);
    cal_v_min   = prefs.getInt("v_min", 2650);
    cal_v_max   = prefs.getInt("v_max", 3050);
    cal_swap_xy = prefs.getBool("swap_xy", false);
    Serial.println("[NVS] Kayitli kalibrasyon verileri yuklendi.");
  } else {
    Serial.println("[NVS] Varsayilan fabrika donanim katsayilari yuklendi.");
  }
  prefs.end();

  if (digitalRead(BOOT_BTN) == LOW) {
    runCalibration();
  }

  initDrawingScreen();
}

// ==========================================
// ARDUINO ANA DONGU (LOOP)
// ==========================================
void loop() {
  if (digitalRead(BOOT_BTN) == LOW) {
    delay(50);
    if (digitalRead(BOOT_BTN) == LOW) {
      Serial.println("\n[BOOT TUSU] Yeniden kalibrasyon baslatiliyor...");
      runCalibration();
      initDrawingScreen();
      return;
    }
  }

  TS_Point p;
  if (checkTouch(p)) {
    int px, py;
    // Ekran sinirlari disindaysa kesinlikle cizme!
    if (!getPixelCoord(p.x, p.y, px, py)) {
      stableTouchStreak = 0;
      isDrawing = false;
      lastDrawX = -1;
      lastDrawY = -1;
      return;
    }

    stableTouchStreak++;
    // En az 2 ardışık dongu temas onaylanmadan cizime baslama (Anti-Blip)
    if (stableTouchStreak < 2) {
      return;
    }

    // Koordinat Bilgi Cubugunu Guncelle
    tft.fillRect(0, 35, 180, 16, ILI9341_DARKGREY);
    tft.setTextColor(ILI9341_WHITE, ILI9341_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(10, 39);
    tft.printf("X: %-3d  Y: %-3d  Z: %-4d", px, py, p.z);

    if (millis() - lastSerialLog >= 100) {
      lastSerialLog = millis();
      Serial.printf("[DOKUNMA] Piksel(X:%3d, Y:%3d) | Ham(X:%4d, Y:%4d, Z:%4d)\n", px, py, p.x, p.y, p.z);
    }

    // 1. TEMIZLE Butonu (X: 135..220, Y: 0..34)
    if (px >= 135 && px <= 220 && py <= 34) {
      Serial.println("[BUTON] Cizim Ekrani Temizlendi.");
      tft.fillRect(0, 52, 320, 188, ILI9341_BLACK);
      lastDrawX = -1;
      lastDrawY = -1;
      isDrawing = false;
      stableTouchStreak = 0;
      delay(200);
      return;
    }

    // 2. KALIBRE Butonu (X: 225..315, Y: 0..34)
    if (px >= 225 && px <= 315 && py <= 34) {
      Serial.println("[BUTON] Ekrani Kalibre Et secildi.");
      runCalibration();
      initDrawingScreen();
      return;
    }

    // 3. Cizim Alani (Y: 55..239)
    if (py >= 55) {
      // Ziplama Kontrolu (Anti-Jump): Kalem birden 45 pikselden fazla ziplarsa cizgiyi baglama!
      if (isDrawing && lastDrawX != -1 && lastDrawY != -1) {
        int dist = (px - lastDrawX) * (px - lastDrawX) + (py - lastDrawY) * (py - lastDrawY);
        if (dist < (45 * 45)) {
          tft.drawLine(lastDrawX, lastDrawY, px, py, ILI9341_GREEN);
          tft.fillCircle(px, py, 2, ILI9341_GREEN);
        } else {
          // Asiri ziplama -> Hayalet atlama, cizgi cekme!
          tft.fillCircle(px, py, 2, ILI9341_GREEN);
        }
      } else {
        tft.fillCircle(px, py, 2, ILI9341_GREEN);
      }
      lastDrawX = px;
      lastDrawY = py;
      isDrawing = true;
    }
  } else {
    isDrawing = false;
    stableTouchStreak = 0;
    lastDrawX = -1;
    lastDrawY = -1;
  }

  delay(6);
}