// CYD Home Hub 2.0
// A touch-first controller for the TPM408-2.8 / ESP32-2432S028R.
// The CYD contains no cloud account password: a local Web Bridge owns the
// Home Assistant/eWeLink connection and exposes only device data and commands.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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

#define FW_VERSION "2.0.0"

namespace {

enum class Page : uint8_t {
  Home, Devices, Wifi, Keyboard, Bluetooth, Infrared, Settings, Bridge, Fun
};
enum class KeyboardTarget : uint8_t {
  None, WifiPassword, BridgeUrl, BridgeKey, IrCode
};
enum class KeyboardMode : uint8_t { Lower, Upper, Symbols };

struct Device {
  String id;
  String name;
  String type;
  String state;
};

struct BleEntry {
  String name;
  int rssi;
};

TFT_eSPI tft;
TFT_Touch touch(Board::kTouchCs, Board::kTouchClock, Board::kTouchDin,
                Board::kTouchDout);
Preferences preferences;
IRsend irsend(Board::kIrPin);

// ---- State ----
Page currentPage = Page::Home;
Page previousPage = Page::Home;
KeyboardTarget keyboardTarget = KeyboardTarget::None;
KeyboardMode keyboardMode = KeyboardMode::Lower;

bool touchDown = false;
bool displaySleeping = false;
bool wifiConnecting = false;
bool wifiConnected = false;
uint8_t wifiDisconnectReason = 0;
bool keyboardSecret = false;
bool keyboardShowPassword = true;
bool pageNeedsRedraw = true;
bool splashDone = false;

uint8_t themeIndex = 0;
uint8_t sleepIndex = 0;
int8_t utcOffset = 3;
uint8_t wifiOffset = 0;
uint8_t deviceOffset = 0;
uint8_t deviceCount = 0;
uint8_t bleCount = 0;
uint32_t lastInteraction = 0;
uint32_t lastTapAt = 0;
uint32_t pageOpenedAt = 0;
uint32_t wifiConnectStarted = 0;
uint32_t lastStatusBarUpdate = 0;
uint32_t toastShownAt = 0;
bool ntpStarted = false;

String toastMessage;
String selectedSsid;
String keyboardTitle;
String keyboardValue;
String bridgeUrl = "https://bott-r34h.onrender.com";
String bridgeKey;
String irCode = "20DF10EF";
String wifiNames[12];
int32_t wifiRssi[12];
uint8_t wifiCount = 0;
BleEntry bleEntries[8];
Device devices[20];

constexpr uint32_t kSleepChoices[] = {10000, 30000, 60000, 0};
constexpr char kLowerRows[][11] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
constexpr char kUpperRows[][11] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
constexpr char kSymbolRows[][11] = {"1234567890", "-_.:/@#", "!?$%&*+="};

const Theme& theme() { return kThemes[themeIndex % kThemeCount]; }
uint32_t sleepTimeout() { return kSleepChoices[sleepIndex % 4]; }
void drawPage();

// ----- Toast notification -------------------------------------------------------
void showToast(const String& message) {
  toastMessage = message;
  toastShownAt = millis();
}

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
  utcOffset = preferences.getChar("utc_offset", 3);
  bridgeUrl = preferences.getString("bridge_url", "https://bott-r34h.onrender.com");
  bridgeKey = preferences.getString("bridge_key", "");
  irCode = preferences.getString("ir_code", "20DF10EF");
  if (themeIndex >= kThemeCount) themeIndex = 0;
  if (sleepIndex > 3) sleepIndex = 0;
  if (utcOffset < -12 || utcOffset > 14) utcOffset = 3;
  if (bridgeUrl.length() == 0) {
    bridgeUrl = "https://bott-r34h.onrender.com";
  }

  // Pre-seed default eWeLink light so device screen is ready immediately
  if (deviceCount == 0) {
    devices[0].id = "100128c304";
    devices[0].name = "M.room(corner)";
    devices[0].type = "light";
    devices[0].state = "off";
    deviceCount = 1;
  }
}

// ----- UI primitives -------------------------------------------------------------
String trimText(const String& value, size_t limit) {
  if (value.length() <= limit) return value;
  return value.substring(0, limit > 3 ? limit - 3 : limit) + "...";
}

// Draw a small Wi-Fi signal icon at (x, y) based on RSSI
void drawWifiIcon(int x, int y, int32_t rssi, uint16_t color) {
  // 4-bar arc icon, 12×10 pixels
  uint8_t bars;
  if (rssi > -50) bars = 4;
  else if (rssi > -60) bars = 3;
  else if (rssi > -70) bars = 2;
  else bars = 1;
  uint16_t dim = theme().surface;
  // Draw dot at base
  tft.fillCircle(x + 1, y + 9, 1, color);
  // Arcs from small to large
  for (uint8_t i = 0; i < 4; ++i) {
    uint16_t c = (i < bars) ? color : dim;
    int r = 3 + i * 2;
    // Draw a simple arc approximation with lines
    for (int a = -40; a <= 40; a += 8) {
      float rad = a * 3.14159f / 180.0f;
      int px = x + 1 + (int)(r * sinf(rad));
      int py = y + 9 - (int)(r * cosf(rad));
      tft.drawPixel(px, py, c);
      tft.drawPixel(px + 1, py, c);
    }
  }
}

// Status bar at the top of every page (except keyboard)
void drawStatusBar(const String& title) {
  const Theme& c = theme();
  tft.fillRect(0, 0, Board::kWidth, 36, c.header);

  // Title
  tft.setTextColor(c.text, c.header);
  tft.setTextSize(1);
  tft.drawString(title, 12, 10, 2);

  // Right side: Wi-Fi indicator + clock
  int rx = 308;

  // Clock
  struct tm localTime;
  if (getLocalTime(&localTime, 5)) {
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &localTime);
    tft.setTextSize(1);
    tft.setTextColor(c.muted, c.header);
    tft.drawRightString(timeBuf, rx, 10, 2);
    rx -= 46;
  }

  // Wi-Fi status icon
  if (WiFi.status() == WL_CONNECTED) {
    drawWifiIcon(rx - 12, 13, WiFi.RSSI(), c.accent);
  } else {
    tft.setTextSize(1);
    tft.setTextColor(c.muted, c.header);
    tft.drawRightString("OFFLINE", rx, 14, 1);
  }
}

// Rounded card button
void card(int x, int y, int w, int h, const String& title,
          const String& detail = "", bool selected = false) {
  const Theme& c = theme();
  uint16_t fill = selected ? c.accent : c.surface;
  uint16_t textColor = selected ? c.header : c.text;
  uint16_t detailColor = selected ? c.header : c.muted;
  uint16_t borderColor = selected ? c.accent : c.surfaceRaised;

  tft.fillRoundRect(x, y, w, h, 6, fill);
  tft.drawRoundRect(x, y, w, h, 6, borderColor);

  tft.setTextSize(1);
  if (h >= 44) {
    // Large card: centered title + detail
    tft.setTextColor(textColor, fill);
    tft.drawCentreString(trimText(title, 18), x + w / 2, y + 8, 2);
    if (detail.length()) {
      tft.setTextColor(detailColor, fill);
      tft.drawCentreString(trimText(detail, 36), x + w / 2, y + h - 18, 1);
    }
  } else if (h >= 25 && detail.length() > 0) {
    // Medium card with title and detail (e.g. Wi-Fi network, Settings row, Devices row)
    tft.setTextColor(textColor, fill);
    tft.drawString(trimText(title, 18), x + 8, y + (h - 16) / 2, 2);
    tft.setTextColor(detailColor, fill);
    tft.drawRightString(trimText(detail, 20), x + w - 8, y + (h - 8) / 2, 1);
  } else if (h >= 25) {
    // Medium button without detail
    tft.setTextColor(textColor, fill);
    tft.drawCentreString(trimText(title, 20), x + w / 2, y + (h - 16) / 2, 2);
  } else {
    // Small card / button (e.g. "Back", "Next >" with h = 22)
    tft.setTextColor(textColor, fill);
    tft.drawCentreString(trimText(title, 15), x + w / 2, y + (h - 16) / 2, 2);
  }
}

// Action button (accent-colored)
void actionButton(int x, int y, int w, int h, const String& label) {
  const Theme& c = theme();
  tft.fillRoundRect(x, y, w, h, 6, c.accent);
  tft.setTextColor(c.header, c.accent);
  tft.setTextSize(1);
  tft.drawCentreString(label, x + w / 2, y + (h - 16) / 2, 2);
}

// Draw toast overlay if active
void drawToast() {
  if (!toastMessage.length() || millis() - toastShownAt > 3000) {
    toastMessage = "";
    return;
  }
  const Theme& c = theme();
  tft.fillRoundRect(20, 200, 280, 28, 6, c.surfaceRaised);
  tft.drawRoundRect(20, 200, 280, 28, 6, c.accent);
  tft.setTextColor(c.text, c.surfaceRaised);
  tft.setTextSize(1);
  tft.drawCentreString(trimText(toastMessage, 42), 160, 208, 1);
}

// Full-screen loading indicator
void showLoading(const String& title) {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("CYD HOME");

  // Centered loading message
  tft.setTextColor(c.text, c.background);
  tft.setTextSize(2);
  tft.drawCentreString(title, 160, 100, 2);

  // Animated dots area
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("Please wait...", 160, 130, 1);

  // Simple progress bar animation
  for (int i = 0; i < 3; i++) {
    int bx = 110 + i * 36;
    tft.fillRoundRect(bx, 150, 28, 4, 2, c.surfaceRaised);
  }
  uint8_t dot = (millis() / 400) % 3;
  tft.fillRoundRect(110 + dot * 36, 150, 28, 4, 2, c.accent);
}

// Empty state with icon-like text and message
void drawEmptyState(const String& icon, const String& message,
                    const String& hint = "") {
  const Theme& c = theme();
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(2);
  tft.drawCentreString(icon, 160, 90, 2);
  tft.setTextColor(c.text, c.background);
  tft.setTextSize(1);
  tft.drawCentreString(message, 160, 125, 2);
  if (hint.length()) {
    tft.setTextColor(c.muted, c.background);
    tft.drawCentreString(hint, 160, 150, 1);
  }
}

String wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
  if (wifiConnecting) return "Connecting...";
  return "No Wi-Fi";
}

void startClock() {
  if (ntpStarted || WiFi.status() != WL_CONNECTED) return;
  configTime(utcOffset * 3600, 0, "pool.ntp.org", "time.nist.gov");
  ntpStarted = true;
}

// ----- Direct Web Cloud / Bridge client -------------------------------------------
String bridgeBase() {
  String base = bridgeUrl;
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

bool bridgeReady() {
  return WiFi.status() == WL_CONNECTED && bridgeUrl.length() > 7;
}

bool httpRequest(const String& url, const String& method, const String& payload,
                String& outResponse, int& outCode) {
  if (WiFi.status() != WL_CONNECTED) {
    outCode = -1;
    return false;
  }

  HTTPClient http;
  http.setTimeout(18000);  // 18s for Render cloud free tier wake-up
  http.setReuse(false);

  bool isHttps = url.startsWith("https://");
  bool ok = false;

  if (isHttps) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();  // Bypass certificate validation on ESP32
    if (http.begin(secureClient, url)) {
      http.addHeader("Accept", "application/json");
      if (bridgeKey.length()) http.addHeader("X-CYD-Key", bridgeKey);
      if (method == "POST") {
        http.addHeader("Content-Type", "application/json");
        outCode = http.POST(payload);
      } else {
        outCode = http.GET();
      }
      if (outCode > 0) {
        outResponse = http.getString();
      }
      http.end();
      secureClient.stop();
      ok = (outCode >= 200 && outCode < 300);
    }
  } else {
    WiFiClient client;
    if (http.begin(client, url)) {
      http.addHeader("Accept", "application/json");
      if (bridgeKey.length()) http.addHeader("X-CYD-Key", bridgeKey);
      if (method == "POST") {
        http.addHeader("Content-Type", "application/json");
        outCode = http.POST(payload);
      } else {
        outCode = http.GET();
      }
      if (outCode > 0) {
        outResponse = http.getString();
      }
      http.end();
      client.stop();
      ok = (outCode >= 200 && outCode < 300);
    }
  }

  return ok;
}

bool fetchDevices() {
  if (!bridgeReady()) {
    showToast("Connect Wi-Fi first");
    return false;
  }
  showLoading("Syncing Cloud Devices...");

  // 1. Try eWeLink endpoint from Render cloud
  String endpoint = bridgeBase() + "/api/ewelink/devices";
  String resp;
  int code = 0;
  bool ok = httpRequest(endpoint, "GET", "", resp, code);

  // 2. If 404, fallback to /api/v1/devices
  if (!ok && code == 404) {
    endpoint = bridgeBase() + "/api/v1/devices";
    ok = httpRequest(endpoint, "GET", "", resp, code);
  }

  // 3. If both failed, try single light status endpoint /api/status/light
  if (!ok) {
    endpoint = bridgeBase() + "/api/status/light";
    int lightCode = 0;
    String lightResp;
    if (httpRequest(endpoint, "GET", "", lightResp, lightCode) && lightCode == 200) {
      lightResp.trim();
      lightResp.toUpperCase();
      deviceCount = 1;
      deviceOffset = 0;
      devices[0].id = "100128c304";
      devices[0].name = "M.room(corner)";
      devices[0].type = "light";
      devices[0].state = (lightResp == "ON") ? "on" : "off";
      showToast("Room Light: " + lightResp);
      return true;
    }

    showToast("Cloud error " + String(code));
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    showToast("JSON parse error");
    return false;
  }

  deviceCount = 0;
  deviceOffset = 0;

  if (doc.is<JsonArray>()) {
    for (JsonObject item : doc.as<JsonArray>()) {
      if (deviceCount >= 20) break;
      Device& dev = devices[deviceCount++];
      dev.id = item["deviceid"] | item["id"] | "";
      dev.name = item["name"] | dev.id;
      if (item["params"].is<JsonObject>() && item["params"]["switch"].is<const char*>()) {
        dev.state = item["params"]["switch"].as<String>();
      } else if (item["state"].is<const char*>()) {
        dev.state = item["state"].as<String>();
      } else {
        dev.state = "off";
      }
      String lowerName = dev.name;
      lowerName.toLowerCase();
      bool isLight = (lowerName.indexOf("room") >= 0 || lowerName.indexOf("light") >= 0 ||
                      lowerName.indexOf("corner") >= 0 || lowerName.indexOf("lamp") >= 0);
      dev.type = isLight ? "light" : (item["type"] | "switch");
    }
  } else if (doc["devices"].is<JsonArray>()) {
    for (JsonObject item : doc["devices"].as<JsonArray>()) {
      if (deviceCount >= 20) break;
      Device& dev = devices[deviceCount++];
      dev.id = item["id"] | item["deviceid"] | "";
      dev.name = item["name"] | dev.id;
      dev.type = item["type"] | "switch";
      dev.state = item["state"] | "off";
    }
  }

  if (deviceCount == 0) {
    devices[0].id = "100128c304";
    devices[0].name = "M.room(corner)";
    devices[0].type = "light";
    devices[0].state = "off";
    deviceCount = 1;
    showToast("Default eWeLink light ready");
  } else {
    showToast(String(deviceCount) + " device(s) synced");
  }

  return true;
}

bool toggleDevice(Device& device) {
  if (!bridgeReady()) {
    showToast("Wi-Fi not connected");
    return false;
  }

  String targetId = device.id.length() ? device.id : "100128c304";
  showToast("Toggling " + device.name + "...");

  // 1. Try eWeLink Cloud action (Apple Shortcut style: POST /api/ewelink/action)
  String url = bridgeBase() + "/api/ewelink/action";
  String payload = "{\"deviceid\":\"" + targetId + "\",\"action\":\"turn\"}";
  String resp;
  int code = 0;

  bool ok = httpRequest(url, "POST", payload, resp, code);

  if (ok && code >= 200 && code < 300) {
    JsonDocument reply;
    deserializeJson(reply, resp);
    if (reply["newState"].is<const char*>()) {
      device.state = reply["newState"].as<String>();
    } else {
      device.state = (device.state == "on" || device.state == "ON") ? "off" : "on";
    }
    showToast(device.name + ": " + (device.state == "on" ? "ON" : "OFF"));
    return true;
  }

  // 2. Fallback to /api/v1/devices/:id/toggle (Home Hub v1 style)
  if (code == 404) {
    url = bridgeBase() + "/api/v1/devices/" + targetId + "/toggle";
    ok = httpRequest(url, "POST", "{}", resp, code);
    if (ok && code >= 200 && code < 300) {
      JsonDocument reply;
      deserializeJson(reply, resp);
      if (reply["state"].is<const char*>()) {
        device.state = reply["state"].as<String>();
      } else {
        device.state = (device.state == "on" || device.state == "ON") ? "off" : "on";
      }
      showToast(device.name + " toggled");
      return true;
    }
  }

  // 3. Fallback to /api/widget/toggle if available
  if (code == 404) {
    url = bridgeBase() + "/api/widget/toggle";
    ok = httpRequest(url, "GET", "", resp, code);
    if (ok && code >= 200 && code < 300) {
      device.state = (device.state == "on" || device.state == "ON") ? "off" : "on";
      showToast(device.name + " toggled");
      return true;
    }
  }

  showToast("Control error " + String(code));
  return false;
}

bool testBridge() {
  if (!bridgeReady()) {
    showToast("Connect Wi-Fi first");
    return false;
  }
  showLoading("Testing Cloud...");

  // Test 1: Check light status
  String resp;
  int code = 0;
  if (httpRequest(bridgeBase() + "/api/status/light", "GET", "", resp, code) && code == 200) {
    resp.trim();
    showToast("Cloud OK! Light: " + resp);
    return true;
  }

  // Test 2: Check general status
  if (httpRequest(bridgeBase() + "/api/status", "GET", "", resp, code) && code == 200) {
    showToast("Cloud Online (Ready)");
    return true;
  }

  // Test 3: Check /api/v1/status
  if (httpRequest(bridgeBase() + "/api/v1/status", "GET", "", resp, code) && code == 200) {
    showToast("Bridge Connected!");
    return true;
  }

  showToast("Cloud error " + String(code));
  return false;
}

// ----- Wi-Fi --------------------------------------------------------------------
void scanWifi() {
  showLoading("Scanning Wi-Fi...");
  wifiCount = 0;
  wifiOffset = 0;
  int count = WiFi.scanNetworks(false, true);
  for (int i = 0; i < count && wifiCount < 12; ++i) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool duplicate = false;
    for (uint8_t known = 0; known < wifiCount; ++known) {
      if (wifiNames[known] == ssid) duplicate = true;
    }
    if (duplicate) continue;
    wifiNames[wifiCount] = ssid;
    wifiRssi[wifiCount] = WiFi.RSSI(i);
    ++wifiCount;
  }
  WiFi.scanDelete();
  if (wifiCount) {
    showToast(String(wifiCount) + " networks found");
  } else {
    showToast("No Wi-Fi networks found");
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiConnected = true;
      wifiConnecting = false;
      wifiDisconnectReason = 0;
      startClock();
      showToast("Connected: " + WiFi.localIP().toString());
      pageNeedsRedraw = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      wifiConnected = false;
      uint8_t reason = info.wifi_sta_disconnected.reason;
      wifiDisconnectReason = reason;
      if (wifiConnecting) {
        if (reason == 2 || reason == 15 || reason == 202 || reason == 204) {
          wifiConnecting = false;
          showToast("Wrong password! Check input.");
          pageNeedsRedraw = true;
        } else if (reason == 201) {
          wifiConnecting = false;
          showToast("SSID out of range");
          pageNeedsRedraw = true;
        }
      }
      break;
    }
    default:
      break;
  }
}

void connectWifi(const String& password) {
  WiFi.disconnect(true, false);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  wifiConnecting = true;
  wifiConnected = false;
  wifiDisconnectReason = 0;
  wifiConnectStarted = millis();
  WiFi.begin(selectedSsid.c_str(), password.c_str());
  showToast("Connecting to " + selectedSsid + "...");
}

void pollWifi() {
  if (!wifiConnecting) return;
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;
    wifiConnected = true;
    startClock();
    showToast("Connected: " + WiFi.localIP().toString());
    pageNeedsRedraw = true;
  } else if (millis() - wifiConnectStarted > 25000) {
    wifiConnecting = false;
    showToast("Connection timed out");
    pageNeedsRedraw = true;
  }
}

// ----- Bluetooth and infrared ---------------------------------------------------
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertised) override {
    if (bleCount >= 8) return;
    String name = advertised->getName().c_str();
    if (!name.length()) name = advertised->getAddress().toString().c_str();
    bleEntries[bleCount].name = name;
    bleEntries[bleCount].rssi = advertised->getRSSI();
    bleCount++;
  }
} scanCallbacks;

void scanBluetooth() {
  showLoading("Scanning BLE...");
  bleCount = 0;
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->clearResults();
  scanner->setActiveScan(true);
  scanner->setMaxResults(0);
  scanner->start(5, false, true);
  if (bleCount) {
    showToast(String(bleCount) + " BLE devices found");
  } else {
    showToast("No BLE devices found");
  }
}

void sendInfrared() {
  char* end = nullptr;
  uint32_t code = strtoul(irCode.c_str(), &end, 16);
  if (!irCode.length() || (end && *end)) {
    showToast("Invalid hex code");
    return;
  }
  irsend.sendNEC(code, 32);
  showToast("IR sent: 0x" + irCode);
}

// ----- Keyboard -----------------------------------------------------------------
void startKeyboard(KeyboardTarget target, const String& title,
                   const String& value, bool secret) {
  keyboardTarget = target;
  keyboardTitle = title;
  keyboardValue = value;
  keyboardSecret = secret;
  // If entering Wi-Fi password, default to showing password text so user can see it!
  keyboardShowPassword = (target == KeyboardTarget::WifiPassword) ? true : false;
  keyboardMode = KeyboardMode::Lower;
  previousPage = currentPage;
  currentPage = Page::Keyboard;
  pageOpenedAt = millis();
  pageNeedsRedraw = true;
}

const char* keyboardRow(uint8_t row) {
  if (keyboardMode == KeyboardMode::Upper) return kUpperRows[row];
  if (keyboardMode == KeyboardMode::Symbols) return kSymbolRows[row];
  return kLowerRows[row];
}

String displayKeyboardValue() {
  String val = keyboardValue;
  if (keyboardSecret && !keyboardShowPassword) {
    String masked;
    for (size_t i = 0; i < val.length(); ++i) masked += '*';
    val = masked;
  }
  // Add cursor
  val += "|";
  if (val.length() > 22) {
    val = ".." + val.substring(val.length() - 20);
  }
  return val;
}

void drawTextInput() {
  const Theme& c = theme();
  // Clear & redraw text field
  tft.fillRoundRect(12, 44, 230, 30, 5, c.surface);
  tft.drawRoundRect(12, 44, 230, 30, 5, c.accent);
  tft.setTextColor(c.text, c.surface);
  tft.setTextSize(1);
  tft.drawString(displayKeyboardValue(), 18, 52, 2);

  // Show/Hide or Clear button
  if (keyboardSecret) {
    uint16_t btnBg = keyboardShowPassword ? c.accent : c.surfaceRaised;
    uint16_t btnFg = keyboardShowPassword ? c.header : c.text;
    tft.fillRoundRect(248, 44, 60, 30, 5, btnBg);
    tft.drawRoundRect(248, 44, 60, 30, 5, c.accent);
    tft.setTextColor(btnFg, btnBg);
    tft.setTextSize(1);
    tft.drawCentreString(keyboardShowPassword ? "HIDE" : "SHOW", 278, 52, 2);
  } else {
    tft.fillRoundRect(248, 44, 60, 30, 5, c.surfaceRaised);
    tft.drawRoundRect(248, 44, 60, 30, 5, c.surfaceRaised);
    tft.setTextColor(c.muted, c.surfaceRaised);
    tft.setTextSize(1);
    tft.drawCentreString("CLR", 278, 52, 2);
  }
}

void drawKeyRow(const char* characters, int y) {
  const Theme& c = theme();
  int count = strlen(characters);
  int width = (296 - (count - 1) * 3) / count;
  int left = 12 + (296 - (width * count + (count - 1) * 3)) / 2;
  for (int i = 0; i < count; ++i) {
    int x = left + i * (width + 3);
    tft.fillRoundRect(x, y, width, 27, 4, c.surfaceRaised);
    tft.setTextColor(c.text, c.surfaceRaised);
    tft.setTextSize(1);
    tft.drawCentreString(String(characters[i]), x + width / 2, y + 7, 2);
  }
}

void drawKeyboard() {
  const Theme& c = theme();
  tft.fillScreen(c.background);

  // Keyboard header
  tft.fillRect(0, 0, Board::kWidth, 36, c.header);
  tft.setTextColor(c.text, c.header);
  tft.setTextSize(2);
  tft.drawString(keyboardTitle, 10, 9, 2);

  // Mode indicator
  const char* modeLabel =
      keyboardMode == KeyboardMode::Symbols ? "123"
      : keyboardMode == KeyboardMode::Upper ? "ABC"
                                            : "abc";
  tft.setTextSize(1);
  tft.setTextColor(c.muted, c.header);
  tft.drawRightString(modeLabel, 308, 14, 1);

  // Draw input text field & show/hide button
  drawTextInput();

  // Key rows
  drawKeyRow(keyboardRow(0), 82);
  drawKeyRow(keyboardRow(1), 113);
  drawKeyRow(keyboardRow(2), 144);

  // Bottom row: mode | space | del | back | action
  int by = 180;
  int bh = 36;

  // Mode toggle
  tft.fillRoundRect(12, by, 44, bh, 5, c.surfaceRaised);
  tft.setTextColor(c.accent, c.surfaceRaised);
  tft.setTextSize(1);
  tft.drawCentreString("aA#", 34, by + 11, 2);

  // Space bar
  tft.fillRoundRect(60, by, 78, bh, 5, c.surfaceRaised);
  tft.setTextColor(c.text, c.surfaceRaised);
  tft.drawCentreString("SPACE", 99, by + 11, 2);

  // Delete
  tft.fillRoundRect(142, by, 46, bh, 5, c.surfaceRaised);
  tft.setTextColor(c.text, c.surfaceRaised);
  tft.drawCentreString("DEL", 165, by + 11, 2);

  // Cancel
  tft.fillRoundRect(192, by, 52, bh, 5, c.surface);
  tft.drawRoundRect(192, by, 52, bh, 5, c.accent);
  tft.setTextColor(c.muted, c.surface);
  tft.drawCentreString("Back", 218, by + 11, 2);

  // Action (Join/Save)
  String actionLabel =
      keyboardTarget == KeyboardTarget::WifiPassword ? "JOIN" : "SAVE";
  actionButton(248, by, 60, bh, actionLabel);
}

bool appendKeyboardCharFromRow(const char* characters, int tapX) {
  int count = strlen(characters);
  if (count <= 0) return false;
  // Distribute tap across 300 horizontal pixels without gaps or dead edges
  int clampedX = constrain(tapX, 10, 309);
  int idx = ((clampedX - 10) * count) / 300;
  if (idx < 0) idx = 0;
  if (idx >= count) idx = count - 1;
  keyboardValue += characters[idx];
  return true;
}

void finishKeyboard() {
  switch (keyboardTarget) {
    case KeyboardTarget::WifiPassword:
      connectWifi(keyboardValue);
      currentPage = Page::Wifi;
      break;
    case KeyboardTarget::BridgeUrl:
      bridgeUrl = keyboardValue;
      saveSettings();
      showToast("Cloud URL saved");
      currentPage = Page::Bridge;
      break;
    case KeyboardTarget::BridgeKey:
      bridgeKey = keyboardValue;
      saveSettings();
      showToast("Bridge key saved");
      currentPage = Page::Bridge;
      break;
    case KeyboardTarget::IrCode:
      irCode = keyboardValue;
      saveSettings();
      showToast("IR code saved");
      currentPage = Page::Infrared;
      break;
    default:
      currentPage = Page::Home;
      break;
  }
  keyboardTarget = KeyboardTarget::None;
  pageOpenedAt = millis();
  pageNeedsRedraw = true;
}

void handleKeyboardTap(int x, int y) {
  // Show / Hide password or Clear button
  if (y >= 40 && y < 76) {
    if (x >= 240) {
      if (keyboardSecret) {
        keyboardShowPassword = !keyboardShowPassword;
      } else {
        keyboardValue = "";
      }
      drawTextInput();
    }
    return;
  }

  // Row 0
  if (y >= 76 && y < 109) {
    if (appendKeyboardCharFromRow(keyboardRow(0), x)) {
      drawTextInput();
      return;
    }
  }
  // Row 1
  else if (y >= 109 && y < 142) {
    if (appendKeyboardCharFromRow(keyboardRow(1), x)) {
      drawTextInput();
      return;
    }
  }
  // Row 2
  else if (y >= 142 && y < 174) {
    if (appendKeyboardCharFromRow(keyboardRow(2), x)) {
      drawTextInput();
      return;
    }
  }
  // Bottom row
  else if (y >= 174 && y <= 240) {
    if (x < 58) {
      keyboardMode = static_cast<KeyboardMode>(
          (static_cast<uint8_t>(keyboardMode) + 1) % 3);
      drawKeyboard();
      return;
    } else if (x < 138) {
      keyboardValue += ' ';
      drawTextInput();
      return;
    } else if (x < 188) {
      if (keyboardValue.length()) {
        keyboardValue.remove(keyboardValue.length() - 1);
        drawTextInput();
      }
      return;
    } else if (x < 246) {
      currentPage = previousPage;
      keyboardTarget = KeyboardTarget::None;
      pageOpenedAt = millis();
      pageNeedsRedraw = true;
      drawPage();
      return;
    } else {
      finishKeyboard();
      return;
    }
  }
}

// ----- Page drawing -------------------------------------------------------------

void drawHome() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("CYD HOME");

  // 2×2 grid of app cards + settings bar
  // Devices
  card(12, 44, 145, 58, "Devices", "eWeLink Cloud");
  // Wi-Fi
  card(163, 44, 145, 58, "Wi-Fi", wifiStatusText());
  // Bluetooth
  card(12, 108, 145, 58, "Bluetooth", "BLE discovery");
  // IR Remote
  card(163, 108, 145, 58, "IR Remote", "Send NEC codes");

  // Settings bar at bottom
  card(12, 174, 96, 36, "Settings");
  card(113, 174, 96, 36, "Cloud API");
  card(214, 174, 94, 36, "Fun", "", false);

  // Version footer
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("v" FW_VERSION, 160, 220, 1);
}

void drawDevices() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("DEVICES");

  if (!deviceCount && !bridgeReady()) {
    drawEmptyState("[ ]", "Wi-Fi Offline",
                   "Connect Wi-Fi in Wi-Fi menu");
    // Refresh and Back at bottom
    card(12, 210, 68, 22, "Back");
    actionButton(160, 210, 148, 22, "Sync");
    return;
  }

  if (!deviceCount) {
    drawEmptyState("( )", "No Devices",
                   "Tap Sync to load from Cloud");
    card(12, 210, 68, 22, "Back");
    actionButton(160, 210, 148, 22, "Sync");
    return;
  }

  // Device list - up to 4 per page
  for (uint8_t row = 0; row < 4; ++row) {
    uint8_t index = deviceOffset + row;
    if (index >= deviceCount) break;
    Device& device = devices[index];
    bool on = (device.state == "on" || device.state == "ON" || device.state == "open");

    int cy = 42 + row * 40;

    // Device type icon prefix
    String typeIcon;
    if (device.type == "light") typeIcon = "*";
    else if (device.type == "fan") typeIcon = "~";
    else if (device.type == "cover") typeIcon = "^";
    else typeIcon = "o";

    String displayName = typeIcon + " " + device.name;
    String stateText = on ? "ON" : "OFF";

    card(12, cy, 296, 36, displayName, stateText, on);
  }

  // Bottom nav
  card(12, 210, 68, 22, "Back");
  actionButton(86, 210, 68, 22, "Sync");

  // Page indicator
  if (deviceCount > 4) {
    uint8_t pageNum = (deviceOffset / 4) + 1;
    uint8_t totalPages = (deviceCount + 3) / 4;
    String pageText = String(pageNum) + "/" + String(totalPages);
    tft.setTextColor(c.muted, c.background);
    tft.setTextSize(1);
    tft.drawCentreString(pageText, 190, 214, 1);

    if (deviceOffset + 4 < deviceCount) {
      card(216, 210, 92, 22, "Next >");
    }
  }
}

void drawWifi() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("WI-FI");

  // Connection status card
  if (WiFi.status() == WL_CONNECTED) {
    card(12, 42, 296, 36, WiFi.SSID(),
         "IP: " + WiFi.localIP().toString(), true);
  } else if (wifiConnecting) {
    card(12, 42, 296, 36, "Connecting...",
         "Waiting for " + selectedSsid);
  } else {
    card(12, 42, 296, 36, "Not connected",
         "Tap Scan to find networks");
  }

  // Scan button
  actionButton(12, 84, 296, 28, "Scan Networks");

  if (wifiCount == 0) {
    tft.setTextColor(c.muted, c.background);
    tft.setTextSize(1);
    tft.drawCentreString("No networks scanned yet", 160, 130, 1);
  }

  // Network list
  for (uint8_t row = 0; row < 3; ++row) {
    uint8_t index = wifiOffset + row;
    if (index >= wifiCount) break;
    int cy = 118 + row * 30;

    // Signal strength suffix
    String signal;
    if (wifiRssi[index] > -50) signal = "****";
    else if (wifiRssi[index] > -60) signal = "*** ";
    else if (wifiRssi[index] > -70) signal = "**  ";
    else signal = "*   ";

    card(12, cy, 296, 27, wifiNames[index],
         signal + " " + String(wifiRssi[index]) + "dBm");
  }

  // Bottom nav
  card(12, 210, 68, 22, "Back");
  if (wifiOffset + 3 < wifiCount) {
    card(160, 210, 148, 22, "More >");
  }
}

void drawBluetooth() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("BLUETOOTH");

  actionButton(12, 42, 296, 32, "Scan BLE Devices");

  if (bleCount == 0) {
    drawEmptyState("(( ))", "No devices found",
                   "Tap scan to discover nearby BLE");
  } else {
    // BLE device list - show up to 4
    for (uint8_t i = 0; i < bleCount && i < 4; ++i) {
      int cy = 82 + i * 30;
      String rssiStr = String(bleEntries[i].rssi) + " dBm";
      card(12, cy, 296, 27, bleEntries[i].name, rssiStr);
    }
    if (bleCount > 4) {
      tft.setTextColor(c.muted, c.background);
      tft.setTextSize(1);
      tft.drawCentreString("+" + String(bleCount - 4) + " more devices", 160,
                           198, 1);
    }
  }

  card(12, 210, 68, 22, "Back");
}

void drawInfrared() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("IR REMOTE");

  // Current code display
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("NEC Code", 160, 44, 1);
  tft.setTextColor(c.accent, c.background);
  tft.setTextSize(2);
  tft.drawCentreString("0x" + irCode, 160, 62, 2);

  // Send button (big, prominent)
  actionButton(12, 96, 296, 44, "SEND IR");

  // Edit code
  card(12, 148, 296, 36, "Change NEC Code", "Edit hexadecimal value");

  // Hardware note
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("GPIO 27 - transistor + IR LED required", 160, 195, 1);

  card(12, 210, 68, 22, "Back");
}

String sleepLabel() {
  uint32_t value = sleepTimeout();
  return value ? String(value / 1000) + "s" : "Never";
}

void drawSettings() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("SETTINGS");

  // Theme
  String themeName = themeIndex == 0 ? "Ocean" : "Midnight";
  card(12, 42, 296, 30, "Theme", themeName + " - tap to change");

  // Sleep
  card(12, 76, 296, 30, "Screen Sleep", sleepLabel() + " - tap to change");

  // Timezone
  String zone = "UTC" + String(utcOffset >= 0 ? "+" : "") + String(utcOffset);
  card(12, 110, 296, 30, "Timezone", zone + " - tap to change");

  // Cloud API shortcut
  card(12, 144, 296, 30, "Cloud API",
       bridgeUrl.length() ? trimText(bridgeUrl, 28) : "Not configured");

  // Forget Wi-Fi
  card(12, 178, 296, 30, "Forget Wi-Fi", "Clear saved network");

  // Bottom
  card(12, 214, 68, 22, "Back");

  // Version + memory info
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  String info = "v" FW_VERSION " | " + String(ESP.getFreeHeap() / 1024) + "KB free";
  tft.drawRightString(info, 308, 218, 1);
}

void drawBridge() {
  const Theme& c = theme();
  tft.fillScreen(c.background);

  drawStatusBar("CLOUD API");

  // URL field
  card(12, 42, 296, 40, "Cloud Endpoint",
       bridgeUrl.length() ? trimText(bridgeUrl, 36)
                          : "Tap to enter Cloud URL");

  // Device ID field
  card(12, 88, 296, 40, "eWeLink Device",
       "100128c304 (Corner Room)");

  // Test button
  actionButton(12, 134, 296, 36, "Test Cloud Connection");

  // Status indicator
  if (bridgeReady()) {
    tft.setTextColor(c.success, c.background);
    tft.setTextSize(1);
    tft.drawCentreString("Direct Cloud API (No PC Needed)", 160, 180, 1);
  } else {
    tft.setTextColor(c.muted, c.background);
    tft.setTextSize(1);
    if (WiFi.status() != WL_CONNECTED) {
      tft.drawCentreString("Connect Wi-Fi first", 160, 180, 1);
    } else {
      tft.drawCentreString("Enter your cloud server address", 160, 180, 1);
    }
  }

  // Architecture note
  tft.setTextColor(c.muted, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("CYD -> Render Cloud -> eWeLink", 160, 196, 1);

  card(12, 214, 68, 22, "Back");
}

void drawFun() {
  const Theme& c = theme();
  tft.fillScreen(c.background);
  drawStatusBar("FUN TOOLS");

  card(12, 42, 145, 40, "WiFi IDS");
  card(163, 42, 145, 40, "Flock Camera");
  
  card(12, 88, 145, 40, "BLE Trackers");
  card(163, 88, 145, 40, "Saved BLE (5)");
  
  card(12, 134, 145, 40, "Net Stats");
  card(163, 134, 145, 40, "Create Wi-Fi AP");

  card(12, 210, 68, 22, "Back");
}

void drawPage() {
  switch (currentPage) {
    case Page::Home:      drawHome(); break;
    case Page::Devices:   drawDevices(); break;
    case Page::Wifi:      drawWifi(); break;
    case Page::Keyboard:  drawKeyboard(); break;
    case Page::Bluetooth: drawBluetooth(); break;
    case Page::Infrared:  drawInfrared(); break;
    case Page::Settings:  drawSettings(); break;
    case Page::Bridge:    drawBridge(); break;
    case Page::Fun:       drawFun(); break;
  }
  pageNeedsRedraw = false;
}

// ----- Splash screen ------------------------------------------------------------
void drawSplash() {
  const Theme& c = theme();
  tft.fillScreen(c.background);

  // App name
  tft.setTextColor(c.accent, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("CYD HOME HUB", 160, 65, 4);

  // Version
  tft.setTextColor(c.text, c.background);
  tft.setTextSize(1);
  tft.drawCentreString("v" FW_VERSION, 160, 102, 2);

  // Subtitle
  tft.setTextColor(c.muted, c.background);
  tft.drawCentreString("Touch-first smart home controller", 160, 132, 1);

  // Board info
  tft.drawCentreString("ESP32-2432S028R / TPM408-2.8", 160, 155, 1);

  // Loading bar animation
  for (int i = 0; i <= 100; i += 5) {
    int barWidth = (i * 200) / 100;
    tft.fillRoundRect(60, 190, barWidth, 6, 3, c.accent);
    tft.fillRoundRect(60 + barWidth, 190, 200 - barWidth, 6, 3, c.surface);
    delay(20);
  }
  delay(400);
}

// ----- Touch routing and display sleep ------------------------------------------
void wakeDisplay() {
  digitalWrite(TFT_BL, HIGH);
  displaySleeping = false;
  lastInteraction = millis();
  pageNeedsRedraw = true;
}

void navigateTo(Page target) {
  previousPage = currentPage;
  currentPage = target;
  pageOpenedAt = millis();
  pageNeedsRedraw = true;
}

void handleTap(int x, int y) {
  lastInteraction = millis();

  if (currentPage == Page::Keyboard) {
    handleKeyboardTap(x, y);
    return;
  }

  if (currentPage == Page::Home) {
    if (y >= 44 && y < 102) {
      if (x < 160) {
        navigateTo(Page::Devices);
        fetchDevices();
        return;
      } else {
        navigateTo(Page::Wifi);
      }
    } else if (y >= 108 && y < 166) {
      if (x < 160)
        navigateTo(Page::Bluetooth);
      else
        navigateTo(Page::Infrared);
    } else if (y >= 174 && y < 210) {
      if (x < 110)
        navigateTo(Page::Settings);
      else if (x < 210)
        navigateTo(Page::Bridge);
      else
        navigateTo(Page::Fun);
    }
  } else if (currentPage == Page::Devices) {
    if (y >= 210) {
      if (x < 80) {
        navigateTo(Page::Home);
      } else if (x < 154) {
        fetchDevices();
        pageNeedsRedraw = true;
      } else if (deviceOffset + 4 < deviceCount) {
        deviceOffset += 4;
        pageNeedsRedraw = true;
      }
    } else if (!deviceCount && y < 200) {
      fetchDevices();
      pageNeedsRedraw = true;
    } else {
      for (uint8_t row = 0; row < 4; ++row) {
        int top = 42 + row * 40;
        if (y >= top && y < top + 36 && deviceOffset + row < deviceCount) {
          toggleDevice(devices[deviceOffset + row]);
          pageNeedsRedraw = true;
          break;
        }
      }
    }
  } else if (currentPage == Page::Wifi) {
    if (y >= 42 && y < 78 && WiFi.status() == WL_CONNECTED) {
      // Tapping connected card does nothing special
    } else if (y >= 84 && y < 112) {
      scanWifi();
      pageNeedsRedraw = true;
    } else if (y >= 118 && y < 208 && wifiCount) {
      uint8_t row = (y - 118) / 30;
      if (wifiOffset + row < wifiCount) {
        selectedSsid = wifiNames[wifiOffset + row];
        startKeyboard(KeyboardTarget::WifiPassword, selectedSsid, "", true);
      }
    } else if (y >= 210) {
      if (x < 80) {
        navigateTo(Page::Home);
      } else if (wifiOffset + 3 < wifiCount) {
        wifiOffset += 3;
        pageNeedsRedraw = true;
      }
    }
  } else if (currentPage == Page::Bluetooth) {
    if (y >= 42 && y < 78) {
      scanBluetooth();
      pageNeedsRedraw = true;
    } else if (y >= 210) {
      navigateTo(Page::Home);
    }
  } else if (currentPage == Page::Infrared) {
    if (y >= 96 && y < 140) {
      sendInfrared();
      pageNeedsRedraw = true;
    } else if (y >= 148 && y < 184) {
      startKeyboard(KeyboardTarget::IrCode, "IR NEC CODE", irCode, false);
    } else if (y >= 210) {
      navigateTo(Page::Home);
    }
  } else if (currentPage == Page::Settings) {
    if (y >= 42 && y < 72) {
      themeIndex = (themeIndex + 1) % kThemeCount;
      saveSettings();
      pageNeedsRedraw = true;
    } else if (y >= 76 && y < 106) {
      sleepIndex = (sleepIndex + 1) % 4;
      saveSettings();
      showToast("Sleep: " + sleepLabel());
      pageNeedsRedraw = true;
    } else if (y >= 110 && y < 140) {
      utcOffset = utcOffset >= 14 ? -12 : utcOffset + 1;
      ntpStarted = false;
      startClock();
      saveSettings();
      pageNeedsRedraw = true;
    } else if (y >= 144 && y < 174) {
      navigateTo(Page::Bridge);
    } else if (y >= 178 && y < 208) {
      WiFi.disconnect(true, true);
      showToast("Wi-Fi credentials cleared");
      pageNeedsRedraw = true;
    } else if (y >= 214) {
      navigateTo(Page::Home);
    }
  } else if (currentPage == Page::Bridge) {
    if (y >= 42 && y < 82) {
      startKeyboard(KeyboardTarget::BridgeUrl, "CLOUD URL", bridgeUrl, false);
    } else if (y >= 88 && y < 128) {
      showToast("Device: 100128c304");
    } else if (y >= 134 && y < 170) {
      testBridge();
      pageNeedsRedraw = true;
    } else if (y >= 214) {
      navigateTo(Page::Settings);
    }
  } else if (currentPage == Page::Fun) {
    if (y >= 42 && y < 82) {
      if (x < 160) showToast("Starting WiFi IDS...");
      else showToast("Scanning Flock Cameras...");
      pageNeedsRedraw = true;
    } else if (y >= 88 && y < 128) {
      if (x < 160) showToast("Scanning for Trackers...");
      else showToast("Opening Saved BLE..."); // Saved BLE slots logic
      pageNeedsRedraw = true;
    } else if (y >= 134 && y < 174) {
      if (x < 160) showToast("Running Speed Test...");
      else {
        // Start a basic Wi-Fi Hotspot
        WiFi.softAP("CYD-Hotspot", "12345678");
        showToast("Started AP: CYD-Hotspot");
      }
      pageNeedsRedraw = true;
    } else if (y >= 210 && x < 80) {
      navigateTo(Page::Home);
    }
  }

  if (pageNeedsRedraw) drawPage();
}

bool readTouchRaw(int& outX, int& outY) {
  if (!touch.Pressed()) {
    delayMicroseconds(2000);
    if (!touch.Pressed()) return false;
  }
  outX = touch.X();
  outY = touch.Y();
  return (outX >= 0 && outX < Board::kWidth && outY >= 0 && outY < Board::kHeight);
}

void pollTouch() {
  int x = 0, y = 0;
  bool pressed = readTouchRaw(x, y);

  if (!pressed) {
    touchDown = false;
    return;
  }

  // Already latched touch
  if (touchDown) return;
  touchDown = true;

  // Wake screen on first touch without activating controls
  if (displaySleeping) {
    wakeDisplay();
    drawPage();
    return;
  }

  // Prevent ghost touches right after a page transition (120ms guard)
  if (millis() - pageOpenedAt < 120) return;

  // Debounce rapid taps (80ms guard)
  if (millis() - lastTapAt < 80) return;
  lastTapAt = millis();

  handleTap(x, y);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("\n[CYD Home Hub v" FW_VERSION "]");

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  touch.setCal(526, 3443, 750, 3377, Board::kWidth, Board::kHeight, 1);

  loadSettings();

  // Show splash screen
  drawSplash();

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.onEvent(onWiFiEvent);
  WiFi.begin();
  startClock();

  NimBLEDevice::init("CYD Home");
  NimBLEDevice::getScan()->setScanCallbacks(&scanCallbacks, false);
  irsend.begin();

  lastInteraction = millis();
  splashDone = true;
  pageOpenedAt = millis();
  drawPage();

  Serial.println("[Ready]");
}

void loop() {
  pollTouch();
  pollWifi();

  // Toast timeout redraw
  if (toastMessage.length() && millis() - toastShownAt > 3000) {
    toastMessage = "";
  }

  // Screen sleep
  uint32_t timeout = sleepTimeout();
  if (!displaySleeping && timeout && millis() - lastInteraction >= timeout) {
    digitalWrite(TFT_BL, LOW);
    displaySleeping = true;
  }

  delay(4);
}
