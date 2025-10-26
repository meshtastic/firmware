CHANGES
=======

Date: 2025-10-26
Branch: develop

Summary
-------
This document lists local edits made in this workspace (compact), why they were changed, and suggested small, focused git commit messages with commands you can run to commit them.

Changed files (one-line notes)
-------------------------------
- platformio.ini
  - Change default_envs to `heltec-v3` so the heltec variant is the default build target for local builds.

- variants/esp32s3/heltec_v3/platformio.ini
  - Add clarifying comments for IDEs/parsers and set `monitor_speed` for convenience.

- .github/copilot-instructions.md
  - Add a large guidance file for AI/code-assistant usage and repository onboarding.

- src/input/ButtonThread.h
  - Add `privateChannel` config, `_lastSendMs` and `_channelIndex` fields to support private-channel button sends and rate-limiting.

- src/input/ButtonThread.cpp
  - Implement `sendMessageForPinWithChannel()` helper, create bounded payloads, set portnum, disable ACKs for button-originated UI messages, add ASCII+hex logging before sending, and add a 300ms cooldown to avoid rapid-send crashes.

- src/main.cpp
  - Add optional GPIO ButtonThread globals and setup wiring for three optional GPIO buttons (default safer pins 5/6/7), plus a USERPREFS_TZ_STRING guard to avoid build-time missing-define issues.

Why these changes
-----------------
- Allow quick local development for Heltec V3 boards without editing the top-level build target.
- Add per-button private-channel text messaging (avoids public channel 0), improved debug output, and protections against rapid button bursts that previously caused monitor/device instability.
- Provide a Copilot guidance file to help AI agents and maintainers understand repository conventions.

Suggested commits
-----------------
Split the work into small, reviewable commits. Below are suggested commit messages and example git commands.

1) Commit: configuration - make heltec the default env

Title:
  config: set default_envs to heltec-v3

Description:
  Change top-level `platformio.ini` to use `heltec-v3` as the default build environment for local development.

Files:
  - platformio.ini

Commands:
```bash
git add platformio.ini
git commit -m "config: set default_envs to heltec-v3"
```

2) Commit: variant - clarify heltec variant INI

Title:
  variants: add comments and monitor_speed to heltec_v3 platformio.ini

Description:
  Add explanatory comments to avoid IDE/inspector warnings and set `monitor_speed` for convenience.

Files:
  - variants/esp32s3/heltec_v3/platformio.ini

Commands:
```bash
git add variants/esp32s3/heltec_v3/platformio.ini
git commit -m "variants(heltec_v3): add comments and monitor_speed for IDEs"
```

3) Commit: docs - add copilot instructions

Title:
  docs: add Copilot instructions for repository onboarding

Description:
  Add `.github/copilot-instructions.md` to provide guidance to automated assistants and maintainers. Large documentation file.

Files:
  - .github/copilot-instructions.md

Commands:
```bash
git add .github/copilot-instructions.md
git commit -m "docs: add copilot instructions for repo onboarding"
```

4) Commit: input - ButtonThread private-channel and cooldown support

Title:
  input: add privateChannel, cooldown and _lastSendMs to ButtonThread

Description:
  Extend `ButtonConfig` with `privateChannel` and add `_lastSendMs` / `_channelIndex` fields to `ButtonThread` to support per-button private-channel sends and rate limiting.

Files:
  - src/input/ButtonThread.h

Commands:
```bash
git add src/input/ButtonThread.h
git commit -m "input: add ButtonConfig.privateChannel and cooldown state for ButtonThread"
```

5) Commit: input - implement button-initiated mesh sends (safe, best-effort)

Title:
  input: implement sendMessageForPinWithChannel and safe button sends

Description:
  Add `sendMessageForPinWithChannel()` to compose a bounded TEXT payload, set `decoded.portnum`, disable ACKs for UI button messages, add ASCII+hex debug logging before queuing, and call `service->sendToMesh()`; also add a 300ms per-button cooldown in `runOnce()`.

Files:
  - src/input/ButtonThread.cpp

Commands:
```bash
git add src/input/ButtonThread.cpp
git commit -m "input: send button text on private channel (bounded payload, no-ACK, cooldown, debug logs)"
```

6) Commit: main - wire optional GPIO button threads

Title:
  main: add optional GPIO button threads and safer default pins

Description:
  Add globals and setup wiring in `setup()` for three optional GPIO button threads (Gpio1/2/3), defaulting to safe pins (5/6/7) when platform-specific GPIO macros aren't present. Also add a guard for `USERPREFS_TZ_STRING` to avoid build errors when the build flag isn't injected.

Files:
  - src/main.cpp

Commands:
```bash
git add src/main.cpp
git commit -m "main: wire optional GPIO button threads (defaults and TZ guard)"
```

One-liner commit alternative
---------------------------
If you prefer a single commit that captures everything, use:

```bash
git add platformio.ini variants/esp32s3/heltec_v3/platformio.ini .github/copilot-instructions.md src/input/ButtonThread.h src/input/ButtonThread.cpp src/main.cpp
git commit -m "feat(buttons): add per-button private-channel sends, cooldown and wiring; update heltec default env; add copilot docs"
```

Verification notes
------------------
- After committing, run the PlatformIO build task to confirm compilation succeeds.
- Flash the device and test quick multi-button presses while monitoring serial to confirm stability. If crashes/stack-traces persist, capture the serial output and paste here for analysis.

If you want
-----------
- I can create the actual git commits for you (if you want me to run the git commands in this environment). Tell me which commit strategy you prefer (one commit vs multiple focused commits).
- I can also add a short entry to `README.md` explaining the three GPIO buttons and private channel defaults.

End of file.
