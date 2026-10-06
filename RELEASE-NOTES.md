# CYD Home Hub 2.0.0 Preview

This is the current V2 source release for the TPM408-2.8 / ESP32-2432S028R display. It builds successfully but needs physical-device verification before being called a final release.

## Before flashing

Replace the entire contents of the GitHub repository with this release. In particular, do not retain an old `web/manifest.json` or an old `.github/workflows/publish-installer.yml`; an earlier installer flashed the app at an incorrect offset.

Wait for the GitHub Actions workflow to complete successfully. Use the GitHub Pages installer only after the new workflow run is green.

## After flashing

1. The dark, light-blue launcher opens in landscape.
2. Tap **Wi-Fi**, then **Scan networks**.
3. Tap a network, use the on-screen keyboard to enter its password, and tap **Join**.
4. In **Settings → Web Bridge**, enter the local bridge URL and optional bridge key.
5. Tap **Devices** to load supported Home Assistant entities through the Web Bridge.

The display switches off after ten seconds without touch. The next touch only wakes it; it does not activate an app.
