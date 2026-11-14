/***************************************************************************************
 * ESP32 CHEAP YELLOW DISPLAY (CYD) HW-458 / CYD3
 * CANLI HAVA DURUMU ISTASYONU & DOKUNMATIK SEHIR SECICI (V1 - OPEN-METEO)
 * 
 * Donanim / Kart: ESP32-2432S028R (Revizyon: HW-458 / TPM408-2.8)
 * Ekran Surucusu: ILI9341 (Donanimsal HSPI: SCLK=14, MISO=12, MOSI=13, CS=15, DC=2, BL=21)
 * Dokunmatik    : XPT2046 (Donanimsal VSPI: CLK=25, MISO=39, MOSI=32, CS=33)
 * SD Kart CS    : GPIO 5 (Pasif = HIGH)
 * 
 * OZELLIKLER:
 * 1. Open-Meteo Servisi: 100% Ucretsiz, API Key istemez, aninda baglanir!
 * 2. Canli Saat & Tarih: Internet uzerinden (NTP - Turkiye GMT+3) saniye saniye guncel saat.
 * 3. Anlik Hava Durumu: Sicaklik, Hissedilen, Nem, Ruzgar Hizi, Atmosferik Basinc.
 * 4. 3 Gunluk Tahmin Kartlari: Bugun, Yarin ve Sonraki Gun icin Min/Max sicaklik ve hava simgesi.
 * 5. Dokunmatik Kontrol Paneli:
 *    - [ IZMIR ]   [ ISTANBUL ]   [ ANKARA ]   [ ↻ YENILE ]
 * 6. V4 Anti-Ghosting Filtresi: Temassizken sifir parazit, kusursuz buton algilama.
 ***************************************************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <ArduinoJson.h>
#include <time.h>

// ==========================================
// WIFI AYARLARI (Kendi Ag Bilgilerinizi Girin)
// ==========================================
const char* WIFI_SSID     = "WIFI_ADINIZ";      // <<-- WiFi Ag Adiniz
const char* WIFI_PASSWORD = "WIFI_SIFRENIZ";   // <<-- WiFi Sifreniz

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
// RENK PALETI (Modern Koyu Tema)
// ==========================================
#define COLOR_BG          0x0842   // Cok Koyu Lacivert/Siyah Arka Plan
#define COLOR_HEADER      0x0110   // Baslik Cubugu
#define COLOR_CARD        0x10A4   // Kart Arka Plani
#define COLOR_CARD_BORDER 0x2967   // Kart Kenarligi
#define COLOR_ACCENT      0x05BF   // Turkuaz Vurgu
#define COLOR_SUN         0xFD20   // Gunes Sarisi / Turuncu
#define COLOR_RAIN        0x3D7F   // Yagmur Mavisi
#define COLOR_SNOW        0xC67A   // Kar Beyazi
#define COLOR_TEMP_HOT    0xFA20   // Sicak (Turuncu/Kirmizi)
#define COLOR_TEMP_COLD   0x3D7F   // Soguk (Mavi)

// ==========================================
// SEHIR VE KOORDINAT BILGILERI
// ==========================================
struct CityInfo {
  const char* name;
  float lat;
  float lon;
};

const CityInfo CITIES[] = {
  { "IZMIR",    38.4192, 27.1287 },
  { "ISTANBUL", 41.0082, 28.9784 },
  { "ANKARA",   39.9334, 32.8597 }
};
const int TOTAL_CITIES = 3;
int currentCity = 0; // Varsayilan: IZMIR (0)

// Hava Durumu Veri Yapisi
struct CurrentWeather {
  float temp;
  float feelsLike;
  int humidity;
  float windSpeed;
  float pressure;
  int weatherCode;
  String timeStr;
};

struct DailyForecast {
  float maxTemp;
  float minTemp;
  int weatherCode;
  String dateStr;
};

CurrentWeather currentW;
DailyForecast dailyW[3];

// Dokunmatik Kalibrasyon Katsayilari (HW-458)
const int cal_h_min = 1750;
const int cal_h_max = 2380;
const int cal_v_min = 2650;
const int cal_v_max = 3050;

// Zaman ve Guncelleme Degiskenleri
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 15 * 60 * 1000; // 15 Dakika
unsigned long lastClockTick = 0;
String lastTimeStr = "";
String lastDateStr = "";
bool isWeatherLoaded = false;

// SPI ve Nesneler
SPIClass tftSPI(HSPI);
SPIClass touchSPI(VSPI);

Adafruit_ILI9341 tft = Adafruit_ILI9341(&tftSPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen ts(XPT2046_CS);

// ==========================================
// ANTI-GHOST DOKUNMA FILTRESI (V4)
// ==========================================
bool checkTouch(TS_Point &p) {
  TS_Point p1 = ts.getPoint();
  if (p1.z < 200 || p1.z > 3750) return false;
  if (p1.x < 1680 || p1.x > 2450 || p1.y < 2550 || p1.y > 3150) return false;

  delay(4);
  TS_Point p2 = ts.getPoint();
  if (p2.z < 200 || p2.z > 3750) return false;
  if (p2.x < 1680 || p2.x > 2450 || p2.y < 2550 || p2.y > 3150) return false;

  if (abs(p1.x - p2.x) > 35 || abs(p1.y - p2.y) > 35) return false;

  p.x = (p1.x + p2.x) / 2;
  p.y = (p1.y + p2.y) / 2;
  p.z = (p1.z + p2.z) / 2;
  return true;
}

bool getPixelCoord(int rawX, int rawY, int &px, int &py) {
  if (rawX < 1650 || rawX > 2480 || rawY < 2550 || rawY > 3150) return false;

  px = map(rawX, cal_h_min, cal_h_max, 30, 290);
  py = map(rawY, cal_v_min, cal_v_max, 30, 210);

  px = constrain(px, 0, 319);
  py = constrain(py, 0, 239);
  return true;
}

// ==========================================
// HAVA DURUMU KOD METNI (WMO Standarti)
// ==========================================
const char* getWeatherDesc(int code) {
  switch (code) {
    case 0:  return "Gunesli / Acik";
    case 1:  return "Az Bulutlu";
    case 2:  return "Parcali Bulutlu";
    case 3:  return "Cok Bulutlu";
    case 45: return "Sisli";
    case 48: return "Kiragili Sis";
    case 51:
    case 53:
    case 55: return "Cisenti Yagisli";
    case 61: return "Hafif Yagmurlu";
    case 63: return "Yagmurlu";
    case 65: return "Siddetli Yagmur";
    case 71:
    case 73:
    case 75: return "Kar Yagisli";
    case 80:
    case 81: return "Saganak Yagisli";
    case 82: return "Kuvvetli Saganak";
    case 85:
    case 86: return "Kar Saganagi";
    case 95: return "Gok Gurultulu Firtina";
    case 96:
    case 99: return "Dolu ve Firtina";
    default: return "Bulutlu";
  }
}

// ==========================================
// HAVA DURUMU VEKTOR SIMGELERI (Adafruit GFX)
// ==========================================
void drawSunIcon(int x, int y, int r) {
  tft.fillCircle(x, y, r, COLOR_SUN);
  tft.drawCircle(x, y, r, ILI9341_ORANGE);
  // Isinlar
  for (int a = 0; a < 360; a += 45) {
    float rad = a * 0.0174533;
    int x1 = x + cos(rad) * (r + 3);
    int y1 = y + sin(rad) * (r + 3);
    int x2 = x + cos(rad) * (r + 7);
    int y2 = y + sin(rad) * (r + 7);
    tft.drawLine(x1, y1, x2, y2, COLOR_SUN);
  }
}

void drawCloudShape(int x, int y, int scale, uint16_t color) {
  tft.fillRoundRect(x - 12 * scale, y + 2 * scale, 24 * scale, 10 * scale, 4 * scale, color);
  tft.fillCircle(x - 6 * scale, y + 2 * scale, 7 * scale, color);
  tft.fillCircle(x + 2 * scale, y - 2 * scale, 9 * scale, color);
  tft.fillCircle(x + 9 * scale, y + 4 * scale, 5 * scale, color);
}

void drawRainDrops(int x, int y, int count) {
  for (int i = 0; i < count; i++) {
    int dx = (i - 1) * 7;
    tft.drawLine(x + dx, y + 14, x + dx - 2, y + 21, COLOR_RAIN);
    tft.drawLine(x + dx + 1, y + 14, x + dx - 1, y + 21, COLOR_RAIN);
  }
}

void drawSnowFlakes(int x, int y) {
  tft.fillCircle(x - 7, y + 16, 2, COLOR_SNOW);
  tft.fillCircle(x,     y + 19, 2, COLOR_SNOW);
  tft.fillCircle(x + 7, y + 16, 2, COLOR_SNOW);
}

void drawLightning(int x, int y) {
  tft.fillTriangle(x + 1, y + 10, x - 5, y + 19, x + 2, y + 19, ILI9341_YELLOW);
  tft.fillTriangle(x - 2, y + 19, x + 4, y + 19, x - 1, y + 29, ILI9341_YELLOW);
}

// Genel Simge Cizici (WMO koduna gore)
void drawWeatherIcon(int x, int y, int code, bool isLarge) {
  int scale = isLarge ? 2 : 1;
  int r = isLarge ? 14 : 7;

  if (code == 0) {
    // Tam Gunesli
    drawSunIcon(x, y, r);
  } else if (code == 1 || code == 2) {
    // Parcali Bulutlu (Gunes + Bulut)
    drawSunIcon(x - (isLarge ? 8 : 4), y - (isLarge ? 8 : 4), r - (isLarge ? 3 : 2));
    drawCloudShape(x + (isLarge ? 4 : 2), y + (isLarge ? 4 : 2), scale, ILI9341_WHITE);
  } else if (code == 3) {
    // Cok Bulutlu / Kapali
    drawCloudShape(x, y, scale, ILI9341_LIGHTGREY);
  } else if (code >= 45 && code <= 48) {
    // Sisli
    drawCloudShape(x, y - (isLarge ? 4 : 2), scale, ILI9341_LIGHTGREY);
    tft.drawFastHLine(x - 14 * scale, y + 14 * scale, 28 * scale, ILI9341_CYAN);
    tft.drawFastHLine(x - 10 * scale, y + 17 * scale, 20 * scale, ILI9341_LIGHTGREY);
  } else if ((code >= 51 && code <= 65) || (code >= 80 && code <= 82)) {
    // Yagmurlu
    drawCloudShape(x, y, scale, 0x7BEF);
    drawRainDrops(x, y, isLarge ? 3 : 2);
  } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    // Karli
    drawCloudShape(x, y, scale, ILI9341_WHITE);
    drawSnowFlakes(x, y);
  } else if (code >= 95) {
    // Gok Gurultulu Firtina
    drawCloudShape(x, y, scale, 0x4208);
    drawLightning(x, y);
  } else {
    // Varsayilan Bulut
    drawCloudShape(x, y, scale, ILI9341_WHITE);
  }
}

// ==========================================
// ARAYUZ: SABIT PANELI VE BUTONLARI CIZ
// ==========================================
void drawInterfaceBase() {
  tft.fillScreen(COLOR_BG);

  // 1. Ust Baslik Cubugu
  tft.fillRect(0, 0, 320, 32, COLOR_HEADER);
  tft.drawFastHLine(0, 32, 320, COLOR_CARD_BORDER);

  // 2. Anlik Hava Durumu Paneli (X: 6, Y: 36, W: 184, H: 98)
  tft.fillRoundRect(6, 36, 184, 98, 6, COLOR_CARD);
  tft.drawRoundRect(6, 36, 184, 98, 6, COLOR_CARD_BORDER);

  // 3. Detay Bilgi Kutucuklari (X: 194 .. 314, Y: 36 .. 134)
  const char* labels[4] = { "HISSEDILEN", "NEM", "RUZGAR", "BASINC" };
  for (int i = 0; i < 4; i++) {
    int by = 36 + i * 25;
    tft.fillRoundRect(194, by, 120, 22, 4, COLOR_CARD);
    tft.drawRoundRect(194, by, 120, 22, 4, COLOR_CARD_BORDER);

    tft.setTextColor(ILI9341_LIGHTGREY, COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(199, by + 7);
    tft.print(labels[i]);
  }

  // 4. 3 Gunluk Tahmin Karti Alanlari (X: 6, Y: 138, W: 100, H: 64)
  for (int i = 0; i < 3; i++) {
    int cx = 6 + i * 105;
    tft.fillRoundRect(cx, 138, 98, 64, 6, COLOR_CARD);
    tft.drawRoundRect(cx, 138, 98, 64, 6, COLOR_CARD_BORDER);
  }

  // 5. Alt Dokunmatik Kontrol Butonlari (Y: 206..236)
  drawControlButtons();
}

// Butonlari Ciz / Guncelle
void drawControlButtons() {
  tft.fillRect(0, 204, 320, 36, COLOR_BG);

  // Sehir Butonlari
  for (int i = 0; i < TOTAL_CITIES; i++) {
    int bx = 6 + i * 86;
    bool isActive = (i == currentCity);
    uint16_t bColor = isActive ? ILI9341_DARKGREEN : COLOR_CARD;
    uint16_t borderColor = isActive ? ILI9341_GREEN : COLOR_CARD_BORDER;

    tft.fillRoundRect(bx, 206, 80, 30, 4, bColor);
    tft.drawRoundRect(bx, 206, 80, 30, 4, borderColor);

    tft.setTextColor(isActive ? ILI9341_WHITE : ILI9341_LIGHTGREY, bColor);
    tft.setTextSize(1);
    int textOffset = (80 - (strlen(CITIES[i].name) * 6)) / 2;
    tft.setCursor(bx + textOffset, 217);
    tft.print(CITIES[i].name);
  }

  // [ ↻ ] Yenile Butonu (X: 268, Y: 206, W: 46, H: 30)
  tft.fillRoundRect(268, 206, 46, 30, 4, COLOR_CARD);
  tft.drawRoundRect(268, 206, 46, 30, 4, COLOR_ACCENT);
  tft.setTextColor(COLOR_ACCENT, COLOR_CARD);
  tft.setTextSize(2);
  tft.setCursor(283, 213);
  tft.print("R"); // Refresh simgesi
}

// ==========================================
// CANLI SAAT VE TARIHI GUNCELLE (Header)
// ==========================================
void updateClockDisplay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }

  char timeBuffer[10];
  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);

  char dateBuffer[20];
  strftime(dateBuffer, sizeof(dateBuffer), "%d.%m.%Y", &timeinfo);

  // Sadece degisim varsa ekrani guncelle (titremeyi onler)
  if (String(timeBuffer) != lastTimeStr) {
    lastTimeStr = String(timeBuffer);

    // Sehir Ismi (Sol)
    tft.setTextColor(ILI9341_YELLOW, COLOR_HEADER);
    tft.setTextSize(2);
    tft.fillRect(10, 8, 90, 18, COLOR_HEADER);
    tft.setCursor(10, 8);
    tft.print(CITIES[currentCity].name);

    // Saat (Orta)
    tft.setTextColor(ILI9341_WHITE, COLOR_HEADER);
    tft.setTextSize(2);
    tft.fillRect(110, 8, 105, 18, COLOR_HEADER);
    tft.setCursor(110, 8);
    tft.print(timeBuffer);

    // Tarih (Sag)
    tft.setTextColor(COLOR_ACCENT, COLOR_HEADER);
    tft.setTextSize(1);
    tft.fillRect(230, 12, 85, 12, COLOR_HEADER);
    tft.setCursor(235, 12);
    tft.print(dateBuffer);
  }
}

// ==========================================
// HAVA DURUMU VERILERINI EKRANA YANSIT
// ==========================================
void renderWeatherData() {
  // 1. Ana Panel (Sicaklik, Simge, Durum Metni)
  tft.fillRoundRect(7, 37, 182, 96, 5, COLOR_CARD);

  // Buyuk Simge
  drawWeatherIcon(45, 78, currentW.weatherCode, true);

  // Buyuk Sicaklik Derecesi
  tft.setTextColor(ILI9341_WHITE, COLOR_CARD);
  tft.setTextSize(3);
  tft.setCursor(95, 50);
  tft.printf("%.0f", currentW.temp);
  tft.setTextSize(2);
  tft.print("C");
  tft.drawCircle(tft.getCursorX() - 17, 52, 2, ILI9341_WHITE); // Derece simgesi

  // Durum Metni (Orn: Gunesli, Parcali Bulutlu)
  tft.setTextColor(COLOR_ACCENT, COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(15, 115);
  tft.print(getWeatherDesc(currentW.weatherCode));

  // 2. Sag Detay Kutucuklari
  // Hissedilen
  tft.fillRect(265, 42, 45, 12, COLOR_CARD);
  tft.setTextColor(ILI9341_YELLOW, COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(270, 43);
  tft.printf("%.0f C", currentW.feelsLike);

  // Nem
  tft.fillRect(265, 67, 45, 12, COLOR_CARD);
  tft.setTextColor(COLOR_ACCENT, COLOR_CARD);
  tft.setCursor(270, 68);
  tft.printf("%%%d", currentW.humidity);

  // Ruzgar
  tft.fillRect(265, 92, 45, 12, COLOR_CARD);
  tft.setTextColor(ILI9341_GREEN, COLOR_CARD);
  tft.setCursor(260, 93);
  tft.printf("%.0f km/h", currentW.windSpeed);

  // Basinc
  tft.fillRect(260, 117, 50, 12, COLOR_CARD);
  tft.setTextColor(ILI9341_ORANGE, COLOR_CARD);
  tft.setCursor(252, 118);
  tft.printf("%.0f hPa", currentW.pressure);

  // 3. 3 Gunluk Tahmin Kartlari
  const char* cardTitles[3] = { "BUGUN", "YARIN", "SONRAKI" };
  for (int i = 0; i < 3; i++) {
    int cx = 6 + i * 105;
    tft.fillRoundRect(cx + 1, 139, 96, 62, 5, COLOR_CARD);

    // Kart Basligi
    tft.setTextColor(ILI9341_LIGHTGREY, COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(cx + 8, 144);
    tft.print(cardTitles[i]);

    // Kucuk Hava Simgesi
    drawWeatherIcon(cx + 74, 155, dailyW[i].weatherCode, false);

    // Max / Min Sicaklik
    tft.setTextSize(1);
    tft.setCursor(cx + 8, 168);
    tft.setTextColor(COLOR_TEMP_HOT, COLOR_CARD);
    tft.printf("Max: %.0fC", dailyW[i].maxTemp);

    tft.setCursor(cx + 8, 184);
    tft.setTextColor(COLOR_TEMP_COLD, COLOR_CARD);
    tft.printf("Min: %.0fC", dailyW[i].minTemp);
  }

  isWeatherLoaded = true;
}

// ==========================================
// OPEN-METEO API ISTEGI (JSON Parsing)
// ==========================================
bool fetchWeatherData(int cityIdx) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HATA] WiFi baglantisi yok!");
    return false;
  }

  // Ekranda kucuk bilgi goster
  tft.fillRect(95, 85, 90, 16, COLOR_CARD);
  tft.setTextColor(ILI9341_YELLOW, COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(95, 89);
  tft.print("Veri aliniyor...");

  WiFiClientSecure client;
  client.setInsecure(); // Sertifika dogrulama gerekmez

  HTTPClient https;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(CITIES[cityIdx].lat, 4) +
               "&longitude=" + String(CITIES[cityIdx].lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,surface_pressure,wind_speed_10m" +
               "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=3";

  Serial.println("\n[HTTP] Open-Meteo Istegi Baslatiliyor:");
  Serial.println(url);

  if (!https.begin(client, url)) {
    Serial.println("[HATA] HTTPS Baglantisi baslatilamadi!");
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HATA] HTTP Hata Kodu: %d\n", httpCode);
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  Serial.println("[HTTP] Veri basariyla alindi, JSON ayristiriliyor...");

  // ArduinoJson 6 & 7 Destegi
  #if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
  #else
    StaticJsonDocument<2500> doc;
  #endif

  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("[HATA] JSON Deserialization: %s\n", error.c_str());
    return false;
  }

  // 1. Guncel Veriler
  currentW.temp        = doc["current"]["temperature_2m"];
  currentW.feelsLike   = doc["current"]["apparent_temperature"];
  currentW.humidity    = doc["current"]["relative_humidity_2m"];
  currentW.windSpeed   = doc["current"]["wind_speed_10m"];
  currentW.pressure    = doc["current"]["surface_pressure"];
  currentW.weatherCode = doc["current"]["weather_code"];

  // 2. 3 Gunluk Tahminler
  for (int i = 0; i < 3; i++) {
    dailyW[i].maxTemp     = doc["daily"]["temperature_2m_max"][i];
    dailyW[i].minTemp     = doc["daily"]["temperature_2m_min"][i];
    dailyW[i].weatherCode = doc["daily"]["weather_code"][i];
    dailyW[i].dateStr     = doc["daily"]["time"][i].as<String>();
  }

  Serial.printf("[BASARILI] %s Sicaklik: %.1f C, Durum: %s\n",
                CITIES[cityIdx].name, currentW.temp, getWeatherDesc(currentW.weatherCode));

  lastWeatherUpdate = millis();
  renderWeatherData();
  return true;
}

// ==========================================
// WIFI VE AG BAGLANTISI
// ==========================================
void connectWiFi() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 32, COLOR_HEADER);
  tft.setTextColor(ILI9341_WHITE, COLOR_HEADER);
  tft.setTextSize(2);
  tft.setCursor(20, 8);
  tft.print("HAVA DURUMU ISTASYONU");

  tft.setTextColor(ILI9341_YELLOW, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(25, 75);
  tft.print("WiFi Baglaniliyor...");

  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(25, 110);
  tft.printf("Ag: %s", WIFI_SSID);

  Serial.printf("\nWiFi Agina Baglaniliyor: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  int barWidth = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 35) {
    delay(500);
    Serial.print(".");
    attempts++;

    // Ilerleme cubugu
    barWidth = (attempts * 270) / 35;
    tft.drawRoundRect(25, 140, 270, 16, 4, COLOR_CARD_BORDER);
    tft.fillRoundRect(27, 142, barWidth, 12, 3, COLOR_ACCENT);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Baglandi! IP Adresi: " + WiFi.localIP().toString());
    tft.setTextColor(ILI9341_GREEN, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(25, 175);
    tft.print("Baglanti Basarili!");
    delay(1000);

    // NTP Saat Eslestirme (Turkiye UTC+3)
    configTime(3 * 3600, 0, "pool.ntp.org", "time.google.com");
    Serial.println("NTP Saat Sunucusu Yapilandirildi (UTC+3).");
    delay(1200);
  } else {
    Serial.println("\n[HATA] WiFi Baglantisi Kurulamadi!");
    tft.setTextColor(ILI9341_RED, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(25, 175);
    tft.print("Baglanti Basarisiz!");

    tft.setTextColor(ILI9341_WHITE, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(25, 205);
    tft.print("Lutfen SSID ve Sifrenizi kod icinde kontrol edin.");
    while (1) { delay(1000); }
  }
}

// ==========================================
// ARDUINO SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n========================================================");
  Serial.println("  ESP32 CYD HW-458 CANLI HAVA DURUMU BASLATILIYOR       ");
  Serial.println("========================================================");

  // 1. SD Karti Pasif Yap
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // 2. Ekran Arka Isigini Ac
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // 3. BOOT Butonu
  pinMode(BOOT_BTN, INPUT_PULLUP);

  // 4. Ekran SPI ve Surucusu (HSPI)
  tftSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1); // Yatay 320x240

  // 5. Dokunmatik SPI (VSPI)
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.begin(touchSPI);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
  ts.setRotation(1);

  // 6. WiFi ve Saat Baglantisi
  connectWiFi();

  // 7. Arayuzu Ciz ve Ilk Verileri Al (Varsayilan: IZMIR)
  drawInterfaceBase();
  fetchWeatherData(currentCity);
}

// ==========================================
// ARDUINO ANA DONGU (LOOP)
// ==========================================
void loop() {
  // 1. Canli Saati Saniyede Bir Guncelle (Titremesiz)
  if (millis() - lastClockTick >= 1000) {
    lastClockTick = millis();
    updateClockDisplay();
  }

  // 2. Belirli Araliklarla Otomatik Guncelle (15 Dakika)
  if (millis() - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
    Serial.println("\n[ZAMANLAYICI] Otomatik hava durumu guncellemesi yapiliyor...");
    fetchWeatherData(currentCity);
  }

  // 3. Dokunmatik Buton Kontrolleri
  TS_Point p;
  if (checkTouch(p)) {
    int px, py;
    if (getPixelCoord(p.x, p.y, px, py)) {
      // Alt Buton Alaninda mi? (Y: 206..236)
      if (py >= 200) {
        // Buton 0: IZMIR (X: 6..86)
        if (px >= 6 && px <= 86) {
          if (currentCity != 0) {
            currentCity = 0;
            drawControlButtons();
            fetchWeatherData(currentCity);
            delay(250);
          }
        }
        // Buton 1: ISTANBUL (X: 92..172)
        else if (px >= 92 && px <= 172) {
          if (currentCity != 1) {
            currentCity = 1;
            drawControlButtons();
            fetchWeatherData(currentCity);
            delay(250);
          }
        }
        // Buton 2: ANKARA (X: 178..258)
        else if (px >= 178 && px <= 258) {
          if (currentCity != 2) {
            currentCity = 2;
            drawControlButtons();
            fetchWeatherData(currentCity);
            delay(250);
          }
        }
        // Buton 3: [ ↻ ] YENILE (X: 268..314)
        else if (px >= 268 && px <= 314) {
          Serial.println("[DOKUNMATIK] Manuel Yenile butonuna basildi.");
          tft.fillRoundRect(268, 206, 46, 30, 4, ILI9341_ORANGE);
          tft.setTextColor(ILI9341_WHITE, ILI9341_ORANGE);
          tft.setTextSize(2);
          tft.setCursor(283, 213);
          tft.print("R");

          fetchWeatherData(currentCity);
          drawControlButtons();
          delay(250);
        }
      }
    }
  }

  // 4. Kart uzerindeki BOOT Tusuna basilirsa hemen yenile
  if (digitalRead(BOOT_BTN) == LOW) {
    delay(50);
    if (digitalRead(BOOT_BTN) == LOW) {
      Serial.println("\n[BOOT TUSU] Manuel Yenileme Tetiklendi.");
      fetchWeatherData(currentCity);
      while (digitalRead(BOOT_BTN) == LOW) { delay(10); }
    }
  }

  delay(15);
}