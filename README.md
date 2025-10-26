<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

## Overview

This repository contains the official device firmware for Meshtastic, an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

## Stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")


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

