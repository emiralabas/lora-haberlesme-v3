// Mega 2560 Pro + RA-02 (SX1278) + Nextion ile LoRa mesajlasma (v4)
// Iki cihaza da ayni kodu yukleyin, sadece CIHAZ_ID degisir.
//
// v3'ten fark: paket alma artik parsePacket() ile degil, kutuphanenin
// kesmeli yontemiyle (onReceive) yapilir. v3'te onTxDone kesmesi gelen
// paketin bayragini temizledigi icin paketler okunamiyordu.
//
// Nextion tarafi (degismedi):
//   t0 : mesaj gecmisi (isbr=True, txt_maxl=200, yeterince yuksek)
//   t2 : taslak kutusu (txt bos, txt_maxl=60)
//   Gonder tusu Touch Release Event:
//       printh 02
//       prints t2.txt,0
//       printh 0A
//       t2.txt=""
//   Tuslarda "Send Component ID" ISARETSIZ olmali.
//
// Baglanti:
//   Nextion TX -> Mega 19 (RX1), Nextion RX -> Mega 18 (TX1)
//   RA-02: SCK 52, MISO 50, MOSI 51, NSS 53, RST 9, DIO0 2 (3.3V!)

#include <SPI.h>
#include <LoRa.h>

// ===== AYARLAR =====
const int  CIHAZ_ID   = 2;        // 1. cihazda 1, 2. cihazda 2
const long FREKANS    = 433E6;    // iki cihazda ayni olmali
const int  MAKS_MESAJ = 40;       // Nextion komutu kisa kalsin

const int PIN_NSS  = 53;
const int PIN_RST  = 9;
const int PIN_DIO0 = 2;

String gecmis[3] = {"", "", ""};  // ekranda son 3 mesaj
String yazilan  = "";             // Nextion'dan gelen taslak
String bekleyen = "";             // gonderim sirasinda gelen mesaj
bool kayit = false;               // 0x02 ile 0x0A arasini okuyoruz

// Gonderim durumu
volatile bool txBitti = false;    // kesmeden set edilir
bool txBekliyor = false;
unsigned long txBasla = 0;

// Alma durumu (kesmeden doldurulur, loop'ta islenir)
char rxBuf[MAKS_MESAJ + 12];
volatile int  rxRssi = 0;
volatile bool rxVar  = false;

void nextionKomut(const String &k) {
  Serial1.print(k);
  Serial1.write(0xFF); Serial1.write(0xFF); Serial1.write(0xFF);
}

// Nextion komutunu bozabilecek karakterleri temizler
String temizle(const String &s) {
  String r = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"')  c = '\'';
    if (c == '\\') c = '/';
    if (c >= 32 && c <= 126) r += c;
  }
  return r;
}

// En yeni mesaj en ustte
void gecmisEkle(const String &satir) {
  gecmis[0] = gecmis[1];
  gecmis[1] = gecmis[2];
  gecmis[2] = temizle(satir);
  nextionKomut("t0.txt=\"" + gecmis[2] + "\\r" + gecmis[1] + "\\r" + gecmis[0] + "\"");
}

// ===== Kesme fonksiyonlari (kisa tutulur) =====
void txBittiCb() { txBitti = true; }

void gelenCb(int boyut) {
  if (rxVar) return;              // onceki paket henuz islenmedi
  int n = 0;
  while (LoRa.available() && n < (int)sizeof(rxBuf) - 1) {
    rxBuf[n++] = (char)LoRa.read();
  }
  while (LoRa.available()) LoRa.read();   // fazlasini at
  rxBuf[n] = 0;
  rxRssi = LoRa.packetRssi();
  rxVar = true;
}

bool loraBaslat() {
  LoRa.setPins(PIN_NSS, PIN_RST, PIN_DIO0);
  if (!LoRa.begin(FREKANS)) return false;
  LoRa.enableCrc();               // bozuk paketleri eler
  LoRa.onReceive(gelenCb);        // paket gelince haber ver
  LoRa.onTxDone(txBittiCb);       // gonderim bitince haber ver
  LoRa.receive();                 // surekli dinleme
  return true;
}

void gonder(const String &m) {
  txBitti = false;
  LoRa.beginPacket();
  LoRa.print(String(CIHAZ_ID) + ": " + m);
  LoRa.endPacket(true);           // beklemeden gonder
  txBekliyor = true;
  txBasla = millis();
  gecmisEkle("Ben: " + m);
  Serial.print(F("TX basladi: "));
  Serial.println(m);
}

void mesajGonder(const String &m) {
  if (txBekliyor) {
    bekleyen = m;                 // onceki gonderim bitince gider
    Serial.println(F("TX mesgul, mesaj siraya alindi"));
  } else {
    gonder(m);
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);            // Nextion baud hizi
  delay(1000);                    // ekranin acilmasini bekle
  nextionKomut("bkcmd=0");        // ekranin onay baytlarini kapat
  nextionKomut("t2.txt=\"\"");

  if (!loraBaslat()) {
    Serial.println(F("LoRa BASLAMADI"));
    nextionKomut("t0.txt=\"LoRa hata\"");
    while (true);
  }
  nextionKomut("t0.txt=\"Hazir\"");
  Serial.println(F("Hazir"));
}

void loop() {
  // 0) 5 saniyede bir "canli" satiri: kod donarsa bu satirlar durur
  static unsigned long sonNabiz = 0;
  if (millis() - sonNabiz > 5000) {
    sonNabiz = millis();
    Serial.print(F("canli "));
    Serial.println(millis() / 1000);
  }

  // 1) Nextion'dan gelen taslak: 0x02 + metin + 0x0A
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == 0x02) {                        // baslangic isareti
      yazilan = "";
      kayit = true;
    } else if (c == '\n') {                 // bitis isareti
      if (kayit && yazilan.length() > 0) {
        Serial.print(F("Nextion'dan: "));
        Serial.println(yazilan);
        mesajGonder(yazilan);
      }
      yazilan = "";
      kayit = false;
    } else if (kayit && c >= 32 && c <= 126 && yazilan.length() < MAKS_MESAJ) {
      yazilan += c;
    }
  }

  // 2) Gonderim takibi
  if (txBekliyor) {
    if (txBitti) {
      txBitti = false;
      txBekliyor = false;
      LoRa.receive();                       // dinlemeye don
      Serial.println(F("TX bitti"));
    } else if (millis() - txBasla > 2000) { // takildiysa modulu yeniden baslat
      Serial.println(F("TX zaman asimi, LoRa yeniden baslatiliyor"));
      LoRa.end();
      delay(50);
      txBekliyor = false;
      if (!loraBaslat()) Serial.println(F("LoRa yeniden BASLAMADI"));
    }
  }

  // 3) Siradaki mesaj
  if (!txBekliyor && bekleyen.length() > 0) {
    String m = bekleyen;
    bekleyen = "";
    gonder(m);
  }

  // 4) Kesmeyle gelen paketi isle
  if (rxVar) {
    String gelen = String(rxBuf);
    int rssi = rxRssi;
    rxVar = false;                          // kopyaladiktan sonra serbest birak
    gecmisEkle(gelen);
    Serial.print(F("Gelen: "));
    Serial.print(gelen);
    Serial.print(F("  RSSI: "));
    Serial.println(rssi);
  }
}
