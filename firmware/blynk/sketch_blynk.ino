#define BLYNK_PRINT Serial

#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Enerji Takip"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>

// WiFi bilgileri
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// PZEM için SoftwareSerial (RX, TX)
SoftwareSerial mySerial(D5, D6);
PZEM004Tv30 pzem(mySerial);

// Röle pin tanımı
#define ROLE_PIN D1

// Fiziksel buton pin tanımı
#define BUTON_PIN D2

// Aşırı güç tüketimi eşiği (Watt) - Blynk V16'dan ayarlanır
float gucEsigi = 100.0f;

// Elektrik birim fiyatı (TL/kWh)
#define BIRIM_FIYAT 3.20

// ESP8266 adaptörü + kablo kayıpları ofseti (Watt)
// Cihaz bağlı değilken okunan boş güç değerini buraya yaz
#define OFFSET_GUC 0.40f

bool asiriUyariGonderildi = false;
bool kullaniciMuhalefet = false;  // Kullanıcı manuel kapattığında tekrar atmasın
bool roleDurumu = false;
bool butonOncekiDurum = HIGH;
bool bootTamamlandi = false;
unsigned long roleSonDegisim = 0;
#define ROLE_DEBOUNCE 1000

// Ortalama güç hesaplama (hareketli ortalama)
long olcumSayisi = 0;
float ortalamaGuc = 0;
float maxGuc = 0;
String sonV6Renk = "";
String sonV8Renk = "";

// Blynk bileşenleri
BlynkTimer timer;
bool ledDurumu = false;

// LED yak-sön fonksiyonu (bağımsız timer)
void ledBlink() {
  ledDurumu = !ledDurumu;
  Blynk.virtualWrite(V1, ledDurumu ? 255 : 0);
}

// Boot tamamlandı - röle kontrol aktif
void bootTamamla() {
  bootTamamlandi = true;
  Serial.println("Boot tamamlandi - role kontrol aktif");
}

// Blynk bağlandığında V5 senkronunu engelle (restart döngüsünü kırar)
BLYNK_CONNECTED() {
  Serial.println("Blynk baglandi");
  Blynk.syncVirtual(V16);  // Dashboard'daki eşik değerini al
}

// Dashboard'dan güç eşiği ayarla (V16 Slider)
BLYNK_WRITE(V16) {
  gucEsigi = param.asFloat();
  Serial.print("Yeni guc esigi: ");
  Serial.print(gucEsigi);
  Serial.println(" W");
}

// Fiziksel buton okuma fonksiyonu
void butonKontrol() {
  bool butonDurum = digitalRead(BUTON_PIN);
  if (butonOncekiDurum == HIGH && butonDurum == LOW) {
    roleDurumu = !roleDurumu;
    if (roleDurumu) {
      digitalWrite(ROLE_PIN, LOW);
      Blynk.virtualWrite(V5, 1);
      Serial.println("BUTON: ROLE ON - AKIM KESILDI");
    } else {
      digitalWrite(ROLE_PIN, HIGH);  // Akım geçer
      Blynk.virtualWrite(V5, 0);
      kullaniciMuhalefet = true;  // Fiziksel butonla kapattı - tekrar atma
      Serial.println("BUTON: ROLE OFF - AKIM GECIYOR (kullanici)");
    }
  }
  butonOncekiDurum = butonDurum;
}

// Blynk'ten röle komutu geldiğinde çalışır (V5 butonu)
BLYNK_WRITE(V5) {
  if (!bootTamamlandi) {
    Serial.println("Boot tamamlanmadi - role komutu reddedildi");
    return;
  }

  unsigned long simdi = millis();
  if (simdi - roleSonDegisim < ROLE_DEBOUNCE) {
    Serial.println("Role debounce - komut reddedildi");
    return;
  }

  int deger = param.asInt();
  if (deger == 1) {
    digitalWrite(ROLE_PIN, LOW);
    roleDurumu = true;
    roleSonDegisim = simdi;
    Serial.println("ROLE ON - AKIM KESILDI");
  } else {
    digitalWrite(ROLE_PIN, HIGH);
    roleDurumu = false;
    roleSonDegisim = simdi;
    kullaniciMuhalefet = true;  // Kullanıcı manuel kapattı - tekrar atma
    Serial.println("ROLE OFF - AKIM GECIYOR (kullanici)");
  }
}

// Blynk'ten enerji sıfırlama komutu (V10 butonu)
BLYNK_WRITE(V10) {
  if (param.asInt() == 1) {
    pzem.resetEnergy();
    olcumSayisi = 0;
    ortalamaGuc = 0;
    Blynk.virtualWrite(V9, 0);
    Blynk.virtualWrite(V7, 0);
    Blynk.virtualWrite(V8, "Sayac sifirlandi!");
    Serial.println("ENERJI SAYACI SIFIRLANDI");
  }
}

// Veri gönderme fonksiyonu
void sendData() {
  float voltage = pzem.voltage();
  float current = pzem.current();
  float power  = pzem.power();
  float energy  = pzem.energy();

  // Enerji her zaman gönder (chart için)
  Blynk.virtualWrite(V7, isnan(energy) ? 0.0f : energy);

  // Röle açıksa (akım kesilmiş) sıfır değer göster
  if (roleDurumu) {
    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
    Blynk.virtualWrite(V4, 0);
    Blynk.virtualWrite(V6, "ROLE ACIK - Akim kesik");
    if (sonV6Renk != "#FF6600") { Blynk.setProperty(V6, "color", "#FF6600"); sonV6Renk = "#FF6600"; }
    Serial.println("ROLE ACIK - Degerler sifirlandi");
    return;
  }

  if (isnan(voltage) || isnan(current) || isnan(power)) {
    Serial.println("Hata: Veri okunamadi");
    return;
  }

  // Çöp değer koruması (röle geçişinde spike olur)
  if (voltage > 260 || voltage < 150 || current > 100 || power > 4000) {
    Serial.println("Hata: Gecersiz deger - atlanıyor");
    return;
  }

  Serial.print("VOLTAJ: ");
  Serial.print(voltage);
  Serial.print(" V, AKIM: ");
  Serial.print(current);
  Serial.print(" A, GUC: ");
  Serial.print(power);
  Serial.print(" W, ENERJI: ");
  Serial.print(isnan(energy) ? 0.0f : energy);
  Serial.println(" kWh");

  // Ofset uygula: ESP8266 adaptörü + kablo kayıplarını çıkar
  float gosterilenGuc = max(0.0f, power - OFFSET_GUC);
  float gosterilenAkim = (voltage > 0) ? (gosterilenGuc / voltage) : 0.0f;

  Blynk.virtualWrite(V2, voltage);
  Blynk.virtualWrite(V3, gosterilenAkim);
  Blynk.virtualWrite(V4, gosterilenGuc);

  float faturaTL = (isnan(energy) ? 0.0f : energy) * BIRIM_FIYAT;
  Blynk.virtualWrite(V9, faturaTL);

  // Röle kapalıyken (akım geçiyor) güç güvenli seviyeye düştüyse bayrakları sıfırla
  if (!roleDurumu && gosterilenGuc < gucEsigi * 0.7f) {
    kullaniciMuhalefet = false;
    asiriUyariGonderildi = false;
  }

  if (gosterilenGuc > gucEsigi) {
    Blynk.virtualWrite(V6, "ASIRI TUKETIM! " + String(gosterilenGuc, 0) + "W");
    if (sonV6Renk != "#FF0000") { Blynk.setProperty(V6, "color", "#FF0000"); sonV6Renk = "#FF0000"; }
    if (!asiriUyariGonderildi && !kullaniciMuhalefet && bootTamamlandi) {
      unsigned long simdi = millis();
      if (simdi - roleSonDegisim >= ROLE_DEBOUNCE) {
        digitalWrite(ROLE_PIN, LOW);
        roleDurumu = true;
        roleSonDegisim = simdi;
        Blynk.virtualWrite(V5, 1);
        Blynk.logEvent("asiri_guc", String("Asiri Guc! Cihaz kapatildi: ") + String(gosterilenGuc, 0) + "W");
        Serial.println("!!! ASIRI GUC - ROLE OTOMATIK KAPATILDI !!!");
        asiriUyariGonderildi = true;
      }
    }
  } else {
    Blynk.virtualWrite(V6, "Guvenli - " + String(gosterilenGuc, 0) + "W");
    if (sonV6Renk != "#00CC00") { Blynk.setProperty(V6, "color", "#00CC00"); sonV6Renk = "#00CC00"; }
  }

  if (gosterilenGuc < 4000) {
    olcumSayisi++;
    if (olcumSayisi == 1) {
      ortalamaGuc = gosterilenGuc;
    } else {
      ortalamaGuc = ortalamaGuc * 0.7 + gosterilenGuc * 0.3;
    }
    if (gosterilenGuc > maxGuc) maxGuc = gosterilenGuc;
  }
}

// Ek metrikler - 10sn'de bir (V11-V15)
void sendExtra() {
  float pf = pzem.pf();
  if (!isnan(pf)) Blynk.virtualWrite(V11, pf);

  float freq = pzem.frequency();
  if (!isnan(freq)) Blynk.virtualWrite(V12, freq);

  Blynk.virtualWrite(V13, WiFi.RSSI());

  unsigned long toplamSn = millis() / 1000;
  unsigned long saat = toplamSn / 3600;
  unsigned long dak   = (toplamSn % 3600) / 60;
  unsigned long sn    = toplamSn % 60;
  String uptime = String(saat) + "sa " + String(dak) + "dk " + String(sn) + "sn";
  Blynk.virtualWrite(V14, uptime);

  Blynk.virtualWrite(V15, maxGuc);
}

// Her 5sn'de bir öneri gönder
void sendOneri() {
  if (roleDurumu) {
    Blynk.virtualWrite(V8, "Role ACIK! Akim kesildi. Ort: " + String(ortalamaGuc, 0) + "W");
    if (sonV8Renk != "#FF6600") { Blynk.setProperty(V8, "color", "#FF6600"); sonV8Renk = "#FF6600"; }
    return;
  }

  if (olcumSayisi < 10) {
    int yuzdelik = ((int)((olcumSayisi * 100) / 10) / 20) * 20;
    Blynk.virtualWrite(V8, "Veri toplaniyor... %" + String(yuzdelik));
    if (sonV8Renk != "#808080") { Blynk.setProperty(V8, "color", "#808080"); sonV8Renk = "#808080"; }
    return;
  }

  float guncelGuc = pzem.power();
  if (isnan(guncelGuc) || guncelGuc > 4000) {
    Serial.println("ONERI: guncelGuc gecersiz - atlanıyor");
    return;
  }

  if (ortalamaGuc <= 0) {
    Blynk.virtualWrite(V8, "Ortalama hesaplaniyor...");
    return;
  }

  String oneri = "";
  String yeniRenk = "";
  if (guncelGuc < 1.0) {
    oneri = "Cihaz bagli degil. Lutfen bir cihaz baglayiniz.";
    yeniRenk = "#808080";
  } else if (guncelGuc < 5.0) {
    oneri = "Verimli! Minimum tuketim: " + String(guncelGuc, 1) + "W (Ort: " + String(ortalamaGuc, 0) + "W)";
    yeniRenk = "#00CC00";
  } else if (guncelGuc < 100.0) {
    oneri = "Normal tuketim: " + String(guncelGuc, 1) + "W (Ort: " + String(ortalamaGuc, 0) + "W)";
    yeniRenk = "#0066FF";
  } else {
    int yuzde = (ortalamaGuc > 0) ? (int)(((guncelGuc - ortalamaGuc) / ortalamaGuc) * 100) : 0;
    oneri = "Dikkat! Yuksek tuketim: " + String(guncelGuc, 1) + "W (Ort: " + String(ortalamaGuc, 0) + "W, %" + String(yuzde) + " fazla)";
    yeniRenk = "#FF0000";
  }

  Blynk.virtualWrite(V8, oneri);
  if (sonV8Renk != yeniRenk) { Blynk.setProperty(V8, "color", yeniRenk); sonV8Renk = yeniRenk; }
  Serial.println("Oneri: " + oneri);
}

void setup() {
  Serial.begin(115200);
  mySerial.begin(9600);

  pinMode(ROLE_PIN, OUTPUT);
  digitalWrite(ROLE_PIN, HIGH);  // Başlangıçta akım geçsin

  pinMode(BUTON_PIN, INPUT_PULLUP);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setTimeout(3000L, bootTamamla);
  timer.setInterval(1000L, sendData);  // 1sn - anlık bildirim için
  timer.setInterval(5000L, sendOneri);
  timer.setInterval(50L, butonKontrol);
  timer.setInterval(3000L, ledBlink);
  timer.setInterval(10000L, sendExtra);

  Serial.println("PZEM-004T Enerji Monitoru Baslatildi");
}

void loop() {
  Blynk.run();
  timer.run();
}
