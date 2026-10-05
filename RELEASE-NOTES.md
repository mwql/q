# CYD Home Hub 1.0.0

This is the release for the TPM408-2.8 / ESP32-2432S028R display that passed the display-verification screen.

## Before flashing

Replace the entire contents of the GitHub repository with this release. In particular, do not retain an old `web/manifest.json` or an old `.github/workflows/publish-installer.yml`; an earlier installer flashed the app at an incorrect offset.

Wait for the GitHub Actions workflow to complete successfully. Use the GitHub Pages installer only after the new workflow run is green.

## After flashing

1. The white-and-light-blue launcher opens in landscape.
2. Tap **Wi-Fi**, then **Setup portal**.
3. Join the `CYD-Home` network on a phone, and open `192.168.4.1`.
4. Enter Wi-Fi credentials, Home Assistant URL, a Home Assistant long-lived token, and optionally an NEC IR code.
5. Tap **Devices** to load supported Home Assistant entities.

The display switches off after ten seconds without touch. The next touch only wakes it; it does not activate an app.
