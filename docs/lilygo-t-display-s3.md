# VibePulse Mini on LilyGO T-Display-S3

This fork adds an explicit `TORGET_TDISPLAY_S3` build target for the regular
T-Display-S3. It is a compact, landscape 320 x 170 VibePulse view with the
same screens as the original 480 x 480 panel:

1. Claude model week
2. Claude all-models week
3. Codex week
4. Grok today/month tokens
5. Burn rate (Claude, Codex, Grok)
6. Max Tracker (Claude)
7. Max Tracker (Codex)
8. Max Tracker (Grok)
9. GitHub
10. Value multiple

NEEDS YOU / DONE / ERROR take over the whole 320 x 170 glass; a short press
dismisses them. GPIO14 pages forward; GPIO0 (BOOT) pages back after boot.
Long-press a quota page for session detail.

## One-click install

You do not need ESP-IDF on the computer that flashes the board.

1. Open the [web installer](https://hardcoreerik.github.io/VibePulse-T-Display-S3/).
2. Enter the 2.4 GHz Wi-Fi name and password, and the token server URL
   (`http://<this-pc-lan-ip>:8737`).
3. Hold **BOOT**, tap **RST**, release **BOOT**, then click Connect and install.
   The board is on COM3 on the maintainer desk; other machines will show a
   different port in the browser picker.
4. On the PC, start `python tools/tokenserver/tokenserver.py`.

The installer patches Wi-Fi and the token-server URL into a factory firmware
image in the browser. No `secrets.h` is required for that path. A local
`secrets.h` still wins when you build from source.

The board wiring and display behaviour follow LilyGO's official
[T-Display-S3 Quick Start](https://wiki.lilygo.cc/products/t-display-series/t-display-s3/quick-start.html)
and the known-working local NEONDRIVE T-Display-S3 target: ST7789V over its
8-bit parallel bus, GPIO15 panel power, GPIO38 backlight, and landscape
320 x 170 LVGL coordinates.

The first release intentionally does not use the optional TF Shield. The
screen receives current, privacy-filtered data from the local token service;
the SD card is reserved for a later, explicitly designed local-history/export
feature rather than becoming a dependency for normal operation.

## Account connection model

There is no account login on the display and no account credential is placed
in firmware, on the SD card, or on the LAN payload.

1. On the Windows computer that will run the service, sign in normally to
   Claude Code and/or Codex. Those tools keep their own local sessions and
   logs under your Windows user profile.
2. Start `py tools\\tokenserver\\tokenserver.py` from this repository. It
   reads only local usage/activity metadata and publishes percentages, reset
   times, project basenames, model names, and coarse states to your LAN.
3. Set `TK_VIBEPULSE_BASE_URL` in your untracked `secrets.h` to that Windows
   computer's LAN IPv4 address or hostname, then build and flash the device.

Codex week usage and agent state are read from Codex's local session data.
Claude activity and token volume come from Claude Code's local logs. On
Windows, live Claude 5-hour/weekly quota works automatically once step 1's
`claude login` has run: the token service reads Claude Code's own local
session file (`%USERPROFILE%\.claude\.credentials.json`, the same store
`claude login` already writes to, with no separate consent step) and makes
Anthropic's read-only usage request with it. `VIBEPULSE_CLAUDE_OAUTH_TOKEN`
remains available as a private, process-local override for edge cases (a
second account, a machine where that file can't be read); if set, it takes
priority. Never paste either value into chat, `secrets.h`, a source file, or
the display. The service uses the token only for that one read-only request
and never exposes it to the ESP32. See
`tools/tokenserver/README.md` ("Live Claude quota on Windows") for the full
setup, security properties, and the manual-override fallback.

## Build prerequisites

Install ESP-IDF 5.5.x with ESP32-S3 support, open its ESP-IDF command prompt,
then from this repository:

```powershell
Copy-Item secrets.h.example secrets.h
# edit Wi-Fi and TK_VIBEPULSE_BASE_URL in secrets.h
idf.py -D TORGET_TDISPLAY_S3=ON set-target esp32s3
idf.py -D TORGET_TDISPLAY_S3=ON build
```

The target uses 16 MB flash and 8 MB OPI PSRAM, as specified by LilyGO.
LilyGO documents 921600 as its standard upload speed. The project can use a
different speed only after the normal build and cable have proved stable.
It preserves the original upstream board target when the option is omitted.

## Flashing

Do not flash until the build succeeds and you have explicitly approved the
install. When ready, use COM5:

```powershell
idf.py -D TORGET_TDISPLAY_S3=ON -p COM5 flash monitor
```

If COM5 is not accepted, hold **BOOT**, tap **RST**, release **BOOT**, then
run the command again.
