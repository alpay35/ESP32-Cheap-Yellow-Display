/***************************************************************************************
 * ESP32 CHEAP YELLOW DISPLAY (CYD) HW-458
 * DOKUNMATIK VE DONANIM DETAYLI CANLI SERI PORT TESHIS KODU (DIAGNOSTIC)
 * 
 * Bu kod dokunmatik ekranin tum ham donanim sinyallerini (Z1, Z2, X, Y, IRQ Pini, P.Z)
 * hem Seri Porttan (115200 baud) saniyede 10 kez basar hem de LCD ekranda gosterir.
 ***************************************************************************************/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// Pinler (HW-458 / CYD3)
#define TFT_BL     21
#define TFT_CS     15
#define TFT_DC      2
#define TFT_MOSI   13
#define TFT_SCLK   14
#define TFT_MISO   12
#define TFT_RST    -1
#define SD_CS       5

#define XPT2046_CLK   25
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CS    33
#define XPT2046_IRQ   36

SPIClass tftSPI(HSPI);
SPIClass touchSPI(VSPI);

Adafruit_ILI9341 tft = Adafruit_ILI9341(&tftSPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen ts(XPT2046_CS);

// Ham SPI Okuma Fonksiyonu
uint16_t readRawRegister(uint8_t cmd) {
  touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(XPT2046_CS, LOW);
  touchSPI.transfer(cmd);
  uint8_t b1 = touchSPI.transfer(0x00);
  uint8_t b2 = touchSPI.transfer(0x00);
  digitalWrite(XPT2046_CS, HIGH);
  touchSPI.endTransaction();
  return ((b1 << 8) | b2) >> 3;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================================");
  Serial.println("  CYD HW-458 CANLI DOKUNMATIK TESHIS BASLATILDI (115200)");
  Serial.println("========================================================");

  // SD Kart Pasif
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Arka Isik Ac
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // IRQ pini (Giris)
  pinMode(XPT2046_IRQ, INPUT);

  // Ekran Baslat
  tftSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  // Baslik
  tft.fillRect(0, 0, 320, 32, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE, ILI9341_NAVY);
  tft.setTextSize(2);
  tft.setCursor(15, 8);
  tft.print("HW-458 SERI TESHIS");

  // Touch SPI
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.begin(touchSPI);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.setRotation(1);

  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 42);
  tft.print("Ekrana kalemin ucuyla dokunun / birakin:");
}

unsigned long lastPrint = 0;

void loop() {
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();

    // 1. Donanim IRQ Pini (Pin 36)
    int irqVal = digitalRead(XPT2046_IRQ);

    // 2. Kutuphane Okumalari
    bool touched = ts.touched();
    TS_Point p = ts.getPoint();

    // 3. Ham Register Okumalari
    uint16_t z1 = readRawRegister(0xB1);
    uint16_t z2 = readRawRegister(0xC1);
    uint16_t rx = readRawRegister(0x91);
    uint16_t ry = readRawRegister(0xD1);

    // SERI PORT CIKTISI (Bunu izliyoruz!)
    Serial.printf("IRQ36:%d | TOUCHED:%d | P_Z:%-4d P_X:%-4d P_Y:%-4d | Z1:%-4d Z2:%-4d RX:%-4d RY:%-4d\n",
                  irqVal, touched ? 1 : 0, p.z, p.x, p.y, z1, z2, rx, ry);

    // LCD EKRAN GOSTERGESI
    tft.fillRect(10, 60, 300, 170, ILI9341_BLACK);
    tft.setTextSize(2);

    // IRQ Durumu
    tft.setCursor(10, 65);
    tft.setTextColor(irqVal == LOW ? ILI9341_GREEN : ILI9341_RED);
    tft.printf("IRQ (Pin 36): %s", irqVal == LOW ? "LOW (DOKUNULDU)" : "HIGH (BOS)");

    // Kutuphane P.Z
    tft.setCursor(10, 95);
    tft.setTextColor(ILI9341_CYAN);
    tft.printf("P.Z Basinc : %-4d", p.z);

    // Koordinatlar
    tft.setCursor(10, 125);
    tft.setTextColor(ILI9341_WHITE);
    tft.printf("P.X: %-4d  P.Y: %-4d", p.x, p.y);

    // Ham Z1 ve Z2
    tft.setCursor(10, 155);
    tft.setTextColor(ILI9341_YELLOW);
    tft.printf("Ham Z1:%-4d Z2:%-4d", z1, z2);

    // Ham X ve Y
    tft.setCursor(10, 185);
    tft.setTextColor(ILI9341_GREEN);
    tft.printf("Ham RX:%-4d RY:%-4d", rx, ry);

    // Dokunma Tespiti
    tft.setCursor(10, 215);
    if (irqVal == LOW || (p.z > 200 && p.z != 4095)) {
      tft.setTextColor(ILI9341_GREEN);
      tft.print("DURUM: >> DOKUNULUYOR <<");
    } else {
      tft.setTextColor(ILI9341_DARKGREY);
      tft.print("DURUM: [ TEMAS YOK ]   ");
    }
  }
}
