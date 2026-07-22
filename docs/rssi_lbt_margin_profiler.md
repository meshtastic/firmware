# RSSI listen-before-talk margin profiler

The firmware has an optional second listen-before-talk (LBT) gate that defers a transmit when the
channel already carries energy a preamble-only CAD scan would miss (a neighbour's packet that is
mid-payload, past its preamble). That gate, `RadioLibInterface::channelBusyByRSSI()`, declares the
channel busy when the instantaneous in-RX RSSI sits more than a fixed margin above the rolling noise
floor:

```text
busy  ⇔  getCurrentRSSI()  >  getNoiseFloor() + RSSI_LBT_MARGIN_DB
```

Picking that margin is the whole problem. Set it too low and the node reads its own noise as "busy" and
stops transmitting; too high and it never defers for real traffic. The right value depends on the board
and the RF environment, so the gate ships **off** and the margin is an unset build flag.

This **profiler** is the instrumentation that lets you choose `RSSI_LBT_MARGIN_DB` from measured data
instead of guessing. It is also **off by default** and compiled out unless you build with
`-DLORA_RSSI_LBT_PROFILE`.

Sources of truth: `src/mesh/RssiProfiler.h` (the stats engine), `src/mesh/RadioLibInterface.{h,cpp}`
(the hooks, the RSSI reads, the gate). Related: the rolling noise floor
(`RadioLibInterface::getNoiseFloor()` / `updateNoiseFloor()`), and the gate flag
`LORA_RSSI_LBT_MARGIN_DB`.

---

## The idea

A workable margin has to sit **above** how far idle noise jitters over the floor and **below** how far a
real packet rises over it. So the profiler characterises the quantity `(RSSI − noise floor)` in two
channel states and reports the gap between their tails:

| State      | Sampled from                            | Tail that matters  | Meaning                                                         |
| ---------- | --------------------------------------- | ------------------ | --------------------------------------------------------------- |
| **idle**   | instantaneous RSSI while not receiving  | **high** (p90-p99) | how far noise rides above the floor - the margin must clear it  |
| **signal** | a decoded packet's RSSI (`mp->rx_rssi`) | **low** (p05-p10)  | how little real traffic rises above the floor - margin below it |

Any value in `(idle p99, signal p05)` works. If the two tails **overlap** (`idle p99 ≥ signal p05`),
there is no clean margin on that board/environment - a useful finding in itself: RSSI-based CCA cannot
separate noise from traffic there.

## How it is wired

The stats math is a small, radio-agnostic, dependency-free engine so it stays out of the core radio file
and can be reasoned about (and unit-tested) on its own:

- **`RssiDeltaStats`** (`src/mesh/RssiProfiler.h`) - a rolling distribution of a signed dB delta:
  running `count` / `min` / `max` / `mean`, plus a 34-bin histogram (2 dB per bin; `bin[0]` is the
  `< 0 dB` underflow, the top bin an overflow catch-all, covering ~0-64 dB above the floor) and an
  approximate `percentile()` read off that histogram.

`RadioLibInterface` owns two of these (`rssiIdleStats`, `rssiSignalStats`) and three hook points, all
under `#ifdef LORA_RSSI_LBT_PROFILE`:

- **idle sample** - folded into `updateNoiseFloor()`. It **reuses the RSSI that function already reads**
  (no extra SPI transaction), so idle samples arrive at the noise-floor cadence (~5 s) and only while the
  radio is genuinely idle in RX - exactly the samples that define the floor.
- **signal sample** - `profileRssiOnPacket(mp->rx_rssi)` in `handleReceiveInterrupt()`, immediately after
  `addReceiveMetadata()` has populated the packet RSSI. Only counts sane, in-range readings.
- **report** - `profileRssiReport()`, throttled to once per 60 s, logs both distributions and the
  derived usable window.

Because the idle sampler piggybacks on the existing noise-floor read and everything is guarded, the
profiler adds no cost - and no behaviour change - to a normal build.

## Building with it

Add the flag to your environment's `build_flags` in `platformio.ini`, or pass it for a one-off build:

```sh
PLATFORMIO_BUILD_FLAGS="-DLORA_RSSI_LBT_PROFILE" pio run -e <your-env>
```

The profiler is independent of the gate: you profile **first** to choose the margin, then build the gate
on with `-DLORA_RSSI_LBT_MARGIN_DB=<n>`. You can also define both together to watch the gate's inputs
live.

## Reading the output

Every ~60 s the node logs three lines (deltas are dB above the noise floor):

```text
RSSIprof floor=-119dBm idle n=412 min=-2 mean=1 p90=3 p95=4 p99=6 max=11
RSSIprof signal n=57 min=14 p05=19 p10=22 mean=41 max=88
RSSIprof suggest margin in (6, 19) dB above floor
```

- **idle line** - the noise-jitter distribution. `p99` (here 6 dB) is the highest routine excursion the
  margin must sit above.
- **signal line** - the real-traffic distribution over packets this node decoded. `p05` (here 19 dB) is
  the weakest rise the margin must stay below.
- **suggest line** - the usable window, `(idle p99, signal p05)`. Pick a value a couple dB inside the low
  end for headroom (e.g. `-DLORA_RSSI_LBT_MARGIN_DB=9` for the run above), then verify the gate's
  defer-rate on air.

Run it long enough to populate the tails - the idle line needs a few hundred samples (tens of minutes at
the 5 s cadence) before `p99` is stable, and the signal line needs real traffic from neighbours.

## Limitations

- The signal distribution only includes packets this node **decoded**. The exact case the gate targets -
  mid-payload of a packet too weak or too collided to decode - cannot be labelled, but it is bounded by
  the decodable-signal distribution, which is what the margin keys off anyway.
- `mp->rx_rssi` is the per-packet RSSI (an average over the packet), not the instantaneous mid-payload
  level. For the true mid-payload distribution you would instead sample `getCurrentRSSI()` _during_ the
  RX window (between `HEADER_VALID` and `RX_DONE`) - more code, more accuracy. The packet-RSSI proxy is
  the intended starting point.
- Percentiles are read from the 2 dB histogram, so they are quantised to bin edges - fine for choosing a
  margin, not a precise statistic.
- All negative deltas share a single underflow bin (`bin[0]`), so a percentile that falls in it collapses
  to the minimum sample rather than resolving within the negative range. This does **not** affect the
  margin, which is keyed off the idle **high** tail (`idle p99`, always well positive - noise rides above
  its own rolling mean). It can understate the signal **low** tail (`signal p05`) when a node decodes
  packets sitting below its noise floor (possible at high SF), which would make the suggested window look
  narrower - or falsely "overlapping" - than it is. It never biases toward a smaller (more aggressive)
  margin. If you need a trustworthy `signal p05` on such a board, give the histogram negative-range bins.
- Cost when enabled: two `RssiDeltaStats` (~296 bytes RAM total, 148 each), no extra SPI reads (the idle
  sample is reused), and one throttled log line per minute.
