<div align="left" markdown="1">


## Local development notes (this workspace)

The copy of this repository in this folder contains a few local edits to help test GPIO buttons and a Heltec V3 workflow. These are local changes and are listed in `CHANGES.md`.

Quick build/flash/monitor
--------------------------------
- Build the default environment (this workspace defaults to `heltec-v3`):

```bash
platformio run
```

- Build + upload to the device:

```bash
platformio run -e heltec-v3 -t upload
```

- Serial monitor (115200):

```bash
platformio device monitor -b 115200
```

What was changed (short)
--------------------------------
- `platformio.ini`: changed default_envs to `heltec-v3` so the heltec variant builds by default.
- `variants/esp32s3/heltec_v3/platformio.ini`: small comments and monitor_speed added.
- `.github/copilot-instructions.md`: large guidance file for local AI/assistant use.
- `src/input/ButtonThread.h` / `src/input/ButtonThread.cpp`: support for per-button private-channel sends, disable ACKs for UI button messages, payload bounds, ASCII+hex debug logs, and a 300ms cooldown to avoid rapid-send issues.
- `src/main.cpp`: added wiring for 3 optional GPIO button threads (defaults to safe pins 5/6/7 if GPIO1/2/3 aren't defined) and a small guard for a missing TZ macro.

Button behavior (how to test)
--------------------------------
- Three optional GPIO button threads are available: `Gpio1Button`, `Gpio2Button`, `Gpio3Button`.
- Defaults (if your variant doesn't provide GPIO1/2/3 macros): Gpio1=GPIO5, Gpio2=GPIO6, Gpio3=GPIO7.
- Each press sends a small text message: `Button GPIO<pin> pressed` on private channel indices 1, 2, and 3 respectively (channel 0 is intentionally skipped).
- Messages are sent best-effort (no ACK) and a 300 ms cooldown per button prevents rapid-fire sends.

Beginner git instructions (commit the local changes)
--------------------------------
I created `CHANGES.md` with suggested commit messages. If you want to make the commits locally, here's a minimal workflow:

1. Check what changed:

```bash
git status --short
```

2. Stage files you want to commit (example for ButtonThread):

```bash
git add src/input/ButtonThread.h src/input/ButtonThread.cpp
```

3. Commit with a clear message:

```bash
git commit -m "input: send button text on private channel (bounded payload, no-ACK, cooldown, debug logs)"
```

4. Repeat for other logical changes, or create a single combined commit:

```bash
git add .
git commit -m "feat(buttons): add per-button private-channel sends, cooldown and wiring; update heltec default env; add copilot docs"
```

5. If you want to push to a remote (only if you intend to):

```bash
git push origin develop
```

If you'd like, I can create the commits here for you — tell me whether you prefer multiple focused commits (recommended) or a single combined commit.

