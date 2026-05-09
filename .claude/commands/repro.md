---
description: Re-run a specific test N times in isolation to triage flakes, diff firmware logs between passes and failures
argument-hint: <test-node-id> [count=5]
---

<!-- markdownlint-disable MD029 -->

# `/repro` — flakiness triage for one test

Re-run a single pytest node ID N times in isolation, track pass rate, and surface what's _different_ in the firmware logs between the passing attempts and the failing ones. Turns "it's flaky, I guess" into "it fails when X, passes when Y."

## What to do

1. **Parse `$ARGUMENTS`**: first token is the pytest node id (e.g. `tests/mesh/test_direct_with_ack.py::test_direct_with_ack_roundtrip[nrf52->esp32s3]`); second token is an integer count (default `5`, cap at `20`). If the first token doesn't look like a test path (no `::` and no `tests/` prefix), treat the whole `$ARGUMENTS` as a `-k` filter instead.

2. **Sanity-check the hub first** (so we're not measuring "nothing plugged in" N times): call `mcp__meshtastic__list_devices`. If the test name contains `nrf52` or `esp32s3` and the matching VID isn't present, stop and report — re-running won't help.

3. **Loop N times**. For each iteration:

   ```bash
   ./mcp-server/run-tests.sh <test-id> --tb=short -p no:cacheprovider
   ```

   Capture: exit code, duration, and (on failure) the `Meshtastic debug` firmware log section from `mcp-server/tests/report.html`. `-p no:cacheprovider` suppresses pytest's `.pytest_cache` writes so iterations don't influence each other.

4. **Track a small structured tally**:

   ```text
   attempt 1: PASS (42s)
   attempt 2: FAIL (128s)  ← firmware log 200-line tail captured
   attempt 3: PASS (39s)
   attempt 4: FAIL (121s)
   attempt 5: PASS (41s)
   --------------------------------------
   pass rate: 3/5 (60%)   |   mean duration: 74s
   ```

5. **On mixed outcomes**: diff the firmware log tails between a representative passing attempt and a representative failing attempt. Focus on:
   - Error-level lines only present in failures (`PKI_UNKNOWN_PUBKEY`, `Alloc an err=`, `Skip send`, `No suitable channel`)
   - Timing around the assertion event — did a broadcast go out, was there an ACK, did NAK fire?
   - Device state fields that changed (nodesByNum entries, region/preset, channel_num)

   Surface the top 3 differences as a "passes when / fails when" table. Don't dump full logs — pull specific lines with uptime timestamps.

5a. **Archive recorder slices per attempt** (no extra device interaction; the recorder runs autouse). Right after each attempt finishes, capture its `(start_ts, end_ts)` and call `mcp__meshtastic__recorder_export(start=<start>, end=<end>, dest_dir="mcp-server/tests/repro_artifacts/<safe-test-id>/attempt_<n>/")`. This drops a `logs.jsonl`, `telemetry.jsonl`, `packets.jsonl`, and `events.jsonl` snapshot scoped to the attempt window. Use these for cross-attempt diffs in step 5: `jq '.line' logs.jsonl` is faster than re-running the test, and the telemetry slice lets you compare heap behavior across attempts.

6. **Classify the flake** into one of:
   - **LoRa airtime collision** → pass rate improves with fewer concurrent transmitters; propose a `time.sleep` gap or retry bump in the test body.
   - **PKI key staleness** → fails on first attempt, passes after self-heal; existing retry loop in `test_direct_with_ack.py` handles this.
   - **NodeInfo cooldown** → `Skip send NodeInfo since we sent it <600s ago` in fail-only logs; needs `broadcast_nodeinfo_ping()` warmup.
   - **Hardware-specific** (one direction fails, other passes; one device's firmware is older; driver wedged) → specific recovery pointer. For a device that's wedged past `touch_1200bps`, the next escalation is `uhubctl_cycle(role=..., confirm=True)` to hard-power-cycle its hub port (requires `uhubctl` installed).
   - **Device went dark mid-run** → fails from some attempt onward, never recovers, firmware log stops arriving. Almost always hardware: a Guru crash + frozen CDC. Hard-power-cycle via `uhubctl_cycle(role=..., confirm=True)` before the next iteration; if that also fails, escalate to replug.
   - **Genuinely unknown** → say so; don't invent a root cause.

7. **Report back** with:
   - Pass rate and mean duration.
   - Classification + evidence (the specific log lines that support it).
   - A suggested next step (re-run with specific args, open `/diagnose`, edit a specific test file, nothing).

## Examples

- `/repro tests/mesh/test_direct_with_ack.py::test_direct_with_ack_roundtrip[esp32s3->nrf52] 10` — runs 10 times, diffs firmware logs.
- `/repro broadcast_delivers` — no `::`, no `tests/`, so interpreted as `-k broadcast_delivers`; runs every matching test the default 5 times.
- `/repro tests/telemetry/test_device_telemetry_broadcast.py 3` — shorter run for a slow test.

## Constraints

- Don't exceed `count=20` per invocation — airtime and USB wear add up. If the user asks for 50, negotiate down.
- Don't rebuild firmware as part of triage; flakes that only reproduce under different firmware belong in a separate session.
- If the FIRST attempt fails AND the rest all pass, that's a classic "state leak from a prior test" → say so and suggest running with `--force-bake` or starting from a clean state rather than chasing the first failure.
