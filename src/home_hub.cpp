// CYD Home Hub — TPM408-2.8 / ESP32-2432S028R
// Display and touch configuration follows a tested TPM408 CYD reference.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

namespace Board {
constexpr uint16_t WIDTH = 320, HEIGHT = 240;
constexpr uint16_t LIGHT_BLUE = 0x9E7F;
constexpr uint8_t TOUCH_DOUT = 39, TOUCH_DIN = 32, TOUCH_CS = 33, TOUCH_CLK = 25;
constexpr uint8_t IR_PIN = 27;
constexpr uint32_t SLEEP_MS = 10000;
}

TFT_eSPI screen;
TFT_Touch touch(Board::TOUCH_CS, Board::TOUCH_CLK, Board::TOUCH_DIN, Board::TOUCH_DOUT);
Preferences store;
IRsend irsend(Board::IR_PIN);

enum class AppPage : uint8_t { Home, Devices, WiFi, Bluetooth, Infrared, System };
struct Device { String id, name, domain, state; };

AppPage page = AppPage::Home;
Device devices[16];
uint8_t deviceCount = 0, deviceOffset = 0;
String haUrl, haToken, irCodeText = "20DF10EF";
String notice = "Ready";
String bleNames[4];
uint8_t bleCount = 0;
bool displayAsleep = false;
uint32_t lastInteraction = 0;

// ----- Drawing: white and light-blue only ------------------------------------
void setInk(uint16_t foreground = Board::LIGHT_BLUE, uint16_t background = TFT_WHITE) {
  screen.setTextColor(foreground, background);
}
void header(const String& title, const String& right = "") {
  screen.fillScreen(TFT_WHITE);
  screen.fillRect(0, 0, Board::WIDTH, 38, Board::LIGHT_BLUE);
  screen.setTextColor(TFT_WHITE, Board::LIGHT_BLUE);
  screen.setTextSize(2);
  screen.drawString(title, 12, 11, 2);
  if (right.length()) { screen.setTextSize(1); screen.drawRightString(right, 309, 14, 1); }
}
void button(int x, int y, int w, int h, const String& name, const String& detail = "") {
  screen.drawRoundRect(x, y, w, h, 8, Board::LIGHT_BLUE);
  setInk(); screen.setTextSize(2); screen.drawCentreString(name, x + w / 2, y + 8, 2);
  if (detail.length()) { screen.setTextSize(1); screen.drawCentreString(detail, x + w / 2, y + h - 16, 1); }
}
void splash(const String& line) { header("CYD HOME"); setInk();screen.setTextSize(2);screen.drawCentreString(line,160,112,2); }

// ----- Settings ----------------------------------------------------------------
void loadSettings() {
  store.begin("cydhub", false);
  haUrl = store.getString("ha_url", "");
  haToken = store.getString("ha_token", "");
  irCodeText = store.getString("ir_nec", "20DF10EF");
}
void saveSettings() {
  store.putString("ha_url", haUrl);
  store.putString("ha_token", haToken);
  store.putString("ir_nec", irCodeText);
}

// ----- Home Assistant ----------------------------------------------------------
bool configured() { return WiFi.isConnected() && haUrl.length() && haToken.length(); }
String apiBase() { String base = haUrl; while (base.endsWith("/")) base.remove(base.length() - 1); return base + "/api"; }
bool fetchDevices() {
  deviceCount = 0; deviceOffset = 0;
  if (!configured()) { notice = "Set Wi-Fi and Home Assistant first"; return false; }
  splash("Loading devices...");
  HTTPClient http;
  http.setTimeout(9000);
  http.begin(apiBase() + "/states");
  http.addHeader("Authorization", "Bearer " + haToken);
  int response = http.GET();
  if (response != HTTP_CODE_OK) { notice = "Home Assistant error " + String(response); http.end(); return false; }
  JsonDocument filter;
  filter[0]["entity_id"] = true;
  filter[0]["state"] = true;
  filter[0]["attributes"]["friendly_name"] = true;
  JsonDocument data;
  DeserializationError error = deserializeJson(data, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) { notice = "Could not read device list"; return false; }
  for (JsonObject item : data.as<JsonArray>()) {
    String id = item["entity_id"] | "";
    int dot = id.indexOf('.'); if (dot <= 0) continue;
    String domain = id.substring(0, dot);
    if (!(domain == "switch" || domain == "light" || domain == "fan" || domain == "cover")) continue;
    if (deviceCount == 16) break;
    Device& d = devices[deviceCount++];
    d.id = id; d.domain = domain; d.name = item["attributes"]["friendly_name"] | id; d.state = item["state"] | "unknown";
  }
  notice = deviceCount ? String(deviceCount) + " devices loaded" : "No supported devices found";
  return true;
}
bool sendDeviceCommand(Device& d) {
  if (!configured()) return false;
  bool currentlyOn = d.state == "on" || d.state == "open";
  String service;
  if (d.domain == "cover") service = currentlyOn ? "close_cover" : "open_cover";
  else service = currentlyOn ? "turn_off" : "turn_on";
  HTTPClient http;
  http.setTimeout(9000);
  http.begin(apiBase() + "/services/" + d.domain + "/" + service);
  http.addHeader("Authorization", "Bearer " + haToken);
  http.addHeader("Content-Type", "application/json");
  int response = http.POST("{\"entity_id\":\"" + d.id + "\"}");
  http.end();
  if (response >= 200 && response < 300) { d.state = currentlyOn ? (d.domain == "cover" ? "closed" : "off") : (d.domain == "cover" ? "open" : "on"); notice = d.name + " updated"; return true; }
  notice = "Control failed: " + String(response); return false;
}

// ----- Wi-Fi -------------------------------------------------------------------
void openSetupPortal() {
  WiFiManager manager;
  WiFiManagerParameter urlParam("ha_url", "Home Assistant URL", haUrl.c_str(), 120);
  WiFiManagerParameter tokenParam("ha_token", "Home Assistant token", haToken.c_str(), 255);
  WiFiManagerParameter irParam("ir_nec", "IR NEC code (hex)", irCodeText.c_str(), 16);
  manager.addParameter(&urlParam); manager.addParameter(&tokenParam); manager.addParameter(&irParam);
  manager.setConfigPortalTimeout(180);
  header("WI-FI SETUP"); setInk();
  screen.drawCentreString("Join CYD-Home", 160, 96, 2);
  screen.setTextSize(1); screen.drawCentreString("Then open 192.168.4.1", 160, 132, 1);
  manager.startConfigPortal("CYD-Home");
  haUrl = urlParam.getValue(); haToken = tokenParam.getValue(); irCodeText = irParam.getValue(); saveSettings();
  notice = WiFi.isConnected() ? "Setup saved" : "Setup cancelled";
}

// ----- Bluetooth ----------------------------------------------------------------
class ScannerCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* found) override {
    if (bleCount >= 4) return;
    String name = found->getName().c_str();
    if (!name.length()) name = found->getAddress().toString().c_str();
    bleNames[bleCount++] = name;
  }
} scannerCallbacks;
void scanBluetooth() {
  bleCount = 0; splash("Scanning Bluetooth...");
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->clearResults(); scanner->setActiveScan(true); scanner->setMaxResults(0);
  scanner->start(5, false, true);
  notice = bleCount ? String(bleCount) + " nearby devices" : "No BLE devices found";
}

// ----- Infrared -----------------------------------------------------------------
void sendInfrared() {
  char* end = nullptr;
  uint32_t code = strtoul(irCodeText.c_str(), &end, 16);
  if (!irCodeText.length() || (end && *end)) { notice = "Invalid IR hexadecimal code"; return; }
  irsend.sendNEC(code, 32);
  notice = "IR code sent";
}

// ----- Pages --------------------------------------------------------------------
void drawHome() {
  header("CYD HOME", WiFi.isConnected() ? "Wi-Fi" : "Offline");
  button(12, 52, 140, 52, "Devices", "Sonoff / eWeLink");
  button(168, 52, 140, 52, "Wi-Fi", "Setup & status");
  button(12, 116, 140, 52, "Bluetooth", "BLE discovery");
  button(168, 116, 140, 52, "IR Remote", "NEC transmitter");
  button(12, 180, 296, 34, "System", "Status and settings");
}
void drawDevices() {
  header("DEVICES", String(deviceCount) + " found");
  if (!deviceCount) { button(12, 65, 296, 52, "Refresh devices", notice); button(12, 180, 140, 34, "Back"); return; }
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t index = deviceOffset + i; if (index >= deviceCount) break;
    Device& d = devices[index];
    button(12, 47 + i * 42, 296, 38, d.name, d.state == "on" || d.state == "open" ? "ON — tap to turn off" : "OFF — tap to turn on");
  }
  button(12, 216, 66, 20, "Back"); button(88, 216, 70, 20, "Refresh");
  if (deviceOffset + 4 < deviceCount) button(168, 216, 140, 20, "Next page");
}
void drawWiFi() { header("WI-FI SETUP"); button(12, 60, 296, 52, WiFi.isConnected() ? WiFi.SSID() : "Not connected", WiFi.isConnected() ? WiFi.localIP().toString() : "Tap setup to connect");button(12, 124, 296, 52, "Setup portal", "Wi-Fi, Home Assistant, and IR code");button(12, 190, 140, 28, "Back"); }
void drawBluetooth() { header("BLUETOOTH");button(12, 50, 296, 38, "Scan nearby devices", "Five-second BLE scan");if (!bleCount) button(12, 98, 296, 42, "No results", "Discovery only — no pairing");for (uint8_t i=0;i<bleCount;i++)button(12,98+i*28,296,26,bleNames[i],"BLE device");button(12, 204, 140, 28, "Back"); }
void drawInfrared() { header("IR REMOTE");button(12, 62, 296, 52, "Send NEC code", "0x" + irCodeText);button(12, 126, 296, 48, "Hardware required", "IR LED + transistor on GPIO 27");button(12, 190, 140, 28, "Back"); }
void drawSystem() { header("SYSTEM");button(12, 55, 296, 40, "Display sleep", "10 seconds; any touch wakes it");button(12, 105, 296, 40, "Home Assistant", haUrl.length() ? "Configured" : "Not configured");button(12, 155, 296, 40, "Last action", notice);button(12, 204, 140, 28, "Back"); }
void draw() { switch(page) { case AppPage::Home:drawHome();break;case AppPage::Devices:drawDevices();break;case AppPage::WiFi:drawWiFi();break;case AppPage::Bluetooth:drawBluetooth();break;case AppPage::Infrared:drawInfrared();break;case AppPage::System:drawSystem();break; } }

// ----- Touch, sleep and routing -------------------------------------------------
void wakeDisplay() { digitalWrite(TFT_BL, HIGH); displayAsleep = false; lastInteraction = millis(); draw(); }
void handleTouch() {
  if (!touch.Pressed()) return;
  if (displayAsleep) { wakeDisplay(); delay(250); return; }
  if (millis() - lastInteraction < 260) return;
  lastInteraction = millis();
  int x = touch.X(), y = touch.Y();
  if (x < 0 || x >= Board::WIDTH || y < 0 || y >= Board::HEIGHT) return;
  if (page == AppPage::Home) {
    if (y < 110) {
      page = x < 160 ? AppPage::Devices : AppPage::WiFi;
      if (page == AppPage::Devices) fetchDevices();
    }
    else if (y < 176) page = x < 160 ? AppPage::Bluetooth : AppPage::Infrared;
    else page = AppPage::System;
  } else if (page == AppPage::Devices) {
    if (!deviceCount) { if (y < 145) fetchDevices(); else if (y > 170) page = AppPage::Home; }
    else if (y > 214 && x < 78) page = AppPage::Home;
    else if (y > 214 && x < 158) fetchDevices();
    else if (y > 214 && deviceOffset + 4 < deviceCount) deviceOffset += 4;
    else if (y >= 47 && y < 215) { uint8_t row = (y - 47) / 42; if (deviceOffset + row < deviceCount) sendDeviceCommand(devices[deviceOffset + row]); }
  } else if (page == AppPage::WiFi) { if (y < 185 && y > 116) openSetupPortal(); else if (y > 185) page = AppPage::Home; }
  else if (page == AppPage::Bluetooth) { if (y < 92) scanBluetooth(); else if (y > 196) page = AppPage::Home; }
  else if (page == AppPage::Infrared) { if (y < 120) sendInfrared(); else if (y > 185) page = AppPage::Home; }
  else if (page == AppPage::System && y > 196) page = AppPage::Home;
  draw();
}

void setup() {
  Serial.begin(115200);
  screen.init(); screen.setRotation(1); screen.setSwapBytes(true);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  touch.setCal(526, 3443, 750, 3377, Board::WIDTH, Board::HEIGHT, 1);
  loadSettings(); WiFi.mode(WIFI_STA); WiFi.begin();
  NimBLEDevice::init("CYD Home"); NimBLEDevice::getScan()->setScanCallbacks(&scannerCallbacks, false);
  irsend.begin(); lastInteraction = millis(); draw();
}
void loop() {
  handleTouch();
  if (!displayAsleep && millis() - lastInteraction >= Board::SLEEP_MS) { digitalWrite(TFT_BL, LOW); displayAsleep = true; }
  delay(15);
}
