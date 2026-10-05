# CYD Home Hub — CYD-2432S028

A touch-first ESP32 application launcher for the 2.8-inch 240×320 **CYD-2432S028**. It is not a device-hosted web UI.

## Included apps

- **Devices:** switches and lights (including Sonoff/eWeLink devices) exposed by Home Assistant; tap a card to toggle it.
- **Wi-Fi:** connects through an on-screen captive portal named `CYD-Home`.
- **Bluetooth:** BLE discovery only. It never pairs, injects, jams, or attacks devices.
- **IR Remote:** sends an optional NEC IR command using an externally wired, transistor-driven IR LED on GPIO 27.
- **Settings:** reports the secure Home Assistant bridge status.

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

1. On the CYD, open **Wi-Fi** → **Connect / change Wi-Fi**. Join the `CYD-Home` network from your phone and visit `192.168.4.1`.
2. Create a long-lived access token in Home Assistant under your profile’s Security page, then enter both the Home Assistant URL and that token in the portal alongside your Wi-Fi details.
3. Open **Devices**. Your Home Assistant `switch.*`, `light.*`, `fan.*`, and `cover.*` entities appear there.

The USB serial `HA_URL` and `HA_TOKEN` commands remain available as a recovery path, but normal setup never needs them.

## Hardware notes

- This targets the ordinary ESP32 CYD-2432S028 / ILI9341 layout. The touch calibration is a common starting point; clones vary, so run TFT_eSPI's calibration example and replace `touchCalData` if touch targets are offset.
- GPIO 27 is used only for the optional IR output. Use a transistor and IR LED with current limiting; do not drive an IR LED directly from the ESP32 pin.
- 4 MB ESP32 CYDs are tight on space; the included partition table leaves room for OTA updates but not a large local asset library.

## Security

This project intentionally excludes offensive Bluetooth/Wi-Fi functions. Keep the Home Assistant token private and revoke it in Home Assistant if the device is lost.
