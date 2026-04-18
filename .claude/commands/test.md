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

3. **On pass**: one-line summary of the form `N passed, M skipped in <duration>`. Don't enumerate the 52 test names — the user can read those. Do mention if any test was SKIPPED for a NON-placeholder reason (e.g. "role not present on hub" is worth flagging).

4. **On failure**: for every FAILED test, open `mcp-server/tests/report.html` and extract the `Meshtastic debug` section for that test. pytest-html embeds the firmware log stream + device state dump there; the 200-line firmware log tail is usually enough to explain the failure. Summarise: which test, one-line assertion message, the firmware log lines that matter (things like `PKI_UNKNOWN_PUBKEY`, `Skip send NodeInfo`, `Error=`, `Guru Meditation`, `assertion failed`).

5. **Classify the failure** as one of:
   - **Transient/flake**: LoRa collision, timing-sensitive assertion, first-attempt NAK + successful retry pattern. Propose `/repro <test_node_id>` to confirm.
   - **Environmental**: device unreachable, port busy, CP2102 driver wedged. Suggest the specific recovery (replug USB, `touch_1200bps`, check `git status userPrefs.jsonc`).
   - **Regression**: same assertion fails repeatedly, firmware log shows a new/unusual error. Surface the diff between expected and observed, identify the module likely responsible.

6. **Never run destructive recovery automatically.** If a failure looks like it needs a reflash, factory*reset, or USB replug, \_describe what to do* — don't execute. The operator decides.

## Arguments handling

- No args → wrapper's defaults (full suite).
- `$ARGUMENTS` passed verbatim to the wrapper, which passes them to pytest.
- Common operator invocations: `/test tests/mesh`, `/test tests/mesh/test_direct_with_ack.py::test_direct_with_ack_roundtrip`, `/test --force-bake`, `/test -k telemetry`.

## Side-effects to mention in summary

- The session fixture snapshots `userPrefs.jsonc` at session start and restores at teardown (plus on `atexit`). After a clean run, `git status userPrefs.jsonc` should be empty. If the wrapper's pre-flight printed a warning about a stale sidecar, call that out — means a prior session crashed.
- `mcp-server/tests/report.html` and `junit.xml` are regenerated on every run; the HTML is self-contained (shareable).
