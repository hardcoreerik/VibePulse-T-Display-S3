# Quick setup: Claude usage on your VibePulse display

No accounts, no tokens to type, no secrets in any file. Five steps.

## 1. Sign in to Claude Code on this PC

If you haven't already:

```bash
claude login
```

That's the only "login" step, ever. It's the same sign-in Claude Code
already needs to work at all.

## 2. Start the token server

From the repository root:

```powershell
python tools\tokenserver\tokenserver.py
```

Leave it running. It reads your local Claude Code usage and serves it on
port `8737` (or whatever port you pass with `--port`). It never asks for a
password, token, or API key — it reads what `claude login` already set up
in step 1.

## 3. Point the display at this PC

Copy `secrets.h.example` to `secrets.h` (already gitignored — never commit
it) and fill in:

- your Wi-Fi name and password
- `TK_VIBEPULSE_BASE_URL` set to `http://<this PC's LAN IP>:8737`

Find your LAN IP with `ipconfig` (look for the IPv4 address on your active
network adapter).

## 4. Build and flash

```powershell
idf.py -D TORGET_TDISPLAY_S3=ON set-target esp32s3
idf.py -D TORGET_TDISPLAY_S3=ON build
idf.py -D TORGET_TDISPLAY_S3=ON -p COM<N> flash monitor
```

Replace `COM<N>` with the device's actual serial port (check Device
Manager → Ports).

## 5. Check it worked

```powershell
curl http://localhost:8737/
```

Look for `"claudeProbe": "usage_http_200 + ok"` in the response. That
means live Claude quota is flowing. The device picks it up on its next
poll (every ~30 seconds) and shows real percentages instead of dashes.

## If something's stuck

- **Dashes on the device, forever:** the token server isn't reachable —
  check it's still running and `TK_VIBEPULSE_BASE_URL` in `secrets.h`
  matches this PC's current LAN IP (it changes if your router reassigns
  it).
- **Numbers on the device never change:** the token server process is
  stale — stop it and start it again. A long-running instance won't pick
  up code updates or a fresh login on its own.
- **`claudeProbe` says `no_claude_oauth_token`:** run `claude login` again
  — the session may have expired.
- **Want to run it automatically on login/boot** instead of a manual
  terminal window: see the autostart section in
  [README.md](README.md).

Full details, including exactly what data is read and what "never leaves
this PC" actually means: [README.md](README.md).
