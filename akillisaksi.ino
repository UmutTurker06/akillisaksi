#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include "time.h"

/* ================= KULLANICI AYARLARI ================= */
#define WIFI_SSID     "Umut Türker "
#define WIFI_PASSWORD "76782210"

/* Firebase Ayarları */
#define FIREBASE_API_KEY "AIzaSyAHbNojJAFm1Qqs7N7l7ojKEKb_Ipk4SSE"
#define FIREBASE_DB_URL  "akillisaksi-30751-default-rtdb.firebaseio.com"

/* ================= PIN TANIMLAMALARI ================= */
#define DHTPIN 27
#define DHTTYPE DHT11
#define SOIL_PIN  34
#define RELAY_PIN 5

/* ================= NESNELER ================= */
Adafruit_SH1106G display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

/* ================= DURUM ================= */
bool autoMode = true;
bool motorState = false;
bool lastMotorState = false;

float tempVal = 0;
float humVal  = 0;
int soilVal   = 0;

unsigned long lastRead = 0;

/* ================= ZAMAN ================= */
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800;
const int   daylightOffset_sec = 0;

/* ================= WEB ARAYÜZÜ (GÜNCELLENDİ) ================= */
const char index_html[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Akıllı Saksı</title>
<script src="https://www.gstatic.com/firebasejs/8.10.1/firebase-app.js"></script>
<script src="https://www.gstatic.com/firebasejs/8.10.1/firebase-database.js"></script>
<style>
body{margin:0;font-family:Arial;background:#121212;color:#e0e0e0}
.container{max-width:420px;margin:auto;padding:20px}
.card{background:#1e1e1e;border-radius:12px;padding:15px;margin-bottom:15px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;text-align:cent
er}
.val{font-size:1.6rem;font-weight:bold;color:#00e676}
.lbl{font-size:0.8rem;color:#aaa}
.btn{width:100%;padding:14px;border:none;border-radius:8px;margin-top:10px;background:#333;color:#fff;font-size:1rem}
.btn.active{background:#00e676;color:#000;font-weight:bold}
.hr{height:1px;background:#333;margin:12px 0}

/* TABLO STİLLERİ */
.history{max-height:200px;overflow:auto;}
table{width:100%;border-collapse:collapse;font-size:0.75rem;color:#ccc}
th, td {padding: 8px 4px; text-align: center; border-bottom: 1px solid #333;}
th {color: #00e676; font-weight: normal; position: sticky; top: 0; background: #1e1e1e;}
</style>
</head>
<body>
<div class="container">
<h2 style="text-align:center;font-weight:300">IITürkerII</h2>

<div class="card">
  <div class="grid">
    <div><div id="temp" class="val">--</div><div class="lbl">Sıcaklık</div></div>
    <div><div id="hum" class="val">--</div><div class="lbl">Nem</div></div>
    <div><div id="soil" class="val">--</div><div class="lbl">Toprak</div></div>
  </div>
</div>

<div class="card">
  <div>Mod: <b id="modText">--</b></div>
  <button class="btn" onclick="sendCmd('/auto')">Mod Değiştir</button>
  <div class="hr"></div>
  <div>Motor: <b id="motorText">--</b></div>
  <button id="btnMotor" class="btn" onclick="sendCmd('/motor')">Motor</button>
</div>

<div class="card">
  <div>Son 15 İşlem</div>
  <div class="history">
    <table id="histTable">
      <thead>
        <tr>
          <th>Saat</th>
          <th>Isı</th>
          <th>Nem</th>
          <th>Toprak</th>
          <th>Mod</th>
          <th>Motor</th>
        </tr>
      </thead>
      <tbody>
        </tbody>
    </table>
  </div>
</div>

</div>
<script>
function sendCmd(url){ fetch(url); }

firebase.initializeApp({
  apiKey:"AIzaSyAHbNojJAFm1Qqs7N7l7ojKEKb_Ipk4SSE",
  databaseURL:"https://akillisaksi-30751-default-rtdb.firebaseio.com"
});
const db = firebase.database();

/* ANLIK VERİ */
db.ref("anlik").on("value", snap=>{
  if(!snap.exists()) return;
  const d = snap.val();
  temp.innerText  = d.sicaklik ?? "--";
  hum.innerText   = d.nem ?? "--";
  soil.innerText  = d.toprak ?? "--";
  modText.innerText = d.otomatikMod ? "OTOMATİK" : "MANUEL";
  motorText.innerText = d.motor ? "ÇALIŞIYOR" : "DURUYOR";
  btnMotor.disabled = d.otomatikMod;
  btnMotor.classList.toggle("active", d.motor);
  btnMotor.style.opacity = d.otomatikMod ? "0.4" : "1";
});

/* GEÇMİŞ TABLOSU */
const tableBody = document.querySelector("#histTable tbody");

db.ref("gecmis").limitToLast(15).on("child_added", snap=>{
  if(!snap.exists()) return;
  const i = snap.val();
  const t = i.zaman ? i.zaman.split(" ")[1] : "--:--";
  
  const row = document.createElement("tr");
  row.innerHTML = `
    <td>${t}</td>
    <td>${i.sicaklik ?? "-"}</td>
    <td>${i.nem ?? "-"}</td>
    <td>${i.toprak}</td>
    <td>${i.mod ?? "-"}</td>
    <td>${i.motor_durumu}</td>
  `;
  // En yeni en üstte dursun diye prepend
  tableBody.prepend(row);
});
</script>
</body>
</html>)=====";

/* ================= YARDIMCI FONKSİYONLAR ================= */
String getCurrentTime(){
  struct tm t;
  if(!getLocalTime(&t)) return "Zaman Yok";
  char buf[30];
  strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&t);
  return String(buf);
}

void handleRoot(){ server.send(200,"text/html",index_html); }
void handleMotor(){
  if(!autoMode){
    motorState=!motorState;
    digitalWrite(RELAY_PIN,motorState?LOW:HIGH);
  }
  server.send(200,"text/plain","OK");
}
void handleAuto(){ autoMode=!autoMode; server.send(200,"text/plain","OK"); }

/* ================= SETUP ================= */
void setup(){
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  Wire.begin(21, 22);
  
  display.begin(0x3C, true); 
  display.display(); 
  delay(1000); 
  display.clearDisplay(); 
  display.setTextColor(SH110X_WHITE); 
  display.setCursor(0,0); 
  display.setTextSize(1);
  display.println("Sistem Aciliyor..."); 
  display.display();

  dht.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  display.setCursor(0, 20);
  display.println("WiFi Baglaniyor...");
  display.display();

  while(WiFi.status() != WL_CONNECTED) { 
    delay(300); 
  }

  display.clearDisplay(); 
  display.setCursor(0,0); 
  display.println("WiFi Baglandi!"); 
  display.print("IP: "); 
  display.println(WiFi.localIP()); 
  display.display();
  delay(2000);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DB_URL;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  server.on("/", handleRoot);
  server.on("/motor", handleMotor);
  server.on("/auto", handleAuto);
  server.begin();
}

/* ================= LOOP ================= */
void loop(){
  server.handleClient();

  if(millis() - lastRead > 2000){
    lastRead = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int s = analogRead(SOIL_PIN);

    if(!isnan(t)) tempVal = t;
    if(!isnan(h)) humVal = h;
    soilVal = s;

    if(autoMode){
      bool on = soilVal > 3000;
      if(on != motorState){
        motorState = on;
        digitalWrite(RELAY_PIN, on ? LOW : HIGH);
      }
    }

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("--- DURUM ---");
    display.print("Sicaklik: "); display.print(tempVal); display.println(" C");
    display.print("Nem:      "); display.print(humVal); display.println(" %");
    display.print("Toprak:   "); display.println(soilVal);
    display.print("Mod:      "); display.println(autoMode ? "OTOMATIK" : "MANUEL");
    display.print("Motor:    "); display.println(motorState ? "ACIK" : "KAPALI");
    display.setCursor(0, 55); 
    display.print("IP: "); display.print(WiFi.localIP());
    display.display();

    if(Firebase.ready()){
      Firebase.RTDB.setFloat(&fbdo, "/anlik/sicaklik", tempVal);
      Firebase.RTDB.setFloat(&fbdo, "/anlik/nem", humVal);
      Firebase.RTDB.setInt(&fbdo, "/anlik/toprak", soilVal);
      Firebase.RTDB.setBool(&fbdo, "/anlik/motor", motorState);
      Firebase.RTDB.setBool(&fbdo, "/anlik/otomatikMod", autoMode);
    }

    // TABLO İÇİN VERİ GÖNDERME KISMI GÜNCELLENDİ
    if(motorState != lastMotorState){
      lastMotorState = motorState;
      FirebaseJson j;
      j.set("zaman", getCurrentTime());
      j.set("sicaklik", tempVal); // Tablo için eklendi
      j.set("nem", humVal);       // Tablo için eklendi
      j.set("toprak", soilVal);
      j.set("mod", autoMode ? "OTO" : "MAN"); // Tablo için eklendi
      j.set("motor_durumu", motorState ? "ACIK" : "KAPALI");
      Firebase.RTDB.pushJSON(&fbdo, "/gecmis", &j);
    }
  }
}
