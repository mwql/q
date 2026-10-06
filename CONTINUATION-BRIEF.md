# CYD Home Hub V2 — Continuation Brief

Give this document and the complete project folder to the next AI. The request is to finish, test, polish, and publish the CYD Home Hub V2 for a **CYD-2432S028 / ESP32-2432S028R** with a display marked **TPM408-2.8**.

## Current release status — do not overclaim

This is a **V2 preview**, not a fully hardware-validated final release.

What has been verified on the actual board:

- An earlier small display test rendered correctly using the exact display/touch configuration below. It displayed `CYD HOME`, `Display is working`, `TPM408-2.8 / ILI9341`, and a white/light-blue test screen.
- Previous generic CYD configurations caused a black screen, color stripes, or unusable UI. The configuration below is the known-good basis and must be preserved unless an actual physical test proves a specific change is necessary.

What has been verified on the computer:

- `python -m platformio run` completed successfully for the V2 source.
- The V2 firmware app is 1,238,720 bytes and uses approximately 64.8% of the app partition.
- `bridge/bridge.js` passes Node syntax checking.
- `web/manifest.json` is valid JSON and lists all required ESP32 flash parts at correct offsets.

What is **not** yet verified on the actual V2 firmware:

- Home screen app layout and touch hit areas.
- Wi-Fi scan, touch keyboard, password entry, and connection.
- Screen sleep after 10 seconds and wake-on-first-touch.
- Home Assistant Web Bridge connection, entity loading, and toggling.
- BLE scan and IR output.

Never claim those features are working until they have been flashed to this physical CYD and tested in that order.

## Hardware — exact known-good configuration

Target device:

- ESP32-2432S028R / CYD with a 2.8-inch TPM408-2.8 display.
- ESP32 WROOM, 4 MB flash, no PSRAM assumed.
- Logical display is landscape: 320 × 240, `setRotation(1)`.

Display driver and pins:

| Item | Required value |
| --- | --- |
| TFT driver | `ILI9341_2_DRIVER` |
| MOSI | GPIO 13 |
| SCLK | GPIO 14 |
| CS | GPIO 15 |
| DC | GPIO 2 |
| Reset | GPIO 12 |
| Backlight | GPIO 21, active `HIGH` |
| SPI frequency | 65 MHz |
| SPI read frequency | 20 MHz |
| Display inversion | ON (`TFT_INVERSION_ON`) |
| Rotation | `tft.setRotation(1)` |
| Colour byte order | `tft.setSwapBytes(true)` |

Touch controller:

| Item | Required value |
| --- | --- |
| Controller | resistive XPT2046 |
| Library | Bodmer `TFT_Touch`, not TFT_eSPI touch |
| DOUT | GPIO 39 |
| DIN | GPIO 32 |
| CS | GPIO 33 |
| Clock | GPIO 25 |
| Constructor | `TFT_Touch touch(33, 25, 32, 39);` |
| Calibration | `touch.setCal(526, 3443, 750, 3377, 320, 240, 1);` |

Other hardware:

- IR output uses GPIO 27. It needs an external transistor/resistor/IR LED circuit; do not drive a strong IR LED directly from the ESP32 pin.

Important implementation rule: TFT_eSPI may emit `TOUCH_CS pin not defined`. That warning is expected because touch deliberately uses the separate `TFT_Touch` library. Do **not** "fix" that by switching touch implementations or changing the working display pins.

Useful prior-art references:

- https://github.com/moomdate/CYD-Resource-Monitor — close PlatformIO/TFT_Touch reference.
- https://github.com/edemko/esp32-2432s028r-idf-clock — related board pin reference.

## Repository layout and responsibilities

```text
platformio.ini                    # exact PlatformIO board/libraries/build flags
partitions.csv                    # dual OTA-compatible 4 MB partition table
src/board_config.h                # dimensions, touch pins, IR pin, 10s default sleep
src/theme.h                       # Ocean and Midnight dark/light-blue themes
src/home_hub.cpp                  # entire ESP32 V2 firmware
bridge/bridge.js                  # LAN-only Node.js Home Assistant bridge
bridge/config.example.json        # token/key configuration template
bridge/README.md                  # bridge installation instructions
web/index.html                    # static browser installer page
web/manifest.json                 # ESP Web Tools flashing offsets
web/firmware/*                    # bootloader, partitions, boot_app0, firmware app
.github/workflows/publish-installer.yml  # builds then deploys web/ to GitHub Pages
README.md                         # needs a V2 documentation pass
RELEASE-NOTES.md                  # current preview caveats and basic quick start
CONTINUATION-BRIEF.md             # this file
```

Legacy source files such as `src/main.cpp` and `src/display_check.cpp` were intentionally removed. `platformio.ini` compiles only `src/home_hub.cpp` via `build_src_filter`. Do not re-add old display experiments to the build.

## Current V2 firmware design

`src/home_hub.cpp` is a single-file Arduino application with internal state for pages, Wi-Fi, Web Bridge, BLE, IR, settings, touchscreen, and display sleeping.

Pages currently implemented:

1. **Home** — dark launcher with app cards.
2. **Devices** — fetches Home Assistant entities through the bridge and toggles them.
3. **Wi-Fi** — scans nearby networks; selecting one opens the touch keyboard for its password; joining saves Wi-Fi in ESP32 NVS.
4. **Keyboard** — on-screen lower/upper/symbol keyboard used for Wi-Fi password, bridge URL, bridge key, and IR code.
5. **Bluetooth** — NimBLE scan and a short discovered-device list.
6. **Infrared** — sends a user-set NEC hexadecimal code on GPIO 27.
7. **Settings** — theme, sleep time, timezone, Web Bridge edit screen, and Forget Wi-Fi.
8. **Web Bridge** — bridge settings/status flow.

Settings stored in ESP32 `Preferences` namespace `cydhub2`:

- `theme`: Ocean or Midnight.
- `sleep`: one of 10 sec, 30 sec, 60 sec, or never.
- `utc`: timezone offset in hours, -12 to +14.
- `bridgeUrl`: LAN bridge URL, for example `http://192.168.1.50:8787`.
- `bridgeKey`: optional LAN key.
- `irCode`: NEC hex code, default `20DF10EF`.

Screen sleep behavior:

- Backlight GPIO 21 switches off after the selected no-touch period (default 10 seconds).
- The first valid touch wakes the screen only. It must not also activate a button.
- Do not break this safety behavior while refactoring touch code.

Wi-Fi behavior:

- `scanWifi()` gathers nearby SSIDs and removes duplicates (maximum 12 shown).
- Password is entered on the display. There is intentionally no Wi-Fi web portal.
- `WiFi.begin(ssid, password)` stores the credentials in ESP32 Wi-Fi NVS, so later boots reconnect using `WiFi.begin()`.
- The connection times out after 18 seconds in current code. Improve feedback/retry UX only after testing.
- NTP starts once connected using `pool.ntp.org` and `time.nist.gov` plus user-configured UTC offset.

Theme direction:

- User explicitly rejected grey/bright blue/pink test-like screens.
- Keep the UI genuinely dark with white text and restrained light-blue accents.
- Do not add pink, rainbow colors, large blank regions, or generic web-page styling.
- Make each screen feel like a small appliance app: obvious status, one main action, touch targets at least roughly 34–40 pixels tall, no tiny text.

## Home Assistant / Sonoff / eWeLink architecture

The CYD must **not** log directly into the user’s eWeLink account. Vendor OAuth/login on an ESP32 is insecure and unreliable. The intended secure architecture is:

```text
Sonoff/eWeLink device
        ↓  (official Home Assistant integration, configured once in a browser)
Home Assistant
        ↓  (REST API token stays only on LAN server)
Node.js CYD Home Bridge on the same LAN
        ↓  (small JSON API and optional bridge key)
CYD ESP32 touchscreen
```

The user therefore only uses the CYD to choose Wi-Fi and set the **local bridge address/key**. They do not enter Home Assistant/eWeLink credentials on the CYD.

The existing bridge is in `bridge/bridge.js` and runs under Node.js 18 or later. It has no npm dependencies. The bridge configuration file must be copied locally from `config.example.json` to `config.json`; `config.json` must stay private and is ignored by Git.

Configuration fields:

```json
{
  "port": 8787,
  "homeAssistantUrl": "http://homeassistant.local:8123",
  "homeAssistantToken": "PASTE_A_HOME_ASSISTANT_LONG_LIVED_TOKEN_HERE",
  "cydKey": "CHOOSE_A_PRIVATE_LOCAL_KEY",
  "allowedOrigins": []
}
```

Current bridge API:

| Request | Purpose |
| --- | --- |
| `GET /api/v1/status` | Connection/status check |
| `GET /api/v1/devices` | Return up to 20 `switch`, `light`, `fan`, or `cover` entities |
| `POST /api/v1/devices/<entity_id>/toggle` | Turn an entity on/off or open/close a cover |

- If `cydKey` is set, the CYD sends it as `X-CYD-Key`.
- The Home Assistant long-lived token never goes to the CYD and must never go into GitHub or `web/`.
- The bridge restricts IDs to safe characters and maps services according to entity domain.
- It is intended for the private LAN only. Do not expose port 8787 publicly or open it to the internet.
- `allowedOrigins` exists in the example but is not currently implemented by `bridge.js`; either implement it correctly or remove it from the template/documentation.

The next AI should add a simple password-protected browser configuration page to the local bridge if the user wants no manual `config.json` editing. It should still never expose the Home Assistant token to the CYD firmware or GitHub Pages.

## Browser installer and GitHub Pages — critical flashing information

The browser installer is a static page only. It flashes firmware with Web Serial; it cannot itself run the Home Assistant bridge.

`web/manifest.json` must contain exactly these ESP32 parts and offsets:

| File | Offset |
| --- | --- |
| `firmware/bootloader.bin` | `0x1000` / 4096 |
| `firmware/partitions.bin` | `0x8000` / 32768 |
| `firmware/boot_app0.bin` | `0xE000` / 57344 |
| `firmware/cyd-home.bin` | `0x10000` / 65536 |

Do **not** flash only `firmware.bin` at address `0x0`. An earlier release did that and caused a blank screen/boot failure. Also do not leave stale installer files in the GitHub repository.

The GitHub workflow compiles PlatformIO then copies all four output files into `web/firmware/` before publishing `web/` to GitHub Pages. The Pages source must be **GitHub Actions**, not a branch. After replacing the repository contents, wait for a green Actions build before using the installer page.

Recommended physical flashing flow:

1. Use current Chrome or Edge on desktop/laptop (Web Serial support required).
2. Connect the CYD by data-capable USB cable.
3. Open the published GitHub Pages installer and choose Install/Connect.
4. If the serial connection cannot enter bootloader, hold **BOOT**, tap **RESET**, then release **BOOT** and retry.
5. Allow erase when prompted because V2 changed partitions/settings.
6. After flash completes, tap RESET if it does not reboot.

Current built app SHA-256 (for checking accidental stale releases):

```text
5FFA539655666E471859667A862DDA28334992E8BA13F63D3BF85434CA9F6B88
```
*(Updated: includes LOAD_FONT2, LOAD_FONT4, LOAD_FONT6, LOAD_FONT7, and LOAD_GFXFF for full UI button, label, and Wi-Fi text rendering).*

## Required first test pass on the real CYD

Do these tests one at a time. Capture serial output and a photo/video of every screen if possible.

1. Flash only using the four-part manifest described above; do not use an old cached GitHub Pages site.
2. Confirm boot: dark launcher, sharp readable title, no stripes, no blank backlight, correct landscape orientation.
3. Tap every home card and Back button. Confirm the real touch location matches the drawn button.
4. Wait 10 seconds, confirm the backlight turns off, then touch once. Confirm the first touch wakes only and the second can activate a control.
5. Wi-Fi: scan, choose a 2.4 GHz network, enter password using all keyboard modes/backspace/space, join, then reboot to verify reconnect.
6. Set timezone and confirm clock once Wi-Fi is connected.
7. On a LAN machine, configure and run the bridge. In Settings set the **IP address** URL (not a `.local` name unless it is confirmed to work) and optional key. Test bridge status.
8. Devices: confirm entity list names/states, toggle a light/switch/fan, and operate a cover safely. Confirm UI refreshes correct state.
9. BLE: scan, verify no crash and discovered names can scroll/refresh.
10. IR: connect an appropriate IR transistor/LED circuit and test a known NEC code. Keep it optional if hardware is absent.
11. Reboot several times; confirm theme/settings/Wi-Fi persist.

Fix only the actual observed failure, then rebuild, flash, and repeat the affected test. Do not replace the known-good display setup just to make a guessed generic CYD config "cleaner."

## Immediate next engineering work

1. Test the preview physically before large redesigns; correct any actual touch/render/wake bug.
2. Update `README.md` and `web/index.html` from old V1 language to precise V2 preview, hardware, bridge, and flashing instructions.
3. Polish the UI after it works: shared navigation, connected/disconnected labels, clear empty/error/loading states, no clipped strings, responsive device list pages, and a visible firmware version.
4. Improve bridge configuration for the nontechnical user with a LAN-only authenticated setup page, token validation, and entity selection/favorites. Do not put setup credentials in browser installer source or the ESP32.
5. Improve entity model: display device icons/types and use HA service calls safely; potentially add brightness/cover-position controls after basic toggle reliability is confirmed.
6. Consider adding OTA update only after the stable installer and recovery procedure have been tested. The partitions support OTA, but no OTA UI is implemented yet.
7. Keep browser installer deploy workflow reproducible and ensure the published site always receives the current four binary files.

## Build commands and expected warnings

From project root:

```powershell
python -m platformio run
node --check bridge\bridge.js
python -m json.tool web\manifest.json
```

Expected nonfatal build warnings:

- TFT_eSPI may say `TOUCH_CS pin not defined`; expected because the project uses standalone `TFT_Touch`.
- IRremoteESP8266 can warn about disabled protocols; the firmware intentionally keeps NEC send only to fit comfortably in 4 MB flash.

The known successful V2 build reported about 59,204 bytes RAM (18.1%) and 1,232,149 bytes flash (64.8%) before the packaging copy. Watch size closely; do not add many large fonts/images/libraries without rebuilding.

## User expectations and non-negotiable constraints

- This must be an on-device **app-like launcher**, not a webpage served by the CYD.
- It must use dark shades, white text, and light-blue accents—not grey/pink/rainbow test colors.
- Wi-Fi setup must be usable by touchscreen: scan, choose SSID, type password, join.
- User wants Sonoff/eWeLink device control without logging into eWeLink on the CYD. Home Assistant plus the LAN bridge is the chosen implementation.
- Settings must be functional—not decorative—including theme, sleep time, Wi-Fi forget/reconnect, timezone, bridge setup, and device refresh.
- Screen must not remain permanently off: screen sleep needs reliable touch wake.
- Upload must be easy from Chrome/Edge through GitHub Pages, similar in spirit to Bruce Flasher. It must use the correct four-part manifest.
- Be candid. If a feature is source-complete but not physically verified, state that it needs device testing.
