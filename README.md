# CYD Home Hub — CYD-2432S028 / TPM408-2.8

A touch-first 320×240 landscape application launcher for the 2.8-inch **TPM408-2.8 / CYD-2432S028**. It is not a device-hosted web UI. The screen sleeps after 10 seconds and wakes on the next touch.

## Included apps

- **Devices:** reads and controls up to 16 `switch`, `light`, `fan`, and `cover` entities exposed by Home Assistant; four devices appear per page.
- **Wi-Fi:** opens the `CYD-Home` captive portal for Wi-Fi credentials, Home Assistant URL, long-lived token, and optional NEC IR code.
- **Bluetooth:** scans and lists up to four nearby BLE device names/addresses. It does not pair with or control Bluetooth devices.
- **IR Remote:** sends your configured 32-bit NEC code through an externally wired transmitter on GPIO 27.
- **System:** shows status, configuration state, and the 10-second touch-to-wake display sleep policy.

## Why Home Assistant is used for eWeLink

eWeLink cloud login requires vendor OAuth/client credentials and should not be put into an ESP32 binary. Add the official Sonoff/eWeLink integration to Home Assistant, expose your entities, then CYD Home Hub controls those entities with a **Home Assistant long-lived access token**. The token stays in ESP32 NVS and is not placed in the installer site.

## Build once, then install from the browser

### Automatic (recommended)

Push this folder to a GitHub repository, enable **Settings → Pages → GitHub Actions**, and push to `main`. The included workflow builds the firmware and publishes the `web/` installer automatically. Open the Pages URL in Chrome or Edge and press **Connect & install**. Neither Arduino IDE nor a local compiler is needed after that.

### Local build

1. Install [PlatformIO Core](https://platformio.org/install/cli), then run `pio run` in this folder.
2. Copy the app, bootloader, partition table, and `boot_app0.bin` to `web/firmware/` as the included GitHub workflow does. The installer manifest flashes each at its required ESP32 address.
3. Publish the `web/` directory over HTTPS (GitHub Pages, Netlify, or your own web host). Open `web/index.html` using Chrome or Edge and press **Connect & install**.

## First use

1. On the CYD, tap **Wi-Fi** → **Setup portal**. Join the `CYD-Home` network from your phone and visit `192.168.4.1`.
2. Create a long-lived access token in Home Assistant under your profile’s Security page, then enter both the Home Assistant URL and that token in the portal alongside your Wi-Fi details.
3. Enter your home Wi-Fi, `http://homeassistant.local:8123` (or your local Home Assistant URL), and the token. You can also set an IR NEC code, such as `20DF10EF`.
4. Open **Devices**. Your Home Assistant `switch.*`, `light.*`, `fan.*`, and `cover.*` entities appear there.

## Hardware notes

- This targets the verified TPM408-2.8 / CYD-2432S028R ILI9341 layout: display reset GPIO 12, backlight GPIO 21, 65 MHz display SPI, color inversion, and separate bit-banged XPT2046 touch pins (33/25/32/39).
- GPIO 27 is used only for the optional IR output. Use a transistor and IR LED with current limiting; do not drive an IR LED directly from the ESP32 pin.
- 4 MB ESP32 CYDs are tight on space; the included partition table leaves room for OTA updates but not a large local asset library.

## Security

This project intentionally excludes offensive Bluetooth/Wi-Fi functions. Keep the Home Assistant token private and revoke it in Home Assistant if the device is lost.
