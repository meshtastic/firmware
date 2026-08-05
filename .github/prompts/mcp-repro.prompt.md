---
mode: agent
description: Re-run a specific test N times to triage flakes; diff firmware logs between passes and failures (Copilot equivalent of the Claude Code /repro slash command)
---

# `/mcp-repro` — flakiness triage for one test

Equivalent of `.claude/commands/repro.md`. Use when the operator says "that one test is flaky — dig in", "repro the direct_with_ack failure", "why does X sometimes fail?".

## What to do

1. **Parse the operator's input** into two pieces:
   - **Test identifier** — either a pytest node id (has `::` or starts with `tests/`) or a `-k`-style filter (plain substring like `direct_with_ack`).
   - **Count** — integer, default `5`, cap at `20`. If the operator asks for 50, negotiate down and explain (airtime + USB wear).

2. **Sanity-check the hub** via the `list_devices` MCP tool. If the test name references `nrf52` or `esp32s3` and the matching VID isn't present, stop and report — re-running won't help.

3. **Loop** N times. Each iteration:

   ```bash
   ./mcp-server/run-tests.sh <test-id> --tb=short -p no:cacheprovider
   ```

   `-p no:cacheprovider` keeps pytest from caching anything between iterations. Capture: exit code, duration, and (on failure) the `Meshtastic debug` firmware-log section from `mcp-server/tests/report.html`.

4. **Tally** results as you go:

   ```text
   attempt 1: PASS (42s)
   attempt 2: FAIL (128s)    ← fw log captured
   attempt 3: PASS (39s)
   attempt 4: FAIL (121s)
   attempt 5: PASS (41s)
   --------------------------------------------------
   pass rate: 3/5 (60%)  |  mean duration: 74s
   ```

5. **On mixed outcomes, diff the firmware logs** between one representative pass and one representative fail. Focus on:
   - Error-level lines present only in failures (`PKI_UNKNOWN_PUBKEY`, `Alloc an err=`, `Skip send`, `No suitable channel`, `NAK`)
   - Timing around the assertion point (broadcast sent? ACK received? retry fired?)
   - Device-state fields that changed between attempts

   Surface the top 3 differences as a compact "passes when / fails when" table with uptime timestamps. Don't dump full logs.

6. **Classify** the flake into one of:
   - **LoRa airtime collision** — pass rate improves with fewer concurrent transmitters. Suggest a `time.sleep` gap or retry bump in the test body.
   - **PKI key staleness** — first attempt fails, subsequent ones pass; existing retry-loop pattern in `test_direct_with_ack.py` is the fix.
   - **NodeInfo cooldown** — `Skip send NodeInfo since we sent it <600s ago` in fail-only logs; needs a `broadcast_nodeinfo_ping()` warmup.
   - **Hardware-specific** — one direction consistently fails, firmware versions differ, CP2102 driver wedged, etc. For a device wedged past `touch_1200bps`, recommend `uhubctl_cycle(role=..., confirm=True)` to hard-power-cycle its hub port (requires `uhubctl` installed).
   - **Device went dark mid-run** — fails from some iteration onward and never recovers; firmware log stops arriving. Almost always a Guru crash with frozen CDC. Recommend `uhubctl_cycle` before the next iteration; escalate to replug if that also fails.
   - **Unknown** — say so. Don't invent a root cause.

7. **Report back** with:
   - Pass rate + mean duration.
   - Classification + the specific log evidence for it.
   - A concrete next step (tighter assertion, more retries, open `/mcp-diagnose`, file a bug, nothing).

## Examples

- `tests/mesh/test_direct_with_ack.py::test_direct_with_ack_roundtrip[esp32s3->nrf52] 10` — 10 runs of that parametrized case.
- `broadcast_delivers` — no `::`, no `tests/`; treat as `-k broadcast_delivers`; runs every match 5 times.
- `tests/telemetry/test_device_telemetry_broadcast.py 3` — shorter count for a slow test.

## Notes

- If the FIRST attempt fails and the rest pass, that's a state-leak signature — suggest starting from `--force-bake` or a clean device state rather than chasing the first-failure firmware logs.
- If ALL N fail, this isn't a flake — it's a regression. Say so, stop iterating, escalate to `/mcp-test` for full-suite context.
- Don't rebuild firmware during triage. Flakes that only reproduce under different firmware belong in a separate session with a plan.
