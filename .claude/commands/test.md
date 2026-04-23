---
description: Run the mcp-server test suite (auto-detects devices) and interpret the results
argument-hint: [pytest-args]
---

# `/test` — mcp-server test runner with interpretation

Run `mcp-server/run-tests.sh` and make sense of the output so the operator doesn't have to.

## What to do

1. **Invoke the wrapper.** From the firmware repo root, run:

   ```bash
   ./mcp-server/run-tests.sh $ARGUMENTS
   ```

   The wrapper auto-detects connected Meshtastic devices, maps each to its PlatformIO env, exports the required `MESHTASTIC_MCP_ENV_*` env vars, and invokes pytest. If the user passed no arguments, the wrapper supplies a sensible default set (`tests/ --html=tests/report.html --self-contained-html --junitxml=tests/junit.xml -v --tb=short`). A `--report-log=tests/reportlog.jsonl` arg is always appended (unless the operator passed their own). `--assume-baked` is deliberately NOT in the defaults — `test_00_bake.py` has its own skip-if-already-baked check and runs the ~8 s verification by default. Operators can opt into the fast path with `--assume-baked`, or force a reflash with `--force-bake`.

2. **Read the pre-flight header.** First ~6 lines print the detected hub (role → port → env). If that line reads `detected hub : (none)`, the wrapper will narrow to `tests/unit` only — say so explicitly in your summary so the operator knows hardware tiers were skipped.

3. **On pass**: one-line summary of the form `N passed, M skipped in <duration>`. Don't enumerate the test names — the user can read those. Do mention any SKIPPED tests and name the cause:
   - `"role not present on hub"` → device unplugged; operator knows to reconnect.
   - `"firmware not baked with USERPREFS_UI_TEST_LOG"` → tests/ui skipped because the macro isn't in firmware yet; suggest `--force-bake`.
   - `"uhubctl not installed"` → tests/recovery + peer-offline skipped; suggest `brew install uhubctl` / `apt install uhubctl`.
   - `"no PPPS-capable hubs detected"` → tests/recovery skipped because the hub doesn't support per-port power; the tier will never run on that setup.
   - `"opencv-python-headless is not installed"` → tests/ui auto-deselected by run-tests.sh; suggest `pip install -e 'mcp-server/.[ui]'`.

4. **On failure**: for every FAILED test, open `mcp-server/tests/report.html` and extract the `Meshtastic debug` section for that test. pytest-html embeds the firmware log stream + device state dump there; the 200-line firmware log tail is usually enough to explain the failure. Summarise: which test, one-line assertion message, the firmware log lines that matter (things like `PKI_UNKNOWN_PUBKEY`, `Skip send NodeInfo`, `Error=`, `Guru Meditation`, `assertion failed`). For UI-tier failures also glance at `mcp-server/tests/ui_captures/<session>/<test>/transcript.md` — it records each step's frame + OCR.

5. **Classify the failure** as one of:
   - **Transient/flake**: LoRa collision, timing-sensitive assertion, first-attempt NAK + successful retry pattern. Propose `/repro <test_node_id>` to confirm.
   - **Environmental**: device unreachable, port busy, CP2102 driver wedged. Suggest the specific recovery in escalation order: (a) replug USB, (b) `touch_1200bps(port=...)` + `pio_flash` for nRF52 DFU, (c) `uhubctl_cycle(role="nrf52", confirm=True)` when a device is fully wedged past DFU (needs `uhubctl` installed — `baked_single`'s auto-recovery hook does this once automatically). Also check `git status userPrefs.jsonc`.
   - **Regression**: same assertion fails repeatedly, firmware log shows a new/unusual error. Surface the diff between expected and observed, identify the module likely responsible.

6. **Never run destructive recovery automatically.** If a failure looks like it needs a reflash, factory*reset, `uhubctl_cycle`, or USB replug, \_describe what to do* — don't execute. The operator decides.

## Arguments handling

- No args → wrapper's defaults (full suite).
- `$ARGUMENTS` passed verbatim to the wrapper, which passes them to pytest.
- Common operator invocations: `/test tests/mesh`, `/test tests/mesh/test_direct_with_ack.py::test_direct_with_ack_roundtrip`, `/test --force-bake`, `/test -k telemetry`.

## Side-effects to mention in summary

- The session fixture snapshots `userPrefs.jsonc` at session start and restores at teardown (plus on `atexit`). After a clean run, `git status userPrefs.jsonc` should be empty. If the wrapper's pre-flight printed a warning about a stale sidecar, call that out — means a prior session crashed.
- `mcp-server/tests/report.html` and `junit.xml` are regenerated on every run; the HTML is self-contained (shareable).
