// =====================================================
// ENERJI TAKIP - Supabase
// Blynk kodundan donusturuldu - tum mantik korundu
// =====================================================

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

// ===== WIFI AYARLARI - BURAYA KENDİ BİLGİLERİNİ GİR =====
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ===== SUPABASE AYARLARI =====
const char* SUPABASE_URL = "https://YOUR_PROJECT_ID.supabase.co";
const char* SUPABASE_KEY = "YOUR_SUPABASE_ANON_KEY";

// ===== PZEM SoftwareSerial (RX=D5, TX=D6) =====
SoftwareSerial mySerial(D5, D6);
PZEM004Tv30 pzem(mySerial);

// ===== PIN TANIMLARI =====
#define ROLE_PIN   D1
#define BUTON_PIN  D2

// ===== AYARLAR (Supabase settings tablosundan guncellenir) =====
float gucEsigi  = 100.0;
float offsetGuc = 0.40;

// ===== DURUM DEGISKENLERI =====
bool  roleDurumu       = false;
bool  butonOncekiDurum = HIGH;
bool  bootTamamlandi   = false;
bool  asiriUyariGonderildi = false;
unsigned long roleSonDegisim = 0;
#define ROLE_DEBOUNCE 1000

// Hareketli ortalama (Blynk kodundaki gibi: 0.7 eski + 0.3 yeni)
float ortalamaGuc = 0;
float maxGuc      = 0;
long  olcumSayisi = 0;

// ===== ZAMANLAYICILAR =====
unsigned long sonVeriGonderme  = 0;
unsigned long sonKomutKontrol  = 0;
unsigned long sonAyarOkuma     = 0;
unsigned long sonButonKontrol  = 0;
unsigned long sonWifiKontrol   = 0;
unsigned long bootBaslangic    = 0;

#define VERI_ARALIK   2000   // ms
#define KOMUT_ARALIK  2000   // ms
#define AYAR_ARALIK   60000  // ms
#define BUTON_ARALIK  50     // ms
#define WIFI_ARALIK   5000   // ms - wifi kontrol araligi
#define HTTP_TIMEOUT  5000   // ms - http zaman asimi
#define WIFI_RETRY    5      // max wifi deneme sayisi

// WiFi kopuksa kac kez denedik
int wifiHataSayisi = 0;

// =====================================================
// WiFi YENİDEN BAĞLANMA (restart olmadan)
// =====================================================
bool wifiYenidenBaglan() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("WiFi kopuk - yeniden baglaniliyor...");
  WiFi.disconnect();
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi yeniden baglandi! IP: " + WiFi.localIP().toString());
      wifiHataSayisi = 0;
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nYeniden baglanti basarisiz.");
  return false;
}

// =====================================================
// SUPABASE HTTP FONKSIYONLARI (retry destekli)
// =====================================================
int supabasePost(const String& endpoint, const String& body) {
  for (int deneme = 1; deneme <= 3; deneme++) {
    if (WiFi.status() != WL_CONNECTED) break;
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT);
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT);
    http.begin(client, String(SUPABASE_URL) + endpoint);
    http.addHeader("apikey",        SUPABASE_KEY);
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Prefer",        "return=minimal");
    int code = http.POST(body);
    http.end();
    if (code == 201 || code == 200) return code;
    Serial.printf("POST deneme %d basarisiz: %d\n", deneme, code);
    delay(300 * deneme);
  }
  return -1;
}

String supabaseGet(const String& endpoint) {
  for (int deneme = 1; deneme <= 3; deneme++) {
    if (WiFi.status() != WL_CONNECTED) break;
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT);
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT);
    http.begin(client, String(SUPABASE_URL) + endpoint);
    http.addHeader("apikey",        SUPABASE_KEY);
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      http.end();
      return payload;
    }
    http.end();
    Serial.printf("GET deneme %d basarisiz: %d\n", deneme, code);
    delay(300 * deneme);
  }
  return "";
}

int supabasePatch(const String& endpoint, const String& body) {
  for (int deneme = 1; deneme <= 3; deneme++) {
    if (WiFi.status() != WL_CONNECTED) break;
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT);
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT);
    http.begin(client, String(SUPABASE_URL) + endpoint);
    http.addHeader("apikey",        SUPABASE_KEY);
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Prefer",        "return=minimal");
    int code = http.PATCH(body);
    http.end();
    if (code == 200 || code == 204) return code;
    Serial.printf("PATCH deneme %d basarisiz: %d\n", deneme, code);
    delay(300 * deneme);
  }
  return -1;
}

// =====================================================
// VERI GONDERME
// Blynk kodundaki sendData() + sendExtra() birlesimi
// =====================================================
void sendData() {
  if (WiFi.status() != WL_CONNECTED) return;

  float voltage = pzem.voltage();
  float current = pzem.current();
  float power   = pzem.power();
  float energy  = pzem.energy();
  float pf      = pzem.pf();
  float freq    = pzem.frequency();

  // Enerji NaN kontrolu
  float enerji = isnan(energy) ? 0.0f : energy;

  // Diger degerler NaN ise gonderme
  if (isnan(voltage) || isnan(current) || isnan(power)) {
    Serial.println("Hata: PZEM veri okunamadi");
    return;
  }

  // Gecersiz deger korumasi (role gecisinde spike olur)
  if (voltage > 260 || voltage < 150 || current > 100 || power > 4000) {
    Serial.println("Hata: Gecersiz deger atlanıyor");
    return;
  }

  // Ofset uygula (ESP8266 + kablo kayıplari)
  float gosterilenGuc  = max(0.0f, power - offsetGuc);
  float gosterilenAkim = (voltage > 0) ? (gosterilenGuc / voltage) : 0.0f;

  Serial.printf("V:%.1f A:%.3f W:%.1f kWh:%.3f PF:%.2f Hz:%.1f\n",
    voltage, gosterilenAkim, gosterilenGuc, enerji,
    isnan(pf) ? 0 : pf, isnan(freq) ? 0 : freq);

  // Hareketli ortalama (Blynk kodundaki mantik ayni)
  if (gosterilenGuc < 4000) {
    olcumSayisi++;
    if (olcumSayisi == 1) {
      ortalamaGuc = gosterilenGuc;
    } else {
      ortalamaGuc = ortalamaGuc * 0.7f + gosterilenGuc * 0.3f;
    }
    if (gosterilenGuc > maxGuc) maxGuc = gosterilenGuc;
  }

  // Asiri tuketim uyarisi → alerts tablosuna kaydet
  if (gosterilenGuc > gucEsigi && !asiriUyariGonderildi) {
    String alertBody = "{\"message\":\"Asiri Guc! ";
    alertBody += String(gosterilenGuc, 0);
    alertBody += "W\",\"power_value\":";
    alertBody += String(gosterilenGuc, 2);
    alertBody += "}";
    supabasePost("/rest/v1/alerts", alertBody);
    asiriUyariGonderildi = true;
    Serial.println("UYARI: Asiri tuketim kaydedildi!");
  } else if (gosterilenGuc <= gucEsigi) {
    asiriUyariGonderildi = false;
  }

  // Role aciksa degerler sifir (dashboard bunu biliyor ama
  // tutarlilik icin gonderiyoruz)
  float sendVoltage = roleDurumu ? 0.0f : voltage;
  float sendCurrent = roleDurumu ? 0.0f : gosterilenAkim;
  float sendPower   = roleDurumu ? 0.0f : gosterilenGuc;
  float sendPF      = (roleDurumu || isnan(pf))   ? 0.0f : pf;
  float sendFreq    = (roleDurumu || isnan(freq))  ? 0.0f : freq;
  unsigned long uptimeSn = millis() / 1000;

  // Manuel JSON string - ArduinoJson'dan daha az hafiza kullanir
  String body = "{";
  body += "\"voltage\":"       + String(sendVoltage, 2) + ",";
  body += "\"current_a\":"     + String(sendCurrent, 4) + ",";
  body += "\"power\":"         + String(sendPower,   2) + ",";
  body += "\"energy\":"        + String(enerji,       4) + ",";
  body += "\"power_factor\":"  + String(sendPF,       3) + ",";
  body += "\"frequency\":"     + String(sendFreq,     2) + ",";
  body += "\"rssi\":"          + String(WiFi.RSSI())    + ",";
  body += "\"uptime_seconds\":"+ String(uptimeSn)       + ",";
  body += "\"max_power\":"     + String(maxGuc,        2);
  body += "}";

  int code = supabasePost("/rest/v1/readings", body);
  Serial.printf("Veri gonderme: %s\n", code == 201 ? "OK" : ("HATA " + String(code)).c_str());
}

// =====================================================
// KOMUT KONTROLU (Dashboard → ESP)
// Blynk BLYNK_WRITE(V5) ve BLYNK_WRITE(V10) karsiligi
// =====================================================
void pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  String payload = supabaseGet(
    "/rest/v1/commands?executed=eq.false&order=created_at.asc&limit=1"
  );
  if (payload.length() < 5 || payload == "[]") return;

  // ArduinoJson ile parse (sadece burada kullaniyoruz)
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || !doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) return;

  JsonObject cmd = doc[0];
  long   cmdId   = cmd["id"].as<long>();
  String command = cmd["command"].as<String>();

  Serial.println("Komut alindi: " + command + " (id:" + String(cmdId) + ")");

  // Boot tamamlanmadiysa komutu reddet ama executed=true yap
  if (!bootTamamlandi) {
    Serial.println("Boot tamamlanmadi - komut reddedildi");
    supabasePatch("/rest/v1/commands?id=eq." + String(cmdId), "{\"executed\":true}");
    return;
  }

  unsigned long simdi = millis();

  if (command == "relay_on") {
    if (simdi - roleSonDegisim >= ROLE_DEBOUNCE) {
      digitalWrite(ROLE_PIN, LOW);
      roleDurumu    = true;
      roleSonDegisim = simdi;
      Serial.println("ROLE ON - AKIM KESILDI");
    }

  } else if (command == "relay_off") {
    if (simdi - roleSonDegisim >= ROLE_DEBOUNCE) {
      digitalWrite(ROLE_PIN, HIGH);
      roleDurumu    = false;
      roleSonDegisim = simdi;
      Serial.println("ROLE OFF - AKIM GECIYOR");
    }

  } else if (command == "energy_reset") {
    pzem.resetEnergy();
    olcumSayisi = 0;
    ortalamaGuc = 0;
    maxGuc      = 0;
    Serial.println("ENERJI SAYACI SIFIRLANDI");

  } else if (command == "wifi_reset") {
    supabasePatch("/rest/v1/commands?id=eq." + String(cmdId), "{\"executed\":true}");
    Serial.println("WIFI SIFIRLANIYOR...");
    delay(500);
    ESP.restart();
    return;
  }

  // Komutu tamamlandi olarak isaretle
  supabasePatch("/rest/v1/commands?id=eq." + String(cmdId), "{\"executed\":true}");
}

// =====================================================
// AYAR OKUMA (60sn'de bir)
// Supabase settings tablosundan guc_esigi ve offset_guc
// =====================================================
void fetchSettings() {
  if (WiFi.status() != WL_CONNECTED) return;

  String payload = supabaseGet("/rest/v1/settings?select=key,value");
  if (payload.length() < 5) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || !doc.is<JsonArray>()) return;

  for (JsonObject s : doc.as<JsonArray>()) {
    String key   = s["key"].as<String>();
    String value = s["value"].as<String>();

    if (key == "guc_esigi") {
      gucEsigi = value.toFloat();
      Serial.println("Ayar - Guc esigi: " + value + "W");
    } else if (key == "offset_guc") {
      offsetGuc = value.toFloat();
      Serial.println("Ayar - Offset: " + value + "W");
    }
  }
}

// =====================================================
// FIZIKSEL BUTON (D2, INPUT_PULLUP)
// Blynk kodundaki butonKontrol() ayni mantik
// =====================================================
void butonKontrol() {
  bool butonDurum = digitalRead(BUTON_PIN);

  if (butonOncekiDurum == HIGH && butonDurum == LOW) {
    unsigned long simdi = millis();
    if (simdi - roleSonDegisim >= ROLE_DEBOUNCE) {
      roleDurumu = !roleDurumu;
      digitalWrite(ROLE_PIN, roleDurumu ? LOW : HIGH);
      roleSonDegisim = simdi;
      Serial.println(roleDurumu ? "BUTON: ROLE ON - AKIM KESILDI"
                                : "BUTON: ROLE OFF - AKIM GECIYOR");
    }
  }
  butonOncekiDurum = butonDurum;
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  mySerial.begin(9600);

  pinMode(ROLE_PIN,  OUTPUT);
  digitalWrite(ROLE_PIN, HIGH);   // Baslangicta AKIM GECSIN (role kapali)

  pinMode(BUTON_PIN, INPUT_PULLUP);

  Serial.println(F("\n=== ENERJI TAKIP v2.0 - Supabase ==="));
  Serial.printf("Serbest heap: %d bytes\n", ESP.getFreeHeap());

  // WiFi baglantisi
  Serial.println("WiFi baglaniliyor: " + String(WIFI_SSID));
  WiFi.persistent(false);        // Flash yazma - pil omru uzatir
  WiFi.setAutoReconnect(true);   // Kernel seviyesinde otomatik yeniden baglan
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int deneme = 0;
  while (WiFi.status() != WL_CONNECTED && deneme < 40) {
    delay(500);
    Serial.print(".");
    deneme++;
    yield();  // Watchdog besleme
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi baglantisi basarisiz! 10sn sonra yeniden denenecek.");
    delay(10000);
    ESP.restart();
  }

  Serial.println("WiFi baglandi!");
  Serial.println("IP: " + WiFi.localIP().toString());
  Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
  Serial.printf("Serbest heap: %d bytes\n", ESP.getFreeHeap());

  // Boot guvenligi - 3 sn sonra komutlar aktif olacak
  bootBaslangic = millis();

  // Ilk ayar okumasi
  fetchSettings();

  // Tum zamanlayicilari simdi'ye esitle
  unsigned long now = millis();
  sonVeriGonderme = now;
  sonKomutKontrol = now + 500;   // Veri gonderme ile ofset
  sonAyarOkuma    = now;
  sonButonKontrol = now;

  Serial.println(F("=== HAZIR - Veri akisi basliyor ==="));
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  unsigned long simdi = millis();

  // Boot tamamlandi mi? (3sn)
  if (!bootTamamlandi && (simdi - bootBaslangic >= 3000)) {
    bootTamamlandi = true;
    Serial.println("Boot tamamlandi - komutlar aktif");
  }

  // WiFi kontrol - oncce setAutoReconnect dene, 5 basarisizda restart
  if (simdi - sonWifiKontrol >= WIFI_ARALIK) {
    sonWifiKontrol = simdi;
    if (WiFi.status() != WL_CONNECTED) {
      wifiHataSayisi++;
      Serial.printf("WiFi kopuk (hata:%d)\n", wifiHataSayisi);
      if (wifiHataSayisi >= WIFI_RETRY) {
        Serial.println("Max WiFi hatasi - yeniden baslatiliyor");
        ESP.restart();
      }
      wifiYenidenBaglan();
    } else {
      wifiHataSayisi = 0;
    }
  }

  // Heap kontrolu - 12KB altina duserse restart
  if (ESP.getFreeHeap() < 12000) {
    Serial.printf("UYARI: Dusuk heap %d - yeniden baslatiliyor\n", ESP.getFreeHeap());
    delay(500);
    ESP.restart();
  }

  // Buton polling (50ms)
  if (simdi - sonButonKontrol >= BUTON_ARALIK) {
    sonButonKontrol = simdi;
    butonKontrol();
  }

  // Veri gonder (2sn)
  if (simdi - sonVeriGonderme >= VERI_ARALIK) {
    sonVeriGonderme = simdi;
    sendData();
  }

  // Komut kontrol (2sn, veri gonderme ile ofsetli)
  if (simdi - sonKomutKontrol >= KOMUT_ARALIK) {
    sonKomutKontrol = simdi;
    pollCommands();
  }

  // Ayar oku (60sn)
  if (simdi - sonAyarOkuma >= AYAR_ARALIK) {
    sonAyarOkuma = simdi;
    fetchSettings();
    Serial.printf("Serbest heap: %d bytes\n", ESP.getFreeHeap());
  }
}
