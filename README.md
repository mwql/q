# CYD Home Hub V2 — ESP32-2432S028R / TPM408-2.8

A touch-first, dark-themed 320×240 smart home controller for the **ESP32-2432S028R / CYD** with a **TPM408-2.8** display. It is an on-device embedded application, not a web page.

---

## What's New in Version 2.0.2 (Direct Cloud Edition)

1. **Zero 3rd Devices — Direct Cloud eWeLink Integration**:
   - **No PC, Raspberry Pi, or local bridge required!** The CYD connects directly over Wi-Fi to your existing Render cloud endpoint: `https://bott-r34h.onrender.com`.
   - Built-in Apple Shortcut style URL: `POST /api/ewelink/action` with `{ "deviceid": "100128c304", "action": "turn" }`.
   - Default primary device `100128c304` (*"M.room(corner)"*) is pre-configured and ready immediately.
   - HTTPS communication via `WiFiClientSecure` with automatic TLS heap cleanup.
2. **Instant Touch Response (Zero-Deadzone Keyboard)**:
   - Fixed resistive touch debouncing with rapid 250 Hz sampling (`delay(4)` loop) and 2.5ms settle delay.
   - Continuous horizontal and vertical touch hit-testing: every tap anywhere in a key row maps to the nearest key—**zero dead zones between buttons or along margins**.
   - Keystrokes redraw only the input field (`drawTextInput()`), delivering instant, zero-lag character updates.
3. **Wi-Fi Password Visibility**:
   - Added a prominent **`[SHOW]` / `[HIDE]`** toggle button on the keyboard.
   - By default on Wi-Fi password entry, passwords are **visible** so you can easily verify every character and avoid typos.
4. **Rock-Solid Wi-Fi Connection Manager**:
   - 19.5 dBm maximum RF transmit power (`WiFi.setTxPower(WIFI_POWER_19_5dBm)`) to eliminate radio brownout.
   - ESP32 hardware event monitoring (`WiFi.onEvent`) providing instant feedback for wrong passwords (`AUTH_FAIL`, `4WAY_HANDSHAKE_TIMEOUT`) and out-of-range networks.
   - Persistent NVS credentials with automatic reconnection on boot.
5. **Verified 4-Part Browser Flasher**:
   - Ready to flash via Chrome/Edge from `web/index.html` with correct ESP32 offsets (`bootloader` at 0x1000, `partitions` at 0x8000, `boot_app0` at 0xE000, `cyd-home.bin` at 0x10000).

---

## Architecture (Direct Cloud — No PC Needed)

```
Sonoff / eWeLink Devices
          ▲
          │ (eWeLink Cloud)
Render Cloud Service (https://bott-r34h.onrender.com)
          ▲
          │ (HTTPS REST API / Apple Shortcut action)
ESP32 CYD Touchscreen (CYD Home Hub V2)
```

No local PC, no Raspberry Pi, and no 3rd device needed! The CYD talks directly to the cloud service over your Wi-Fi router.

---

## Hardware Configuration (Known-Good TPM408-2.8)

| Component | Pin / Value | Notes |
|---|---|---|
| **TFT Driver** | `ILI9341_2_DRIVER` | 320 × 240 landscape (`setRotation(1)`) |
| **MOSI / SCLK / CS** | GPIO 13, 14, 15 | 65 MHz SPI frequency |
| **DC / Reset** | GPIO 2, GPIO 12 | Color byte swap enabled |
| **Backlight** | GPIO 21 | Active HIGH, auto-sleep after 10s |
| **Touch Controller** | XPT2046 | `TFT_Touch(33, 25, 32, 39)` |
| **IR Transmitter** | GPIO 27 | NEC transmission (transistor circuit required) |

---

## Quick Start & Walkthrough

### 1. Flash to CYD
- Open `web/index.html` in Chrome or Edge (or host via GitHub Pages).
- Connect the CYD via a micro-USB cable with data lines.
- Click **Install CYD Home Hub V2**, select the COM port, and allow full erase on first install.

### 2. Connect to Wi-Fi
- On the CYD home screen, tap **Wi-Fi** → **Scan Networks**.
- Tap your 2.4 GHz Wi-Fi network.
- The on-screen keyboard opens with **`SHOW`** mode enabled by default: you can see exactly what you type!
- Tap **JOIN**. If the password was wrong, the device will immediately warn you. Once connected, your IP is displayed.

### 3. Direct eWeLink Control (Zero Configuration!)
- **No PC or 3rd device needed!**
- The CYD is already pre-configured to connect to `https://bott-r34h.onrender.com`.
- Device `100128c304` (*"M.room(corner)"*) is already built in.
- Simply tap **Devices** on the CYD home screen:
  - The CYD automatically syncs your devices from the cloud over Wi-Fi.
  - Tap any device card to toggle it ON/OFF via the Apple Shortcut action API!
