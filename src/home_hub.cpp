// CYD Home Hub 2.0
// A touch-first controller for the TPM408-2.8 / ESP32-2432S028R.
// The CYD contains no cloud account password: a local Web Bridge owns the
// Home Assistant/eWeLink connection and exposes only device data and commands.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <time.h>
#include <cstring>
#include "board_config.h"
#include "theme.h"

namespace {

enum class Page : uint8_t { Home, Devices, Wifi, Keyboard, Bluetooth, Infrared, Settings, Bridge };
enum class KeyboardTarget : uint8_t { None, WifiPassword, BridgeUrl, BridgeKey, IrCode };
enum class KeyboardMode : uint8_t { Lower, Upper, Symbols };

struct Device {
  String id;
  String name;
  String type;
  String state;
};

TFT_eSPI tft;
TFT_Touch touch(Board::kTouchCs, Board::kTouchClock, Board::kTouchDin, Board::kTouchDout);
Preferences preferences;
IRsend irsend(Board::kIrPin);

Page page = Page::Home;
KeyboardTarget keyboardTarget = KeyboardTarget::None;
KeyboardMode keyboardMode = KeyboardMode::Lower;
bool touchDown = false;
bool displaySleeping = false;
bool wifiConnecting = false;
bool keyboardSecret = false;
uint8_t themeIndex = 0;
uint8_t sleepIndex = 0;
int8_t utcOffset = 0;
uint8_t wifiOffset = 0;
uint8_t deviceOffset = 0;
uint8_t deviceCount = 0;
uint8_t bleCount = 0;
uint32_t lastInteraction = 0;
uint32_t wifiConnectStarted = 0;
bool ntpStarted = false;

String notice = "Ready";
String selectedSsid;
String keyboardTitle;
String keyboardValue;
String bridgeUrl;
String bridgeKey;
String irCode = "20DF10EF";
String wifiNames[12];
int32_t wifiRssi[12];
uint8_t wifiCount = 0;
String bleNames[4];
Device devices[20];

constexpr uint32_t kSleepChoices[] = {10000, 30000, 60000, 0};
constexpr char kLowerRows[][11] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
constexpr char kUpperRows[][11] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
constexpr char kSymbolRows[][11] = {"1234567890", "-_.:/@#", "!?$%&*+="};

const Theme& theme() { return kThemes[themeIndex % kThemeCount]; }
uint32_t sleepTimeout() { return kSleepChoices[sleepIndex % 4]; }

// ----- Settings persistence ------------------------------------------------------
void saveSettings() {
  preferences.putUChar("theme", themeIndex);
  preferences.putUChar("sleep", sleepIndex);
  preferences.putChar("utc_offset", utcOffset);
  preferences.putString("bridge_url", bridgeUrl);
  preferences.putString("bridge_key", bridgeKey);
  preferences.putString("ir_code", irCode);
}

void loadSettings() {
  preferences.begin("cydhub2", false);
  themeIndex = preferences.getUChar("theme", 0);
  sleepIndex = preferences.getUChar("sleep", 0);
  utcOffset = preferences.getChar("utc_offset", 0);
  bridgeUrl = preferences.getString("bridge_url", "");
  bridgeKey = preferences.getString("bridge_key", "");
  irCode = preferences.getString("ir_code", "20DF10EF");
  if (themeIndex >= kThemeCount) themeIndex = 0;
  if (sleepIndex > 3) sleepIndex = 0;
  if (utcOffset < -12 || utcOffset > 14) utcOffset = 0;
}

// ----- UI primitives -------------------------------------------------------------
String trimText(const String& value, size_t limit) {
  if (value.length() <= limit) return value;
  return value.substring(0, limit > 3 ? limit - 3 : limit) + "...";
}

void header(const String& title, const String& status = "") {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  tft.fillRect(0, 0, Board::kWidth, 40, c.header);
  tft.setTextColor(c.text, c.header);
  tft.setTextSize(2);
  tft.drawString(title, 12, 12, 2);
  if (status.length()) {
    tft.setTextSize(1);
    tft.drawRightString(status, 308, 15, 1);
  }
}

void card(int x, int y, int w, int h, const String& title, const String& detail = "", bool selected = false) {
  const Theme& c = theme();
  uint16_t fill = selected ? c.surfaceRaised : c.surface;
  tft.fillRoundRect(x, y, w, h, 8, fill);
  tft.drawRoundRect(x, y, w, h, 8, c.accent);
  tft.setTextColor(c.text, fill);
  if (h >= 44) {
    tft.setTextSize(2);
    tft.drawCentreString(trimText(title, 19), x + w / 2, y + 8, 2);
    if (detail.length()) { tft.setTextColor(c.muted, fill); tft.setTextSize(1); tft.drawCentreString(trimText(detail, 43), x + w / 2, y + h - 15, 1); }
  } else {
    tft.setTextSize(1);
    tft.drawString(trimText(title, 19), x + 8, y + (h - 8) / 2, 1);
    if (detail.length()) { tft.setTextColor(c.muted, fill); tft.drawRightString(trimText(detail, 26), x + w - 8, y + (h - 8) / 2, 1); }
  }
}

void toast(const String& message) {
  const Theme& c = theme();
  tft.fillRoundRect(12, 204, 296, 25, 6, c.surfaceRaised);
  tft.drawRoundRect(12, 204, 296, 25, 6, c.accent);
  tft.setTextColor(c.text, c.surfaceRaised);
  tft.setTextSize(1);
  tft.drawCentreString(trimText(message, 47), 160, 212, 1);
}

void loading(const String& title) {
  header("CYD HOME");
  const Theme& c = theme();
  tft.setTextColor(c.text, c.background);
  tft.setTextSize(2);
  tft.drawCentreString(title, 160, 104, 2);
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("Please wait", 160, 140, 1);
}

String wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
  if (wifiConnecting) return "Connecting...";
  return "Wi-Fi offline";
}

void startClock() {
  if (ntpStarted || WiFi.status() != WL_CONNECTED) return;
  configTime(utcOffset * 3600, 0, "pool.ntp.org", "time.nist.gov");
  ntpStarted = true;
}

String clockText() {
  struct tm localTime;
  if (!getLocalTime(&localTime, 5)) return "";
  char value[6];
  strftime(value, sizeof(value), "%H:%M", &localTime);
  return value;
}

// ----- Web Bridge client ---------------------------------------------------------
String bridgeBase() {
  String base = bridgeUrl;
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

bool bridgeReady() { return WiFi.status() == WL_CONNECTED && bridgeUrl.length() > 7; }

void addBridgeHeaders(HTTPClient& request) {
  request.addHeader("Accept", "application/json");
  if (bridgeKey.length()) request.addHeader("X-CYD-Key", bridgeKey);
}

bool fetchDevices() {
  deviceCount = 0;
  deviceOffset = 0;
  if (!bridgeReady()) { notice = "Set Wi-Fi and Web Bridge first"; return false; }
  loading("Getting devices...");
  HTTPClient request;
  request.setTimeout(9000);
  request.begin(bridgeBase() + "/api/v1/devices");
  addBridgeHeaders(request);
  int status = request.GET();
  if (status != HTTP_CODE_OK) { notice = "Web Bridge error " + String(status); request.end(); return false; }
  JsonDocument document;
  DeserializationError error = deserializeJson(document, request.getStream());
  request.end();
  if (error) { notice = "Invalid Web Bridge response"; return false; }
  for (JsonObject item : document["devices"].as<JsonArray>()) {
    if (deviceCount >= 20) break;
    Device& device = devices[deviceCount++];
    device.id = item["id"] | "";
    device.name = item["name"] | device.id;
    device.type = item["type"] | "switch";
    device.state = item["state"] | "unknown";
  }
  notice = deviceCount ? String(deviceCount) + " devices ready" : "No supported devices on the Web Bridge";
  return true;
}

bool toggleDevice(Device& device) {
  if (!bridgeReady()) { notice = "Web Bridge is offline"; return false; }
  HTTPClient request;
  request.setTimeout(9000);
  request.begin(bridgeBase() + "/api/v1/devices/" + device.id + "/toggle");
  addBridgeHeaders(request);
  request.addHeader("Content-Type", "application/json");
  int status = request.POST("{}");
  if (status >= 200 && status < 300) {
    JsonDocument reply;
    deserializeJson(reply, request.getStream());
    device.state = reply["state"] | device.state;
    notice = device.name + " updated";
    request.end();
    return true;
  }
  notice = "Control error " + String(status);
  request.end();
  return false;
}

bool testBridge() {
  if (!bridgeReady()) { notice = "Connect Wi-Fi and set URL"; return false; }
  loading("Testing Web Bridge...");
  HTTPClient request;
  request.setTimeout(7000);
  request.begin(bridgeBase() + "/api/v1/status");
  addBridgeHeaders(request);
  int status = request.GET();
  request.end();
  notice = status == HTTP_CODE_OK ? "Web Bridge connected" : "Web Bridge error " + String(status);
  return status == HTTP_CODE_OK;
}

// ----- Wi-Fi --------------------------------------------------------------------
void scanWifi() {
  loading("Scanning Wi-Fi...");
  wifiCount = 0;
  wifiOffset = 0;
  int count = WiFi.scanNetworks(false, true);
  for (int i = 0; i < count && wifiCount < 12; ++i) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool duplicate = false;
    for (uint8_t known = 0; known < wifiCount; ++known) if (wifiNames[known] == ssid) duplicate = true;
    if (duplicate) continue;
    wifiNames[wifiCount] = ssid;
    wifiRssi[wifiCount] = WiFi.RSSI(i);
    ++wifiCount;
  }
  WiFi.scanDelete();
  notice = wifiCount ? String(wifiCount) + " networks found" : "No Wi-Fi networks found";
}

void connectWifi(const String& password) {
  WiFi.disconnect();
  WiFi.begin(selectedSsid.c_str(), password.c_str());
  wifiConnecting = true;
  wifiConnectStarted = millis();
  notice = "Connecting to " + selectedSsid;
}

void pollWifi() {
  if (!wifiConnecting) return;
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;
    startClock();
    notice = "Connected: " + WiFi.localIP().toString();
  } else if (millis() - wifiConnectStarted > 18000) {
    wifiConnecting = false;
    notice = "Wi-Fi connection timed out";
  }
}

// ----- Bluetooth and infrared ---------------------------------------------------
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertised) override {
    if (bleCount >= 4) return;
    String name = advertised->getName().c_str();
    if (!name.length()) name = advertised->getAddress().toString().c_str();
    bleNames[bleCount++] = name;
  }
} scanCallbacks;

void scanBluetooth() {
  loading("Scanning Bluetooth...");
  bleCount = 0;
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->clearResults();
  scanner->setActiveScan(true);
  scanner->setMaxResults(0);
  scanner->start(5, false, true);
  notice = bleCount ? String(bleCount) + " nearby devices" : "No Bluetooth devices found";
}

void sendInfrared() {
  char* end = nullptr;
  uint32_t code = strtoul(irCode.c_str(), &end, 16);
  if (!irCode.length() || (end && *end)) { notice = "IR code must be hexadecimal"; return; }
  irsend.sendNEC(code, 32);
  notice = "IR code sent";
}

// ----- Keyboard -----------------------------------------------------------------
void startKeyboard(KeyboardTarget target, const String& title, const String& value, bool secret) {
  keyboardTarget = target;
  keyboardTitle = title;
  keyboardValue = value;
  keyboardSecret = secret;
  keyboardMode = KeyboardMode::Lower;
  page = Page::Keyboard;
}

const char* keyboardRow(uint8_t row) {
  if (keyboardMode == KeyboardMode::Upper) return kUpperRows[row];
  if (keyboardMode == KeyboardMode::Symbols) return kSymbolRows[row];
  return kLowerRows[row];
}

String displayKeyboardValue() {
  if (!keyboardSecret) return trimText(keyboardValue, 35);
  String masked;
  for (size_t i = 0; i < keyboardValue.length(); ++i) masked += '*';
  return masked;
}

void drawKeyRow(const char* characters, int y) {
  const Theme& c = theme();
  int count = strlen(characters);
  int width = (300 - (count - 1) * 2) / count;
  int left = 10 + (300 - (width * count + (count - 1) * 2)) / 2;
  for (int i = 0; i < count; ++i) {
    int x = left + i * (width + 2);
    tft.fillRoundRect(x, y, width, 25, 4, c.surfaceRaised);
    tft.drawRoundRect(x, y, width, 25, 4, c.accent);
    tft.setTextColor(c.text, c.surfaceRaised);
    tft.setTextSize(1);
    tft.drawCentreString(String(characters[i]), x + width / 2, y + 8, 1);
  }
}

void drawKeyboard() {
  header(keyboardTitle, keyboardMode == KeyboardMode::Symbols ? "123" : keyboardMode == KeyboardMode::Upper ? "ABC" : "abc");
  const Theme& c = theme();
  tft.fillRoundRect(10, 48, 300, 34, 5, c.surfaceRaised);
  tft.drawRoundRect(10, 48, 300, 34, 5, c.accent);
  tft.setTextColor(c.text, c.surfaceRaised);
  tft.setTextSize(1);
  tft.drawString(displayKeyboardValue(), 18, 61, 1);
  drawKeyRow(keyboardRow(0), 91);
  drawKeyRow(keyboardRow(1), 120);
  drawKeyRow(keyboardRow(2), 149);
  card(10, 184, 48, 35, "aA", "123");
  card(63, 184, 83, 35, "Space");
  card(151, 184, 43, 35, "Del");
  card(199, 184, 51, 35, "Back");
  card(255, 184, 55, 35, keyboardTarget == KeyboardTarget::WifiPassword ? "Join" : "Save");
}

bool appendKeyboardCharFromRow(const char* characters, int tapX, int y) {
  int count = strlen(characters);
  int width = (300 - (count - 1) * 2) / count;
  int left = 10 + (300 - (width * count + (count - 1) * 2)) / 2;
  for (int i = 0; i < count; ++i) {
    int x = left + i * (width + 2);
    if (tapX >= x && tapX <= x + width) { keyboardValue += characters[i]; return true; }
  }
  return false;
}

void finishKeyboard() {
  switch (keyboardTarget) {
    case KeyboardTarget::WifiPassword: connectWifi(keyboardValue); page = Page::Wifi; break;
    case KeyboardTarget::BridgeUrl: bridgeUrl = keyboardValue; saveSettings(); notice = "Bridge URL saved"; page = Page::Bridge; break;
    case KeyboardTarget::BridgeKey: bridgeKey = keyboardValue; saveSettings(); notice = "Bridge key saved"; page = Page::Bridge; break;
    case KeyboardTarget::IrCode: irCode = keyboardValue; saveSettings(); notice = "IR code saved"; page = Page::Infrared; break;
    default: page = Page::Home; break;
  }
  keyboardTarget = KeyboardTarget::None;
}

void handleKeyboardTap(int x, int y) {
  if (y >= 91 && y < 116) appendKeyboardCharFromRow(keyboardRow(0), x, y);
  else if (y >= 120 && y < 145) appendKeyboardCharFromRow(keyboardRow(1), x, y);
  else if (y >= 149 && y < 174) appendKeyboardCharFromRow(keyboardRow(2), x, y);
  else if (y >= 184 && y <= 219) {
    if (x < 58) keyboardMode = static_cast<KeyboardMode>((static_cast<uint8_t>(keyboardMode) + 1) % 3);
    else if (x < 146) keyboardValue += ' ';
    else if (x < 194 && keyboardValue.length()) keyboardValue.remove(keyboardValue.length() - 1);
    else if (x < 250) { page = keyboardTarget == KeyboardTarget::WifiPassword ? Page::Wifi : Page::Bridge; keyboardTarget = KeyboardTarget::None; }
    else finishKeyboard();
  }
}

// ----- Pages --------------------------------------------------------------------
void drawHome() {
  String homeStatus = WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE";
  String time = clockText(); if (time.length()) homeStatus += " " + time;
  header("CYD HOME", homeStatus);
  card(12, 52, 140, 62, "Devices", "Web Bridge controls");
  card(168, 52, 140, 62, "Wi-Fi", wifiStatusText());
  card(12, 124, 140, 62, "Bluetooth", "Discover nearby");
  card(168, 124, 140, 62, "IR Remote", "Send NEC codes");
  card(12, 196, 296, 32, "Settings", "Look, sleep, bridge");
}

void drawDevices() {
  header("DEVICES", bridgeReady() ? String(deviceCount) + " found" : "BRIDGE OFFLINE");
  if (!deviceCount) {
    card(12, 62, 296, 55, "Refresh devices", bridgeReady() ? notice : "Set Wi-Fi and Web Bridge first");
    card(12, 178, 140, 38, "Back");
    return;
  }
  for (uint8_t row = 0; row < 4; ++row) {
    uint8_t index = deviceOffset + row;
    if (index >= deviceCount) break;
    Device& device = devices[index];
    bool on = device.state == "on" || device.state == "open";
    card(12, 48 + row * 39, 296, 35, device.name, on ? "ON  - tap to switch off" : "OFF - tap to switch on", on);
  }
  card(12, 210, 68, 22, "Back");
  card(86, 210, 68, 22, "Refresh");
  if (deviceOffset + 4 < deviceCount) card(160, 210, 148, 22, "Next page");
}

void drawWifi() {
  header("WI-FI", wifiStatusText());
  card(12, 50, 296, 31, "Scan networks", wifiConnecting ? "Connecting..." : "Choose your Wi-Fi");
  if (!wifiCount) { card(12, 96, 296, 46, "No networks listed", notice); }
  for (uint8_t row = 0; row < 4; ++row) {
    uint8_t index = wifiOffset + row;
    if (index >= wifiCount) break;
    card(12, 92 + row * 29, 296, 26, wifiNames[index], String(wifiRssi[index]) + " dBm");
  }
  card(12, 210, 68, 22, "Back");
  if (wifiOffset + 4 < wifiCount) card(160, 210, 148, 22, "More networks");
}

void drawBluetooth() {
  header("BLUETOOTH", "DISCOVERY");
  card(12, 50, 296, 36, "Scan nearby devices", "Five-second scan - no pairing");
  if (!bleCount) card(12, 100, 296, 48, "No results", notice);
  for (uint8_t index = 0; index < bleCount; ++index) card(12, 96 + index * 28, 296, 25, bleNames[index], "BLE device");
  card(12, 210, 68, 22, "Back");
}

void drawInfrared() {
  header("IR REMOTE", "GPIO 27");
  card(12, 58, 296, 54, "Send IR command", "NEC 0x" + irCode);
  card(12, 124, 296, 42, "Change NEC code", "Use hexadecimal digits");
  card(12, 178, 296, 22, "Hardware", "IR LED and transistor required");
  card(12, 210, 68, 22, "Back");
}

String sleepLabel() {
  uint32_t value = sleepTimeout();
  return value ? String(value / 1000) + " seconds" : "Never";
}

void drawSettings() {
  header("SETTINGS", themeIndex == 0 ? "OCEAN" : "MIDNIGHT");
  card(12, 48, 296, 28, "Appearance", themeIndex == 0 ? "Ocean dark - tap to change" : "Midnight dark - tap to change");
  card(12, 80, 296, 28, "Screen sleep", sleepLabel() + " - tap to change");
  String zone = "UTC" + String(utcOffset >= 0 ? "+" : "") + String(utcOffset);
  card(12, 112, 296, 28, "Clock time zone", zone + " - tap to change");
  card(12, 144, 296, 28, "Web Bridge", bridgeUrl.length() ? trimText(bridgeUrl, 34) : "Not configured");
  card(12, 176, 296, 28, "Clear Wi-Fi", "Forget saved Wi-Fi network");
  card(12, 210, 68, 22, "Back");
}

void drawBridge() {
  header("WEB BRIDGE", bridgeReady() ? "READY" : "SETUP");
  card(12, 52, 296, 36, "Bridge URL", bridgeUrl.length() ? trimText(bridgeUrl, 39) : "Tap to enter server address");
  card(12, 94, 296, 36, "Access key", bridgeKey.length() ? "Configured" : "Optional local-network key");
  card(12, 136, 296, 36, "Test connection", "Checks the Web Bridge");
  card(12, 178, 296, 22, "About", "The server stores cloud login, never the CYD");
  card(12, 210, 68, 22, "Back");
}

void drawPage() {
  switch (page) {
    case Page::Home: drawHome(); break;
    case Page::Devices: drawDevices(); break;
    case Page::Wifi: drawWifi(); break;
    case Page::Keyboard: drawKeyboard(); break;
    case Page::Bluetooth: drawBluetooth(); break;
    case Page::Infrared: drawInfrared(); break;
    case Page::Settings: drawSettings(); break;
    case Page::Bridge: drawBridge(); break;
  }
}

// ----- Touch routing and display sleep ------------------------------------------
void wakeDisplay() {
  digitalWrite(TFT_BL, HIGH);
  displaySleeping = false;
  lastInteraction = millis();
  drawPage();
}

void handleTap(int x, int y) {
  lastInteraction = millis();
  if (page == Page::Keyboard) { handleKeyboardTap(x, y); drawPage(); return; }

  if (page == Page::Home) {
    if (y >= 52 && y < 114) page = x < 160 ? Page::Devices : Page::Wifi;
    else if (y >= 124 && y < 186) page = x < 160 ? Page::Bluetooth : Page::Infrared;
    else if (y >= 196) page = Page::Settings;
    if (page == Page::Devices) fetchDevices();
  } else if (page == Page::Devices) {
    if (!deviceCount && y < 145) fetchDevices();
    else if (y >= 210 && x < 80) page = Page::Home;
    else if (y >= 210 && x < 154) fetchDevices();
    else if (y >= 210 && deviceOffset + 4 < deviceCount) deviceOffset += 4;
    else {
      for (uint8_t row = 0; row < 4; ++row) {
        int top = 48 + row * 39;
        if (y >= top && y < top + 35 && deviceOffset + row < deviceCount) { toggleDevice(devices[deviceOffset + row]); break; }
      }
    }
  } else if (page == Page::Wifi) {
    if (y >= 50 && y < 84) scanWifi();
    else if (y >= 92 && y < 208 && wifiCount) { uint8_t row = (y - 92) / 29; if (wifiOffset + row < wifiCount) { selectedSsid = wifiNames[wifiOffset + row]; startKeyboard(KeyboardTarget::WifiPassword, selectedSsid, "", true); } }
    else if (y >= 210 && x < 80) page = Page::Home;
    else if (y >= 210 && wifiOffset + 4 < wifiCount) wifiOffset += 4;
  } else if (page == Page::Bluetooth) {
    if (y >= 50 && y < 90) scanBluetooth(); else if (y >= 210) page = Page::Home;
  } else if (page == Page::Infrared) {
    if (y >= 58 && y < 114) sendInfrared();
    else if (y >= 124 && y < 168) startKeyboard(KeyboardTarget::IrCode, "IR NEC CODE", irCode, false);
    else if (y >= 210) page = Page::Home;
  } else if (page == Page::Settings) {
    if (y >= 48 && y < 76) { themeIndex = (themeIndex + 1) % kThemeCount; saveSettings(); }
    else if (y >= 80 && y < 108) { sleepIndex = (sleepIndex + 1) % 4; saveSettings(); }
    else if (y >= 112 && y < 140) { utcOffset = utcOffset >= 14 ? -12 : utcOffset + 1; ntpStarted = false; startClock(); saveSettings(); }
    else if (y >= 144 && y < 172) page = Page::Bridge;
    else if (y >= 176 && y < 204) { WiFi.disconnect(true, true); notice = "Saved Wi-Fi cleared"; }
    else if (y >= 210) page = Page::Home;
  } else if (page == Page::Bridge) {
    if (y >= 52 && y < 90) startKeyboard(KeyboardTarget::BridgeUrl, "BRIDGE URL", bridgeUrl, false);
    else if (y >= 94 && y < 132) startKeyboard(KeyboardTarget::BridgeKey, "ACCESS KEY", bridgeKey, true);
    else if (y >= 136 && y < 174) testBridge();
    else if (y >= 210) page = Page::Settings;
  }
  drawPage();
}

void pollTouch() {
  bool pressed = touch.Pressed();
  if (!pressed) { touchDown = false; return; }
  if (touchDown) return;
  touchDown = true;
  if (displaySleeping) { wakeDisplay(); return; }
  int x = touch.X(), y = touch.Y();
  if (x >= 0 && x < Board::kWidth && y >= 0 && y < Board::kHeight) handleTap(x, y);
}

} // namespace

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  touch.setCal(526, 3443, 750, 3377, Board::kWidth, Board::kHeight, 1);
  loadSettings();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  startClock();
  NimBLEDevice::init("CYD Home");
  NimBLEDevice::getScan()->setScanCallbacks(&scanCallbacks, false);
  irsend.begin();
  lastInteraction = millis();
  drawPage();
}

void loop() {
  pollTouch();
  pollWifi();
  uint32_t timeout = sleepTimeout();
  if (!displaySleeping && timeout && millis() - lastInteraction >= timeout) {
    digitalWrite(TFT_BL, LOW);
    displaySleeping = true;
  }
  delay(12);
}
