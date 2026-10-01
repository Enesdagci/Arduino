#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <stdlib.h>   
#include "FS.h"
#include "SPIFFS.h"

// ================== WiFi Ayarlari ==================
const char* wifiAdi   = "0xED";
const char* wifiSifre = "1q2w3e4r";   // en az 8 karakter olmali

WebServer sunucu(80);

// ================== Pin tanimlamalari ==================
int pinNtc[4] = {A0, A1, A2, A3};
int pinAdcRef = A4;
int pinNtcRef = A5;

// ================== Sabitler ==================
const float vcc = 3.3;
const float adc_max = 4095.0;
const float res = 4700.0;
const float res_ntc = 4700.0;
const float beta = 3950.0;
const int OVERSAMPLE_N = 64;
const float esikHafif = 1.0;
const float esikKritik = 3.0;
const char* bolgeler[4] = {"Bolge1(Ust-Sag)", "Bolge2(Ust-Sol)", "Bolge3(Alt-Sag)", "Bolge4(Alt-Sol)"};
const int olcumSayisi = 10;
const float refKaymaEsik = 0.50;
const unsigned long isinmaSuresiIlk = 90;
const unsigned long isinmaSuresiTekrar = 15;

// Temas kaybi tespiti icin sabitler (36. gun).
const float SICAKLIK_TABAN_ESIK = 26.0;
const float KANAL_SAPMA_ESIGI = 4.0;
const float DIGERLERI_TUTARLILIK_ESIGI = 1.5;
const int TEMAS_PENCERE = 10;
const float DUZLUK_ESIGI = 0.15;

float refGecmis[10];
int refGecmisIndex = 0;
bool refGecmisDolu = false;
float baslangicOffset[4] = {0, 0, 0, 0};
bool baslangicKaydetme = false;

// 39. gun: REF karasizlik sifirlamalarindan ETKILENMEZ - sadece cihazin ilk
// 90 sn'lik yerlesmesi bir kez tamamlaninca true olur, bir daha false olmaz.
// Alarm blogundaki B yolunun (REF'e bagimli olmayan capraz-kanal kontrolu)
// REF kararsizken de calismaya devam etmesi icin kullanilir.
bool ilkBaselineKaydedildiMi = false;

unsigned long baslangicZamani = 0;
bool oncekiRefKararsiz = false;
bool ilkIsinma = true;

// DEĞİŞTİ (36. gün - REF eklentisi): indeks 0-3 = diz kanallari (B1-B4),
// indeks 4 = REF (saglikli doku referansi). REF'in de kendi gecmis
// penceresi olmasi gerekiyor cunku o da fiziksel olarak elden
// cikarilabilir/gevseyebilir - bunu gercek testte gorduk.
const int TEMAS_KANAL_SAYISI = 5;
float kanalGecmis[TEMAS_KANAL_SAYISI][TEMAS_PENCERE];
int kanalGecmisIndex[TEMAS_KANAL_SAYISI] = {0, 0, 0, 0, 0};
bool kanalGecmisDolu[TEMAS_KANAL_SAYISI] = {false, false, false, false, false};
bool oncekiTemasKoptu[TEMAS_KANAL_SAYISI] = {false, false, false, false, false};

const float kalibrasyonOffset[5]{ +0.17, -0.54, +0.36, +2.36, -1.43 };

struct OlcumKaydi {
  unsigned long sira;
  unsigned long zamanMs;
  float b1, b2, b3, b4, ref;
  float dt1, dt2, dt3, dt4;
  bool alarm;
  bool isiniyor;
  bool refKararsiz;
  byte temasDurumu; // bit0-3 = B1-B4, bit4 = REF -> 1 = temas kaybi supheli
};

const int ARABELLEK_KAPASITE = 1800;
OlcumKaydi arabellek[ARABELLEK_KAPASITE];
int arabellekYaziIndex = 0;
unsigned long toplamKayitSayisi = 0;

long saatFarkiSaniye = 0;
bool saatSenkronize = false;

const char* GUNLUK_DOSYA = "/gunluk.csv";
const char* GUNLUK_YEDEK_DOSYA = "/gunluk_eski.csv";
const size_t GUNLUK_MAKS_BOYUT = 300000;
const unsigned long GUNLUK_YAZMA_ARALIGI = 10000;
bool spiffsHazir = false;
String gunlukBekleyenSatirlar = "";
unsigned long sonGunlukYazmaZamani = 0;
unsigned long oturumBaslangicSira = 0;

void kaydiArabellegeYaz(const OlcumKaydi& k) {
  arabellek[arabellekYaziIndex] = k;
  arabellekYaziIndex = (arabellekYaziIndex + 1) % ARABELLEK_KAPASITE;
}

long okuOrtalama(int pin) {
  long toplam = 0;
  for (int i = 0; i < OVERSAMPLE_N; i++) { toplam += analogRead(pin); delayMicroseconds(100); }
  return toplam / OVERSAMPLE_N;
}

float direnctenSicakliga(float r_ntc) {
  return 1.0 / ( (1.0 / (25.0 + 273.15)) + (1.0 / beta) * log(r_ntc / res_ntc) ) - 273.15;
}

float pinSicaklikHesaplama(int pin, long pinAdcRef, int index) {
  long adcHam = okuOrtalama(pin);
  float vout = vcc * (float)adcHam / (float) pinAdcRef;
  float r_ntc = res * (vcc - vout) / vout;
  return direnctenSicakliga(r_ntc) + kalibrasyonOffset[index];
}

bool refKararsiz(float yeniRef) {
  float eskiRef = refGecmis[refGecmisIndex];
  refGecmis[refGecmisIndex] = yeniRef;
  refGecmisIndex = (refGecmisIndex + 1) % olcumSayisi;
  if (!refGecmisDolu) { if (refGecmisIndex == 0) refGecmisDolu = true; return false; }
  float kayma = abs(yeniRef - eskiRef);
  return kayma >= refKaymaEsik;
}

// kanalIndex (0-3 diz, 4 REF) icin son TEMAS_PENCERE saniyenin gecmisini
// gunceller ve o penceredeki (maks-min) farkini dondurur. Pencere henuz
// dolmadiysa 999.0 ("degisken varsay") doner.
float kanalDegisimAraligi(int kanalIndex, float yeniDeger) {
  kanalGecmis[kanalIndex][kanalGecmisIndex[kanalIndex]] = yeniDeger;
  kanalGecmisIndex[kanalIndex] = (kanalGecmisIndex[kanalIndex] + 1) % TEMAS_PENCERE;
  if (!kanalGecmisDolu[kanalIndex]) {
    if (kanalGecmisIndex[kanalIndex] == 0) kanalGecmisDolu[kanalIndex] = true;
    return 999.0;
  }
  float mn = kanalGecmis[kanalIndex][0], mx = kanalGecmis[kanalIndex][0];
  for (int j = 1; j < TEMAS_PENCERE; j++) {
    if (kanalGecmis[kanalIndex][j] < mn) mn = kanalGecmis[kanalIndex][j];
    if (kanalGecmis[kanalIndex][j] > mx) mx = kanalGecmis[kanalIndex][j];
  }
  return mx - mn;
}

String kaydiJsonYap(const OlcumKaydi& k) {
  String j = "{";
  j += "\"sira\":" + String(k.sira) + ",";
  j += "\"zamanMs\":" + String(k.zamanMs) + ",";
  if (saatSenkronize) {
    unsigned long unixSaniyeKayit = (unsigned long)((long)(k.zamanMs / 1000) + saatFarkiSaniye);
    j += "\"unixSaniye\":" + String(unixSaniyeKayit) + ",";
  } else { j += "\"unixSaniye\":0,"; }
  j += "\"b1\":" + String(k.b1, 2) + ",";
  j += "\"b2\":" + String(k.b2, 2) + ",";
  j += "\"b3\":" + String(k.b3, 2) + ",";
  j += "\"b4\":" + String(k.b4, 2) + ",";
  j += "\"ref\":" + String(k.ref, 2) + ",";
  j += "\"dt1\":" + String(k.dt1, 2) + ",";
  j += "\"dt2\":" + String(k.dt2, 2) + ",";
  j += "\"dt3\":" + String(k.dt3, 2) + ",";
  j += "\"dt4\":" + String(k.dt4, 2) + ",";
  j += "\"alarm\":" + String(k.alarm ? "true" : "false") + ",";
  j += "\"isiniyor\":" + String(k.isiniyor ? "true" : "false") + ",";
  j += "\"refKararsiz\":" + String(k.refKararsiz ? "true" : "false") + ",";
  j += "\"temasDurumu\":" + String(k.temasDurumu);
  j += "}";
  return j;
}

String kaydiCsvYap(const OlcumKaydi& k) {
  unsigned long unixSaniyeKayit = saatSenkronize
      ? (unsigned long)((long)(k.zamanMs / 1000) + saatFarkiSaniye)
      : 0;
  String satir = String(k.sira) + "," + String(k.zamanMs) + "," + String(unixSaniyeKayit);
  satir += "," + String(k.b1, 2) + "," + String(k.b2, 2) + "," + String(k.b3, 2) + "," + String(k.b4, 2);
  satir += "," + String(k.ref, 2);
  satir += "," + String(k.dt1, 2) + "," + String(k.dt2, 2) + "," + String(k.dt3, 2) + "," + String(k.dt4, 2);
  satir += "," + String(k.alarm ? 1 : 0) + "," + String(k.isiniyor ? 1 : 0) + "," + String(k.refKararsiz ? 1 : 0);
  satir += "," + String(k.temasDurumu);
  satir += "\n";
  return satir;
}

void gunlukeYaz() {
  if (!spiffsHazir || gunlukBekleyenSatirlar.length() == 0) return;
  File kontrol = SPIFFS.open(GUNLUK_DOSYA, FILE_READ);
  size_t mevcutBoyut = kontrol ? kontrol.size() : 0;
  if (kontrol) kontrol.close();
  if (mevcutBoyut >= GUNLUK_MAKS_BOYUT) {
    SPIFFS.remove(GUNLUK_YEDEK_DOSYA);
    SPIFFS.rename(GUNLUK_DOSYA, GUNLUK_YEDEK_DOSYA);
    Serial.println("Gunluk dosyasi doldu, yedeklendi ve sifirlandi (rotate).");
  }
  File dosya = SPIFFS.open(GUNLUK_DOSYA, FILE_APPEND);
  if (!dosya) { Serial.println("UYARI: gunluk dosyasina yazilamadi."); return; }
  size_t yaziliBayt = dosya.print(gunlukBekleyenSatirlar);
  dosya.close();
  Serial.print("Gunluk flash'a yazildi (");
  Serial.print(yaziliBayt);
  Serial.println(" bayt).");
  gunlukBekleyenSatirlar = "";
}

void sayaciGunloktenKurtar() {
  File dosya = SPIFFS.open(GUNLUK_DOSYA, FILE_READ);
  if (!dosya) { Serial.println("Onceki gunluk bulunamadi, sayac 0'dan basliyor."); return; }
  String sonSatir = "";
  while (dosya.available()) {
    String satir = dosya.readStringUntil('\n');
    satir.trim();
    if (satir.length() > 0) sonSatir = satir;
  }
  dosya.close();
  if (sonSatir.length() == 0) return;
  int virgulIndex = sonSatir.indexOf(',');
  if (virgulIndex <= 0) return;
  unsigned long kurtarilanSira = strtoul(sonSatir.substring(0, virgulIndex).c_str(), nullptr, 10);
  if (kurtarilanSira > 0) {
    toplamKayitSayisi = kurtarilanSira;
    arabellekYaziIndex = (int)(kurtarilanSira % (unsigned long)ARABELLEK_KAPASITE);
    oturumBaslangicSira = kurtarilanSira;
    Serial.print("Sayac gunlukten kurtarildi, kaldigi yerden devam ediyor: ");
    Serial.println(toplamKayitSayisi);
  }
}

void handleSimdi() {
  if (toplamKayitSayisi == 0 || toplamKayitSayisi <= oturumBaslangicSira) {
    sunucu.send(404, "text/plain", "Henuz olcum yok");
    return;
  }
  int sonIndex = (int)((toplamKayitSayisi - 1) % ARABELLEK_KAPASITE);
  sunucu.send(200, "application/json", kaydiJsonYap(arabellek[sonIndex]));
}

void handleZamanAyarla() {
  if (!sunucu.hasArg("unixSaniye")) { sunucu.send(400, "text/plain", "unixSaniye parametresi eksik"); return; }
  unsigned long unixSaniye = strtoul(sunucu.arg("unixSaniye").c_str(), nullptr, 10);
  saatFarkiSaniye = (long)unixSaniye - (long)(millis() / 1000);
  saatSenkronize = true;
  Serial.print("Saat senkronize edildi. Fark (sn): ");
  Serial.println(saatFarkiSaniye);

  if (spiffsHazir) {
    gunlukBekleyenSatirlar = "";
    SPIFFS.remove(GUNLUK_DOSYA);
    SPIFFS.remove(GUNLUK_YEDEK_DOSYA);
    sonGunlukYazmaZamani = millis();
    Serial.println("Telefon baglandi: flash gunlugu temizlendi.");
  }

  sunucu.send(200, "text/plain", "OK");
}

void handleGecmis() {
  unsigned long sonSira = 0;
  if (sunucu.hasArg("sonSira")) sonSira = strtoul(sunucu.arg("sonSira").c_str(), nullptr, 10);
  const int MAKS_PARCA = 100;
  String j = "{\"kayitlar\":[";
  bool ilk = true;
  int eklenen = 0;
  unsigned long en_eski_sira = (toplamKayitSayisi > (unsigned long)ARABELLEK_KAPASITE)
      ? (toplamKayitSayisi - ARABELLEK_KAPASITE) : 0;
  if (oturumBaslangicSira > en_eski_sira) en_eski_sira = oturumBaslangicSira;
  bool veriAtlandi = (sonSira > 0 && sonSira < en_eski_sira);
  for (unsigned long s = (sonSira > en_eski_sira ? sonSira : en_eski_sira) + 1;
       s <= toplamKayitSayisi && eklenen < MAKS_PARCA; s++) {
    int idx = (int)((s - 1) % ARABELLEK_KAPASITE);
    if (arabellek[idx].sira != s) continue;
    if (!ilk) j += ",";
    j += kaydiJsonYap(arabellek[idx]);
    ilk = false;
    eklenen++;
  }
  j += "],";
  j += "\"veriAtlandi\":" + String(veriAtlandi ? "true" : "false") + ",";
  j += "\"toplamUretilen\":" + String(toplamKayitSayisi);
  j += "}";
  sunucu.send(200, "application/json", j);
}

void handleDurum() {
  String j = "{";
  j += "\"calisiyor\":true,";
  j += "\"acilmadanBeriMs\":" + String(millis()) + ",";
  j += "\"toplamUretilen\":" + String(toplamKayitSayisi) + ",";
  j += "\"arabellekKapasite\":" + String(ARABELLEK_KAPASITE) + ",";
  j += "\"wifiBagli\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  j += "\"sinyalGucuDbm\":" + String(WiFi.RSSI()) + ",";
  j += "\"flashKalicilik\":" + String(spiffsHazir ? "true" : "false");
  j += "}";
  sunucu.send(200, "application/json", j);
}

void handleGunlukIndir() {
  if (!spiffsHazir) { sunucu.send(404, "text/plain", "SPIFFS aktif degil"); return; }
  gunlukeYaz();
  File dosya = SPIFFS.open(GUNLUK_DOSYA, FILE_READ);
  if (!dosya) { sunucu.send(404, "text/plain", "Aktif gunluk dosyasi yok"); return; }
  sunucu.sendHeader("Content-Disposition", "attachment; filename=gunluk.csv");
  sunucu.streamFile(dosya, "text/csv");
  dosya.close();
}

void handleGunlukIndirYedek() {
  if (!spiffsHazir) { sunucu.send(404, "text/plain", "SPIFFS aktif degil"); return; }
  File dosya = SPIFFS.open(GUNLUK_YEDEK_DOSYA, FILE_READ);
  if (!dosya) { sunucu.send(404, "text/plain", "Yedek gunluk dosyasi yok"); return; }
  sunucu.sendHeader("Content-Disposition", "attachment; filename=gunluk_eski.csv");
  sunucu.streamFile(dosya, "text/csv");
  dosya.close();
}

void handleBulunamadi() { sunucu.send(404, "text/plain", "Bulunamadi"); }

void setup() {
  Serial.begin(9600);
  Serial.println("Sistem baslatildi. 4 Bolge + 1 Saglikli Doku (WiFi istemci surumu)");
  if (SPIFFS.begin(true)) {
    spiffsHazir = true;
    Serial.print("SPIFFS hazir. Toplam: ");
    Serial.print(SPIFFS.totalBytes() / 1024);
    Serial.print(" KB, Kullanilan: ");
    Serial.print(SPIFFS.usedBytes() / 1024);
    Serial.println(" KB");
    sayaciGunloktenKurtar();
  } else {
    Serial.println("UYARI: SPIFFS baslatilamadi — flash kalicilik devre disi, sadece RAM arabellek kullanilacak.");
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(wifiAdi, wifiSifre);
  Serial.print("Hotspot'a baglaniliyor: ");
  Serial.println(wifiAdi);
  unsigned long baslangic = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - baslangic < 20000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(); Serial.print("Baglandi! ESP32 adresi: http://"); Serial.println(WiFi.localIP());
    if (MDNS.begin("termodiz")) {
      Serial.println("mDNS baslatildi: http://termodiz.local adresinden de erisilebilir.");
    } else {
      Serial.println("UYARI: mDNS baslatilamadi — sadece IP adresiyle erisilebilir.");
    }
  } else {
    Serial.println(); Serial.println("UYARI: 20 saniyede baglanamadi, loop() icinde tekrar denenecek.");
  }
  sunucu.on("/simdi", handleSimdi);
  sunucu.on("/gecmis", handleGecmis);
  sunucu.on("/zamanAyarla", handleZamanAyarla);
  sunucu.on("/durum", handleDurum);
  sunucu.on("/gunlukIndir", handleGunlukIndir);
  sunucu.on("/gunlukIndirYedek", handleGunlukIndirYedek);
  sunucu.onNotFound(handleBulunamadi);
  sunucu.begin();
  Serial.println("HTTP sunucu baslatildi.");
}

unsigned long sonOlcumZamani = 0;
unsigned long sonBaglantiKontrolu = 0;

void loop() {
  sunucu.handleClient();
  unsigned long simdiKontrol = millis();
  if (WiFi.status() != WL_CONNECTED && simdiKontrol - sonBaglantiKontrolu > 5000) {
    sonBaglantiKontrolu = simdiKontrol;
    Serial.println("WiFi baglantisi yok, yeniden deneniyor...");
    WiFi.reconnect();
  }
  unsigned long simdi = millis();
  if (simdi - sonOlcumZamani < 1000) return;
  sonOlcumZamani = simdi;

  long adcRef = okuOrtalama(pinAdcRef);
  float sicakliklar[4];
  for (int i = 0; i < 4; i++) sicakliklar[i] = pinSicaklikHesaplama(pinNtc[i], adcRef, i);
  float sicaklikSagDoku = pinSicaklikHesaplama(pinNtcRef, adcRef, 4);
  float mutlakDeltaT[4];
  for (int i = 0; i < 4; i++) mutlakDeltaT[i] = sicakliklar[i] - sicaklikSagDoku;

  // ===== 1. GECIS: her kanalin (ve REF'in) kendi baslarina bakilan
  // sinyallerini hesapla - baska kanala bagimli DEGIL. Boylece "zaten
  // supheli" kanallari 2. gecisteki kiyaslamadan disleyebilecegiz.
  bool tabanAlti[4], duzZeminde[4], onKoyu[4];
  for (int i = 0; i < 4; i++) {
    tabanAlti[i] = sicakliklar[i] < SICAKLIK_TABAN_ESIK;
    float aralik = kanalDegisimAraligi(i, sicakliklar[i]);
    duzZeminde[i] = aralik < DUZLUK_ESIGI;
    onKoyu[i] = tabanAlti[i] && duzZeminde[i];
  }
  // REF icin de ayni iki sinyal - ama capraz kiyaslamasi yok (REF tek,
  // "digerleriyle karsilastirma" kavrami REF icin anlamli degil, cunku
  // dizin REF'ten farkli olmasi zaten olculmek istenen sey).
  bool tabanAltiRef = sicaklikSagDoku < SICAKLIK_TABAN_ESIK;
  float aralikRef = kanalDegisimAraligi(4, sicaklikSagDoku);
  bool duzZemindeRef = aralikRef < DUZLUK_ESIGI;
  bool temasKoptuRef = tabanAltiRef && duzZemindeRef;

  // ===== 2. GECIS: capraz kiyaslama - "digerleri" derken, zaten 1.
  // gecişte ONKOYU (kesin supheli) isaretlenmis kanallari SAYMA. Boylece
  // iki kanal ayni anda bozulsa bile (34. gunun test verisinde oldugu
  // gibi B1+B4), biri digerini "digerleri tutarsiz" diye maskeleyemez.
  bool temasKoptu[4];
  byte temasBit = 0;
  for (int i = 0; i < 4; i++) {
    float digerToplam = 0; int digerSayisi = 0;
    float digerMin = 999, digerMax = -999;
    for (int j = 0; j < 4; j++) {
      if (j == i || onKoyu[j]) continue; // zaten supheli olani kiyasa katma
      digerToplam += sicakliklar[j];
      digerSayisi++;
      if (sicakliklar[j] < digerMin) digerMin = sicakliklar[j];
      if (sicakliklar[j] > digerMax) digerMax = sicakliklar[j];
    }
    bool sapmaVar = false;
    if (digerSayisi >= 2) {
      float digerOrtalama = digerToplam / digerSayisi;
      bool digerleriTutarli = (digerMax - digerMin) < DIGERLERI_TUTARLILIK_ESIGI;
      sapmaVar = digerleriTutarli && (abs(sicakliklar[i] - digerOrtalama) >= KANAL_SAPMA_ESIGI);
    }
    temasKoptu[i] = tabanAlti[i] && (sapmaVar || duzZeminde[i]);
    if (temasKoptu[i]) temasBit |= (1 << i);

    if (temasKoptu[i] != oncekiTemasKoptu[i]) {
      Serial.print(temasKoptu[i] ? "Temas kaybi SUPHESI basladi: " : "Temas kaybi supheli durum bitti: ");
      Serial.print(bolgeler[i]);
      Serial.print(" (");
      Serial.print(sicakliklar[i], 2);
      Serial.println(" C)");
      oncekiTemasKoptu[i] = temasKoptu[i];
    }
  }
  if (temasKoptuRef) temasBit |= (1 << 4);
  if (temasKoptuRef != oncekiTemasKoptu[4]) {
    Serial.print(temasKoptuRef ? "Temas kaybi SUPHESI basladi: REF(SaglikliDoku) (" : "Temas kaybi supheli durum bitti: REF(SaglikliDoku) (");
    Serial.print(sicaklikSagDoku, 2);
    Serial.println(" C)");
    oncekiTemasKoptu[4] = temasKoptuRef;
  }
  bool tumKanallarTemassiz = ((temasBit & 0b1111) == 0b1111);
  if (tumKanallarTemassiz) {
    Serial.println("UYARI: 4 diz kanali da temas kaybi supheli - cihaz vucuttan cikarilmis olabilir.");
  }

  bool refKararsizlik = refKararsiz(sicaklikSagDoku);
  if (refKararsizlik && !oncekiRefKararsiz) {
    baslangicZamani = millis();
    baslangicKaydetme = false;
    ilkIsinma = false;
    Serial.println("REF kararsizligi tespit edildi");
  }
  oncekiRefKararsiz = refKararsizlik;

  unsigned long guncelIsinmaSuresi = ilkIsinma ? isinmaSuresiIlk : isinmaSuresiTekrar;
  bool isinma = (millis() - baslangicZamani) < (guncelIsinmaSuresi * 1000UL);
  if (!isinma && !baslangicKaydetme) {
    for (int i = 0; i < 4; i++) baslangicOffset[i] = mutlakDeltaT[i];
    baslangicKaydetme = true;
    ilkBaselineKaydedildiMi = true;   // 39. gun eklendi
    Serial.println("baslangic kaydedildi.");
  }

  float duzeltilmisDeltaT[4];
  for (int i = 0; i < 4; i++) duzeltilmisDeltaT[i] = mutlakDeltaT[i] - baslangicOffset[i];

  // 39. gun: alarm blogu iki bagimsiz yola ayrildi (asagida).
  bool alarmVar = false;

  // A yolu: REF'e gore baseline'dan sapma. REF supheli/kararsizsa (baseline
  // henuz yeniden kaydedilmediyse) degerlendirilmez - bu davranis DEGISMEDI,
  // cunku bu yol gercekten gecerli bir REF baseline'ina muhtac.
  if (!temasKoptuRef) {
    for (int i = 0; i < 4; i++) {
      if (baslangicKaydetme && !temasKoptu[i] && abs(duzeltilmisDeltaT[i]) >= esikKritik) alarmVar = true;
    }
  }

  // B yolu: sadece B1-B4 arasindaki ham fark - REF'e hic bagli degil, o
  // yuzden REF'in o an supheli/kararsiz olmasindan ETKILENMEMELI. Sadece
  // cihazin ilk yerlesme suresinin bir kez tamamlanmis olmasi yeterli.
  // (39. gun testinde bulunan 15 sn'lik "kor pencere" sorununu kapatir.)
  if (ilkBaselineKaydedildiMi) {
    float maxV = -999, minV = 999;
    bool gecerliKanalVar = false;
    for (int i = 0; i < 4; i++) {
      if (temasKoptu[i]) continue;
      if (sicakliklar[i] > maxV) maxV = sicakliklar[i];
      if (sicakliklar[i] < minV) minV = sicakliklar[i];
      gecerliKanalVar = true;
    }
    if (gecerliKanalVar && (maxV - minV) >= esikKritik) alarmVar = true;
  }

  Serial.print("B1:" + String(sicakliklar[0], 2));
  Serial.print(" B2:" + String(sicakliklar[1], 2));
  Serial.print(" B3:" + String(sicakliklar[2], 2));
  Serial.print(" B4:" + String(sicakliklar[3], 2));
  Serial.print(" REF:" + String(sicaklikSagDoku, 2));
  Serial.print(" ALARM:" + String(alarmVar ? 1 : 0));
  Serial.print(" TEMAS:" + String(temasBit));
  Serial.print(" | wifi:" + String(WiFi.status() == WL_CONNECTED ? "bagli" : "KOPTU"));
  Serial.println(" | sira:" + String(toplamKayitSayisi + 1));

  toplamKayitSayisi++;
  OlcumKaydi k;
  k.sira = toplamKayitSayisi;
  k.zamanMs = millis();
  k.b1 = sicakliklar[0]; k.b2 = sicakliklar[1]; k.b3 = sicakliklar[2]; k.b4 = sicakliklar[3];
  k.ref = sicaklikSagDoku;
  k.dt1 = duzeltilmisDeltaT[0]; k.dt2 = duzeltilmisDeltaT[1]; k.dt3 = duzeltilmisDeltaT[2]; k.dt4 = duzeltilmisDeltaT[3];
  k.alarm = alarmVar;
  k.isiniyor = isinma;
  k.refKararsiz = refKararsizlik;
  k.temasDurumu = temasBit;
  kaydiArabellegeYaz(k);

  if (spiffsHazir) gunlukBekleyenSatirlar += kaydiCsvYap(k);
  unsigned long simdiGunluk = millis();
  if (spiffsHazir && (simdiGunluk - sonGunlukYazmaZamani >= GUNLUK_YAZMA_ARALIGI)) {
    sonGunlukYazmaZamani = simdiGunluk;
    gunlukeYaz();
  }
}
