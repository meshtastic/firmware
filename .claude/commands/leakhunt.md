---
description: Hunt for memory leaks (and other slow degradations) by reading the persistent recorder's heap timeline + log slice over a window
argument-hint: [window=1h] [field=free_heap] [variant=local]
---

<!-- markdownlint-disable MD029 -->

# `/leakhunt` — read the recorder, classify a memory leak

Use the always-on recorder (`mcp-server/.mtlog/`) to read a heap timeline plus the matching log slice and produce a one-page verdict: **steady / slow leak / fragmentation / OOM-imminent**. No firmware changes, no special build flags — the LocalStats telemetry packet that the firmware already broadcasts every ~60 s carries `heap_free_bytes` and `heap_total_bytes`.

## Two signal paths — pick the right one

| Path                  | Build flag       | Cadence        | Per-thread attribution | Cost                      |
| --------------------- | ---------------- | -------------- | ---------------------- | ------------------------- |
| LocalStats packet     | (default)        | ~60 s          | No                     | Free — always on          |
| `[heap N]` log prefix | `-DDEBUG_HEAP=1` | every log line | Yes (Thread X leaked)  | Bigger flash + log volume |

Both feed the same `telemetry_timeline(field="free_heap")` query — when DEBUG_HEAP is on, the recorder synthesizes telemetry rows from log prefixes (tagged `source: debug_heap`), so a single timeline call gets whichever signal is available. **For a slow leak diagnosis, the default path is plenty** (60 s cadence over 6 h = 360 points; linear regression over that nails sub-100-byte/min slopes). **DEBUG_HEAP is for attribution** — when the slope is real and you need to know which thread is leaking.

## What to do

1. **Parse `$ARGUMENTS`**: optional `window` (default `1h`, accepts `30m`/`6h`/`-3d`/etc.), optional `field` (default `free_heap`; alternates: `total_heap`, `battery_level`, anything in the LocalStats variant), optional `variant` (default `local`; alternates: `device`, `environment`, `power`, `airQuality`, `health`).

2. **Verify the recorder is alive** — call `mcp__meshtastic__recorder_status`. Check:
   - `running == True`
   - `files.telemetry.lines > 0` (at least one telemetry packet recorded — if zero, the device hasn't broadcast LocalStats yet OR `set_debug_log_api` has never been on; tell the operator to run `mcp__meshtastic__set_debug_log_api(enabled=True)` and wait one device-update interval)
   - `files.telemetry.last_ts` within the last 5 minutes (if older, the device is silent — log that, not "leak detected")

3. **Detect whether DEBUG_HEAP is active** — `mcp__meshtastic__logs_window(start="-2m", grep=r"\\[heap \\d+\\]", max_lines=3)`. If any line matches, the firmware has the prefix → DEBUG_HEAP is on, expect higher-cadence data and `heap_event` rows. If zero matches over the last 2 minutes, you're on the LocalStats-only path.

4. **Pull the timeline** — `mcp__meshtastic__telemetry_timeline(window=$window, variant=$variant, field=$field, max_points=200)`. Read:
   - `samples` — how many raw points contributed
   - `min`, `max` — total swing
   - `slope_per_min` — units per minute (linear regression over the whole window)

5. **Pull the log context for the same window** — `mcp__meshtastic__logs_window(start="-${window}", grep="Heap status|leaked heap|freed heap|out of memory|Alloc an err|panic|abort", max_lines=200)`. These are the strings the firmware emits when something memory-related happens (`DEBUG_HEAP` builds emit `"Heap status:"` and `"leaked heap"` lines; production builds emit `"Alloc an err"` on failure and `"out of memory"` on OOM).

6. **Pull marker events** so we know if the operator labeled phases — `mcp__meshtastic__events_window(start="-${window}", kind="mark|connection_lost|connection_established")`. If a `connection_lost` overlaps a sharp drop, that's not a leak; that's a reboot.

6a. **(DEBUG_HEAP only) Per-thread attribution** — `mcp__meshtastic__logs_window(start="-${window}", grep="leaked heap", max_lines=200)`. Each row has a structured `heap_event` field with `{kind, thread, before, after, delta}`. Aggregate by thread: sum the `delta` over the window per thread name. The thread with the largest cumulative negative delta is your suspect. Note the count too — a thread with 50× small leaks is different from 1× big leak.

7. **Classify** based on what the data says, NOT on what you wish it said. Use these rules in order:
   - **Insufficient data** (< 5 samples): say so. Suggest a longer window or longer wait. Stop.
   - **Reboot mid-window**: if any `connection_lost` event is present AND `free_heap` jumped UP at that timestamp, the device rebooted. Note it; pre-reboot trend may be a leak but you only have part of the curve.
   - **OOM-imminent**: any `Alloc an err=` or `out of memory` line in the log slice. This trumps everything; flag urgently.
   - **Slow leak**: `slope_per_min < -50` AND `max - min > 1000` AND no reboot. The heap is monotonically (or near-monotonically) declining. Estimate time-to-zero: `min / -slope_per_min` minutes. Surface it.
   - **Fragmentation suspect**: `slope_per_min` close to zero (|x| < 50) BUT min trends down across the window AND the log slice shows `Alloc an err` warnings WITHOUT total OOM. Means free total is OK but largest contiguous block is shrinking. Recommend a `DEBUG_HEAP` build to confirm.
   - **Steady**: |slope_per_min| < 50, no error lines. Heap is fine.
   - **Recovery curve**: slope is POSITIVE — heap recovered. Either a workload completed or GC fired. Note it; not a leak.

8. **Report**:

   ```text
   /leakhunt window=6h field=free_heap variant=local
   ────────────────────────────────────────────────────
   recorder        : running, telem last_ts 8s ago
   build           : DEBUG_HEAP=ON (per-line prefix detected)
   samples         : 14,200 over 6h (cadence ~1.5s, log-line synth)
   free_heap       : min 92,344 / max 124,008 / range 31,664
   slope           : -82 bytes/min (negative — heap declining)
   reboots         : none in window
   OOM events      : none
   error lines     : 3× "Alloc an err=ESP_ERR_NO_MEM" at +4h12m, +5h08m, +5h44m
   thread leaks    : (DEBUG_HEAP) MeshPacket -3,124 B over 18 events
                                  Router       -1,408 B over 4 events
                                  others       -240 B
   verdict         : SLOW LEAK — primary suspect MeshPacket thread
   est. time-to-OOM: ~1,127 min (~18.8 h) at current slope
   evidence        : (3 log line citations with uptimes)
   ```

   Then: **what to do next.**
   - SLOW LEAK, **DEBUG_HEAP off** → recommend rebuilding with the flag and re-running this skill. Concrete one-liner the operator can copy:
     ```text
     mcp__meshtastic__build(env="<env>", build_flags={"DEBUG_HEAP": 1})
     mcp__meshtastic__pio_flash(env="<env>", port="<port>", confirm=True)
     ```
     After flash, set debug_log_api back on and wait one window; re-run `/leakhunt`.
   - SLOW LEAK, **DEBUG_HEAP on** → cite the top-leaking thread name from step 6a. Point at the corresponding source file (`grep -rn "ThreadName(\"<name>\")" src/`); the operator decides what to fix.
   - FRAGMENTATION SUSPECT → propose pre-allocating any per-packet buffers; or rebuilding with `CONFIG_HEAP_TASK_TRACKING=y` on ESP32 to see who's holding the largest blocks.
   - OOM-IMMINENT → flag for immediate attention; don't wait for the next telemetry interval.
   - STEADY → say so; stop. Don't invent problems.

## What NOT to do

- Don't assume a leak from a single dip. LocalStats fires every ~60 s and the firmware naturally allocates+frees on each broadcast cycle; one packet sees the trough. Look at the slope, not the deltas.
- Don't recommend code changes. This skill diagnoses; the operator decides what to fix.
- Don't enable `set_debug_log_api` automatically — if it's off, telemetry isn't reaching pubsub anyway, and the recorder will be empty. Tell the operator to flip it on and wait, then re-run.
- Don't run heavy workloads to "trigger the leak." The recorder is passive; we read what's there.

## Companion: `mark_event` for stress runs

If the operator wants to test under stimulus (e.g. blast 50 broadcasts and see what the heap does), they can frame the experiment with markers:

```text
mark_event("burst-start")
… run the workload …
mark_event("burst-end")
/leakhunt window=15m
```

The markers land in both `events.jsonl` and `logs.jsonl`, so the report can show "free_heap dipped 8 KB during the burst window, recovered to baseline within 2 LocalStats cycles" → not a leak.
