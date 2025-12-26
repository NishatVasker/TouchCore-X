/*
  ESP32 IoT Digital Alarm Clock (ILI9341 TFT + Buzzer + Stop/Snooze Button)
  - Gets time from NTP (Internet)
  - Displays time/date + alarm status on ILI9341
  - Web page to set alarm from phone/PC (same WiFi)
  - Buzzer rings at alarm time
  - Button stops ringing + snoozes 5 minutes

  ===== WIRING (PCB-Ready) =====
  ILI9341  -> ESP32
  VCC      -> 3V3
  GND      -> GND
  CS       -> GPIO5
  DC       -> GPIO2
  RST      -> GPIO4
  MOSI     -> GPIO23
  SCK      -> GPIO18
  MISO     -> GPIO19 (optional)
  LED      -> 3V3

  BUZZER + -> GPIO25
  BUZZER - -> GND

  Push BUTTON one side -> GPIO27
  push BUTTON other    -> GND   (uses INPUT_PULLUP)

  add an on/off switch

  add a seral line for unused pins, including vcc/3v3/5v/gnd pins

  add battery segment
*/

#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ====== TFT pins ======
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ====== IO ======
#define BUZZER_PIN 25
#define BTN_STOP   27

// ====== WiFi (EDIT THESE) ======
//const char* WIFI_SSID = "YOUR_WIFI_NAME";
//const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";


// ====== Timezone (Bangladesh = UTC+6) ======
const long GMT_OFFSET_SEC = 6 * 3600;
const int  DST_OFFSET_SEC = 0;

// ====== Alarm state ======
int  alarmHour = 7;
int  alarmMin  = 0;
bool alarmEnabled = true;

bool ringing = false;
unsigned long ringStartMs = 0;
const unsigned long RING_DURATION_MS = 60UL * 1000UL; // ring max 60 sec

// Snooze minutes
const int SNOOZE_MINUTES = 5;

WebServer server(80);

// ---------- Buzzer control (Active buzzer: HIGH/LOW) ----------
void buzzerOn()  { digitalWrite(BUZZER_PIN, HIGH); }
void buzzerOff() { digitalWrite(BUZZER_PIN, LOW);  }

// ---------- Helpers ----------
String twoDigits(int v) {
  if (v < 10) return "0" + String(v);
  return String(v);
}

String ipToString(const IPAddress& ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

// ---------- WEB UI ----------
String makeHtmlPage(const String& nowStr, const String& ipStr) {
  String html = R"(
<!DOCTYPE html><html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Alarm Clock</title>
<style>
body{font-family:Arial;background:#f4f6f8;padding:18px;}
.card{max-width:460px;margin:auto;background:#fff;border-radius:14px;
box-shadow:0 6px 18px rgba(0,0,0,.08);padding:16px;}
h2{margin:6px 0 10px;}
.row{margin:10px 0;}
input{width:80px;padding:10px;font-size:16px;border:1px solid #ddd;border-radius:10px;}
button{padding:10px 14px;font-size:16px;border:0;border-radius:10px;cursor:pointer;}
.btn{background:#1976d2;color:#fff;}
.btn2{background:#e53935;color:#fff;text-decoration:none;padding:10px 14px;border-radius:10px;display:inline-block;}
small{color:#555;}
.badge{display:inline-block;background:#eef2ff;border:1px solid #dbe2ff;
padding:6px 10px;border-radius:999px;font-size:13px;}
</style>
</head>
<body>
<div class="card">
  <h2>ESP32 IoT Alarm Clock</h2>
  <div class="row"><span class="badge">Now: )" + nowStr + R"(</span></div>
  <div class="row"><small>Device IP: )" + ipStr + R"(</small></div>

  <form class="row" action="/set" method="GET">
    <div class="row">
      <label>Alarm Hour (0-23):</label><br>
      <input name="hh" type="number" min="0" max="23" required>
    </div>
    <div class="row">
      <label>Alarm Min (0-59):</label><br>
      <input name="mm" type="number" min="0" max="59" required>
    </div>
    <div class="row">
      <label><input type="checkbox" name="en" value="1"> Enable Alarm</label>
    </div>
    <button class="btn" type="submit">Save Alarm</button>
  </form>

  <div class="row">
    <a class="btn2" href="/stop">Stop Ringing</a>
  </div>

  <div class="row"><small>Tip: Button on GPIO27 also stops + snoozes 5 minutes.</small></div>
</div>
</body></html>
)";
  return html;
}

void handleRoot() {
  tm tminfo;
  String nowStr = "N/A";
  if (getLocalTime(&tminfo)) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tminfo);
    nowStr = String(buf);
  }
  String ipStr = (WiFi.isConnected()) ? ipToString(WiFi.localIP()) : "Not connected";
  server.send(200, "text/html", makeHtmlPage(nowStr, ipStr));
}

void handleSet() {
  if (!server.hasArg("hh") || !server.hasArg("mm")) {
    server.send(400, "text/plain", "Missing hh/mm");
    return;
  }

  int hh = server.arg("hh").toInt();
  int mm = server.arg("mm").toInt();

  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    server.send(400, "text/plain", "Invalid time values");
    return;
  }

  alarmHour = hh;
  alarmMin  = mm;
  alarmEnabled = server.hasArg("en"); // checkbox present -> true

  String msg = "Saved Alarm: " + twoDigits(alarmHour) + ":" + twoDigits(alarmMin) +
               (alarmEnabled ? " (ENABLED)" : " (DISABLED)");
  server.send(200, "text/plain", msg);
}

void handleStop() {
  ringing = false;
  buzzerOff();
  server.send(200, "text/plain", "Alarm stopped.");
}

// ---------- DISPLAY ----------
void drawClockScreen(const tm &tminfo, const String& ipStr) {
  static int lastSec = -1;
  if (tminfo.tm_sec == lastSec) return;
  lastSec = tminfo.tm_sec;

  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &tminfo);

  char dateStr[24];
  strftime(dateStr, sizeof(dateStr), "%d-%b-%Y", &tminfo);

  tft.fillScreen(ILI9341_BLACK);

  // Title
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("IoT Alarm Clock");

  // Time
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(4);
  tft.setCursor(10, 50);
  tft.print(timeStr);

  // Date
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 105);
  tft.print(dateStr);

  // Alarm line
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(10, 140);
  tft.print("Alarm: ");
  tft.print(twoDigits(alarmHour));
  tft.print(":");
  tft.print(twoDigits(alarmMin));
  tft.print("  ");
  tft.print(alarmEnabled ? "ON" : "OFF");

  // IP
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 170);
  tft.print("IP: ");
  tft.print(ipStr);

  // Status
  if (ringing) {
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(3);
    tft.setCursor(10, 205);
    tft.print("ALARM RINGING!");
  } else {
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 210);
    tft.print("Btn: Stop+Snooze");
  }
}

// ---------- ALARM LOGIC ----------
void applySnooze() {
  int total = alarmHour * 60 + alarmMin + SNOOZE_MINUTES;
  total %= (24 * 60);
  alarmHour = total / 60;
  alarmMin  = total % 60;
}

void checkAlarm(const tm &tminfo) {
  if (!alarmEnabled) return;

  // Trigger exactly at HH:MM:00
  if (!ringing &&
      tminfo.tm_hour == alarmHour &&
      tminfo.tm_min  == alarmMin &&
      tminfo.tm_sec  == 0) {
    ringing = true;
    ringStartMs = millis();
  }

  if (ringing) {
    // Beep pattern: 300ms ON / 300ms OFF
    unsigned long phase = (millis() / 300) % 2;
    if (phase == 0) buzzerOn(); else buzzerOff();

    // Button stops + snoozes
    if (digitalRead(BTN_STOP) == LOW) {
      ringing = false;
      buzzerOff();
      applySnooze();
      delay(250); // basic debounce
      return;
    }

    // Auto stop after duration
    if (millis() - ringStartMs > RING_DURATION_MS) {
      ringing = false;
      buzzerOff();
    }
  }
}

// ---------- WIFI CONNECT ----------
bool connectWiFiWithTimeout(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  return (WiFi.status() == WL_CONNECTED);
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  pinMode(BTN_STOP, INPUT_PULLUP);

  Serial.begin(115200);
  delay(300);

  // TFT init
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 40);
  tft.print("Starting...");

  // WiFi
  tft.setCursor(10, 80);
  tft.print("Connecting WiFi...");
  bool ok = connectWiFiWithTimeout(15000);

  // NTP Time
  if (ok) {
    tft.setCursor(10, 110);
    tft.print("WiFi OK. NTP sync...");
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  } else {
    tft.setCursor(10, 110);
    tft.print("WiFi failed.");
  }

  // Web server
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/stop", handleStop);
  server.begin();

  Serial.print("WiFi: ");
  Serial.println(ok ? "Connected" : "Not connected");
  if (ok) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
}

void loop() {
  server.handleClient();

  tm tminfo;
  String ipStr = WiFi.isConnected() ? ipToString(WiFi.localIP()) : "No WiFi";

  if (getLocalTime(&tminfo)) {
    drawClockScreen(tminfo, ipStr);
    checkAlarm(tminfo);
  } else {
    // If time not available yet, show message
    static unsigned long lastMsg = 0;
    if (millis() - lastMsg > 1000) {
      lastMsg = millis();
      tft.fillScreen(ILI9341_BLACK);
      tft.setTextColor(ILI9341_WHITE);
      tft.setTextSize(2);
      tft.setCursor(10, 60);
      tft.print("Waiting for NTP...");
      tft.setCursor(10, 95);
      tft.print("Check WiFi / SSID");
      tft.setCursor(10, 130);
      tft.print("IP: ");
      tft.print(ipStr);
    }
  }

  delay(20);
}
