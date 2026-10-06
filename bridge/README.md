# CYD Home Web Bridge

This small local server is the secure connection between CYD Home Hub and Home Assistant. It is the reason the CYD does **not** need your Home Assistant or eWeLink login.

1. Install Node.js 18 or newer on a computer, Raspberry Pi, or NAS that stays on your home network.
2. Copy `config.example.json` to `config.json`.
3. In `config.json`, add your Home Assistant address, a long-lived Home Assistant token, and a private `cydKey`.
4. Add the Sonoff/eWeLink integration to Home Assistant once. Its devices appear as Home Assistant entities.
5. Run `npm start` from this folder.
6. Find the computer's local IP address. In CYD Home Hub: **Settings → Web Bridge**, enter `http://YOUR_COMPUTER_IP:8787` and the same `cydKey`.

The bridge offers only three endpoints: status, list supported devices, and toggle a listed device. It never sends your Home Assistant token to the CYD.

Keep this service on your local network. Do not expose port 8787 directly to the internet.
