# CYD Home Hub V2 — ESP32-2432S028R / TPM408-2.8

A touch-first, dark-themed 320×240 smart home controller for the **ESP32-2432S028R / CYD** with a **TPM408-2.8** display. It is an on-device embedded application, not a web page.

---

## What's New in Version 2.0.1 (Latest)

1. **Instant Touch Response (Zero-Deadzone Keyboard)**:
   - Fixed resistive touch debouncing with rapid 250 Hz sampling (`delay(4)` loop) and 2.5ms settle delay.
   - Continuous horizontal and vertical touch hit-testing: every tap anywhere in a key row maps to the nearest key—**zero dead zones between buttons or along margins**.
   - Keystrokes redraw only the input field (`drawTextInput()`), delivering instant, zero-lag character updates.
2. **Wi-Fi Password Visibility**:
   - Added a prominent **`[SHOW]` / `[HIDE]`** toggle button on the keyboard.
   - By default on Wi-Fi password entry, passwords are **visible** so you can easily verify every character and avoid typos.
3. **Rock-Solid Wi-Fi Connection Manager**:
   - 19.5 dBm maximum RF transmit power (`WiFi.setTxPower(WIFI_POWER_19_5dBm)`) to eliminate radio brownout.
   - ESP32 hardware event monitoring (`WiFi.onEvent`) providing instant feedback for wrong passwords (`AUTH_FAIL`, `4WAY_HANDSHAKE_TIMEOUT`) and out-of-range networks.
   - Persistent NVS credentials with automatic reconnection on boot.
4. **eWeLink & Sonoff Native Support**:
   - Direct integration with local **mwaqqp** server (`http://<PC_IP>:3000`) or the standalone bridge (`http://<PC_IP>:8787`).
   - Automatically loads switches and lights from eWeLink without vendor OAuth on the ESP32.
   - Also supports Home Assistant seamlessly.
5. **Verified 4-Part Browser Flasher**:
   - Ready to flash via Chrome/Edge from `web/index.html` with correct ESP32 offsets (`bootloader` at 0x1000, `partitions` at 0x8000, `boot_app0` at 0xE000, `cyd-home.bin` at 0x10000).

---

## Architecture

```
Sonoff / eWeLink Devices
          ↓
Local Node.js Bridge or mwaqqp Dashboard (Port 3000 or 8787 on your PC)
          ↓ (LAN JSON API)
CYD ESP32 Touchscreen (CYD Home Hub V2)
```

No cloud passwords or OAuth secrets are stored on the CYD. The CYD simply talks to your local LAN bridge.

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

### 3. Connect eWeLink Devices
- In your computer's terminal:
  - If using your existing **mwaqqp** server: start it with `npm start` (it runs on port 3000).
  - If using the standalone bridge: run `node bridge/bridge.js` (runs on port 8787).
- On the CYD, tap **Settings** → **Web Bridge**.
- Tap **Bridge URL** and enter `http://<YOUR_COMPUTER_IP>:3000` (or `:8787`).
- Tap **Test Connection** → "Bridge connected!".
- Tap **Back** → **Devices** to view and toggle your Sonoff/eWeLink devices!
