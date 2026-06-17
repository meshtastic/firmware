---
mode: agent
description: Run the mcp-server test suite and interpret results (Copilot equivalent of the Claude Code /test slash command)
---

# `/mcp-test` — mcp-server test runner with interpretation

Equivalent of the Claude Code `/test` slash command in `.claude/commands/test.md`. Use this when the operator asks you to "run the tests", "check the mcp test suite", "run the mesh tests", etc.

## What to do

1. **Invoke the wrapper** from the firmware repo root:

   ```bash
   ./mcp-server/run-tests.sh [pytest-args]
   ```

   If the operator specified a subset (e.g. "just the mesh tests"), pass it through as `tests/mesh` or a pytest `-k filter`. If they said nothing, use the wrapper's defaults (full suite with pytest-html report).

   The wrapper auto-detects connected Meshtastic devices, maps each to its PlatformIO env, exports the required env vars, and invokes pytest. Zero pre-flight config needed from the operator.

2. **Read the pre-flight header** (first few lines of wrapper output). The `detected hub :` line lists role → port → env mappings. If it reads `(none)`, the wrapper narrowed to `tests/unit` only — call that out explicitly so the operator knows hardware tiers were skipped.

3. **On pass**: one-line summary like `N passed, M skipped in <duration>`. Don't enumerate test names. DO mention any non-placeholder SKIPs and name the cause:
   - `"role not present on hub"` → device unplugged; operator should reconnect.
   - `"firmware not baked with USERPREFS_UI_TEST_LOG"` → tests/ui skipped; the UI-log compile macro isn't in the baked firmware. Suggest `--force-bake`.
   - `"uhubctl not installed"` → tests/recovery + `test_peer_offline_recovery` skipped. Suggest `brew install uhubctl` / `apt install uhubctl`.
   - `"no PPPS-capable hubs detected"` → tests/recovery skipped because the attached hub doesn't support per-port power switching; won't run on that setup.
   - `"opencv-python-headless is not installed"` → tests/ui auto-deselected by `run-tests.sh`. Suggest `pip install -e 'mcp-server/.[ui]'`.

4. **On failure**: open `mcp-server/tests/report.html` (pytest-html output, self-contained) and extract the `Meshtastic debug` section for each failed test. That section includes a firmware log stream (last 200 lines) and device state dump. For each failure, summarise:
   - test name
   - one-line assertion message
   - the specific firmware log lines that explain why (look for `PKI_UNKNOWN_PUBKEY`, `Skip send NodeInfo`, `Error=`, `Guru Meditation`, `assertion failed`, `No suitable channel`)
   - for UI-tier failures also check `mcp-server/tests/ui_captures/<session>/<test>/transcript.md` (per-step frame + OCR)

5. **Classify each failure** as one of:
   - **Transient flake** — LoRa collision, first-attempt NAK with self-heal pattern, timing-sensitive assertion. Suggest `/mcp-repro <test-id>` to confirm.
   - **Environmental** — device unreachable, port busy, CP2102 driver wedged on macOS. Suggest recovery in escalation order: (a) replug USB, (b) `touch_1200bps` + `pio_flash` for nRF52 DFU, (c) `uhubctl_cycle(role=..., confirm=True)` for a device wedged past DFU (needs `uhubctl` installed; `baked_single` does this once automatically when available). Also check `git status userPrefs.jsonc`.
   - **Regression** — same assertion fails repeatedly on re-runs, firmware log shows novel errors. Identify the firmware module likely responsible.

6. **Do NOT run destructive recovery automatically**. If a failure looks like it needs a reflash, factory*reset, `uhubctl_cycle`, or replug — \_describe the steps* and let the operator decide. Never burn airtime or flash cycles without approval.

## Arguments convention

Operators generally invoke this prompt either with no arguments (full suite) or with a specific subset. Examples:

- `tests/mesh` — one tier
- `tests/mesh/test_direct_with_ack.py::test_direct_with_ack_roundtrip` — one test
- `--force-bake` — reflash devices first
- `-k telemetry` — name-filter

## Side-effects to confirm in your summary

- `userPrefs.jsonc` should be clean after a successful run. The session fixture in `mcp-server/tests/conftest.py` (`_session_userprefs`) snapshots and restores. Check `git status --porcelain userPrefs.jsonc` and report if it's non-empty.
- `mcp-server/tests/report.html` and `junit.xml` regenerate on every run.
- The wrapper prints a warning if a `.mcp-session-bak` sidecar was left over from a crashed prior session and auto-restores from it — mention that if it happened.
