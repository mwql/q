#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "Config.h"

TFT_eSPI tft;
Preferences prefs;
AppConfig config;
constexpr uint16_t APP_BLUE = 0x0010;
enum class Screen { HOME, DEVICES, WIFI, BLE, IR, SETTINGS, DETAIL };
Screen screen = Screen::HOME;
String statusLine = "Ready";
String selectedEntity;
unsigned long lastTouch = 0;
// Typical CYD-2432S028 portrait touch calibration. Use TFT_eSPI's calibration
// example if a clone has a different touch controller orientation.
uint16_t touchCalData[5] = { 275, 3620, 275, 3520, 7 };

void loadConfig() {
  prefs.begin("cydhome", false);
  config.haUrl = prefs.getString("ha_url", "");
  config.haToken = prefs.getString("ha_token", "");
  config.irName = prefs.getString("ir_name", "TV power");
  config.irCode = prefs.getUInt("ir_code", 0x20DF10EF);
}
void saveConfig() {
  prefs.putString("ha_url", config.haUrl);
  prefs.putString("ha_token", config.haToken);
  prefs.putString("ir_name", config.irName);
  prefs.putUInt("ir_code", config.irCode);
}
void header(const char *title) {
  tft.fillScreen(TFT_BLACK); tft.fillRect(0,0,240,34,TFT_NAVY);
  tft.setTextDatum(TL_DATUM); tft.setTextColor(TFT_WHITE, TFT_NAVY); tft.setTextSize(2); tft.drawString(title, 10, 9);
  tft.setTextSize(1); tft.setTextColor(WiFi.isConnected()?TFT_GREEN:TFT_ORANGE,TFT_NAVY);
  tft.drawString(WiFi.isConnected()?"WiFi":"OFFLINE",185,12);
}
void card(int y, const String &label, const String &sub, uint16_t color=TFT_DARKGREY) {
  tft.fillRoundRect(10,y,220,48,7,color); tft.setTextColor(TFT_WHITE,color); tft.setTextSize(2); tft.drawString(label,20,y+8);
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY,color); tft.drawString(sub,20,y+31);
}
void drawHome() {
  header("CYD Home");
  card(46,"Devices","Sonoff / eWeLink via Home Assistant",TFT_DARKCYAN);
  card(102,"Wi-Fi","Connect or change network",TFT_DARKGREEN);
  card(158,"Bluetooth","Scan nearby BLE devices",APP_BLUE);
  card(214,"IR Remote","Send learned infrared codes",TFT_MAROON);
  card(270,"Settings","Home Assistant connection",TFT_DARKGREY);
}

bool apiGet(const String &path, JsonDocument &doc) {
  if (!WiFi.isConnected() || config.haUrl.length()==0 || config.haToken.length()==0) { statusLine="Set up Home Assistant first"; return false; }
  HTTPClient http; http.begin(config.haUrl + path); http.addHeader("Authorization", "Bearer " + config.haToken); http.addHeader("Content-Type","application/json");
  int code=http.GET(); if (code != 200) { statusLine="Home Assistant error " + String(code); http.end(); return false; }
  DeserializationError e=deserializeJson(doc,http.getString()); http.end(); return !e;
}
bool callService(const String &entity, bool on) {
  HTTPClient http; http.begin(config.haUrl + "/api/services/homeassistant/" + (on?"turn_on":"turn_off"));
  http.addHeader("Authorization","Bearer "+config.haToken); http.addHeader("Content-Type","application/json");
  String body="{\"entity_id\":\""+entity+"\"}"; int code=http.POST(body); http.end();
  statusLine = code==200 ? (on?"Turned on":"Turned off") : "Action failed: "+String(code); return code==200;
}
void drawDevices() {
  header("My Devices");
  DynamicJsonDocument doc(12288);
  if (!apiGet("/api/states",doc)) { card(55,"Not connected",statusLine,TFT_MAROON); card(115,"Open Settings","Connect Home Assistant",TFT_DARKGREY); return; }
  int y=42, shown=0;
  for (JsonObject state: doc.as<JsonArray>()) {
    String id=state["entity_id"]|""; if (!(id.startsWith("switch.")||id.startsWith("light.")||id.startsWith("fan.")||id.startsWith("cover."))) continue;
    String name=state["attributes"]["friendly_name"]|id; String value=state["state"]|"unknown";
    card(y,name,value=="on"?"ON  • tap to toggle":"OFF • tap to toggle",value=="on"?TFT_DARKGREEN:TFT_DARKGREY);
    y+=54; if (++shown==5) break;
  }
  if (!shown) card(60,"No devices found","Expose Sonoff entities in Home Assistant",TFT_MAROON);
}
void drawWifi() { header("Wi-Fi"); card(55,WiFi.isConnected()?WiFi.SSID():"Not connected",WiFi.isConnected()?WiFi.localIP().toString():"Tap to open setup portal",TFT_DARKGREEN); card(115,"Connect / change Wi-Fi","Opens temporary CYD-Home portal",TFT_DARKCYAN); card(175,"Forget Wi-Fi","Tap again to confirm",TFT_MAROON); }
void drawBle() { header("Bluetooth"); card(48,"Bluetooth","Bluetooth radio reserved",APP_BLUE); card(106,"Status","No pairing or device control enabled",TFT_DARKGREY); card(164,"Privacy","This app never scans or connects",TFT_DARKGREY); }
void drawIr() { header("IR Remote"); card(55,config.irName,"Tap to transmit configured NEC code",TFT_MAROON); card(115,"IR transmitter needed","Wire IR LED + transistor to GPIO 27",TFT_DARKGREY); card(175,"Code",String("0x")+String(config.irCode,HEX),TFT_DARKGREY); }
void drawSettings() { header("Settings"); card(52,"Home Assistant bridge",config.haUrl.length()?config.haUrl:"Not configured",TFT_DARKCYAN); card(112,"Setup help","Use USB serial commands below",TFT_DARKGREY); card(172,"Security", "Token stored only on this CYD",TFT_DARKGREEN); card(232,"Back",statusLine,TFT_DARKGREY); }
void draw() { switch(screen) { case Screen::HOME:drawHome();break; case Screen::DEVICES:drawDevices();break;case Screen::WIFI:drawWifi();break;case Screen::BLE:drawBle();break;case Screen::IR:drawIr();break;case Screen::SETTINGS:drawSettings();break;default:drawHome(); } }
void startPortal() {
  WiFiManager wm;
  WiFiManagerParameter haUrlParam("ha_url", "Home Assistant URL", config.haUrl.c_str(), 128);
  WiFiManagerParameter tokenParam("ha_token", "Home Assistant access token", config.haToken.c_str(), 256);
  wm.addParameter(&haUrlParam); wm.addParameter(&tokenParam); wm.setConfigPortalTimeout(180);
  statusLine="Wi-Fi setup active"; tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.drawString("Connect to CYD-Home",18,110); tft.setTextSize(1); tft.drawString("then open 192.168.4.1",35,145);
  wm.startConfigPortal("CYD-Home");
  config.haUrl=haUrlParam.getValue(); config.haToken=tokenParam.getValue(); saveConfig();
  statusLine=WiFi.isConnected()?"Wi-Fi and bridge saved":"Wi-Fi setup cancelled";
}
void sendNec(uint32_t code) { // 38 kHz NEC sender; use transistor-driven IR LED, not the GPIO directly.
  ledcSetup(0,38000,8); ledcAttachPin(IR_PIN,0); auto mark=[](int us){ledcWrite(0,85);delayMicroseconds(us);}; auto space=[](int us){ledcWrite(0,0);delayMicroseconds(us);};
  mark(9000);space(4500); for(int i=0;i<32;i++){mark(560);space((code>>i)&1?1690:560);}mark(560);ledcWrite(0,0); statusLine="IR sent";
}
void processSerial() { if (!Serial.available()) return; String s=Serial.readStringUntil('\n'); s.trim();
  if(s.startsWith("HA_URL ")) {config.haUrl=s.substring(7);saveConfig();statusLine="HA URL saved";}
  else if(s.startsWith("HA_TOKEN ")) {config.haToken=s.substring(9);saveConfig();statusLine="HA token saved";}
  else if(s=="HELP") Serial.println("Commands: HA_URL http://homeassistant.local:8123 | HA_TOKEN <long-lived-token>");
}
void handleTouch() { uint16_t x,y; if(!tft.getTouch(&x,&y) || millis()-lastTouch<300) return; lastTouch=millis();
  if(screen==Screen::HOME) { if(y<100)screen=Screen::DEVICES;else if(y<156)screen=Screen::WIFI;else if(y<212)screen=Screen::BLE;else if(y<268)screen=Screen::IR;else screen=Screen::SETTINGS; }
  else if(screen==Screen::WIFI) { if(y>95&&y<170)startPortal(); else if(y>170){WiFi.disconnect(true,true);statusLine="Wi-Fi forgotten";} }
  else if(screen==Screen::BLE&&y<110) { statusLine="Bluetooth is safely disabled"; }
  else if(screen==Screen::IR&&y<115) sendNec(config.irCode);
  else if(screen==Screen::DEVICES) { DynamicJsonDocument doc(12288);if(apiGet("/api/states",doc)){int row=(y-42)/54,seen=0;for(JsonObject st:doc.as<JsonArray>()){String id=st["entity_id"]|"";if(id.startsWith("switch.")||id.startsWith("light.")||id.startsWith("fan.")||id.startsWith("cover.")){if(seen++==row){callService(id,String(st["state"]|"")!="on");break;}}}} }
  else screen=Screen::HOME; draw();
}
void setup() {
  Serial.begin(115200);
  // Failsafe: enable the CYD-2432S028 backlight before any other subsystem.
  pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH); delay(80);
  tft.init(); tft.setRotation(0); tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.setTextSize(2); tft.drawCentreString("CYD Home starting", 120, 140, 2);
  tft.setTouch(touchCalData); loadConfig(); WiFi.mode(WIFI_STA); WiFi.begin();
  draw();
}
void loop() { processSerial();handleTouch();delay(15); }
