# Dynamic Coding Rate

Dynamic Coding Rate (DCR) lets firmware select LoRa coding rate per packet while leaving the Meshtastic wire packet unchanged. It relies on LoRa explicit header mode: the RF header carries the payload CR, payload length, and CRC flag, so the selected CR is physical-layer metadata and does not belong in `MeshPacket`.

The implementation is centered on `AirtimePolicy`. Today that policy chooses CR 4/5 through CR 4/8. It is deliberately not owned by telemetry, routing, or an individual radio backend because those paths all compete for the same airtime budget.

## Configuration

DCR settings live in `Config.LoRaConfig`:

- `dcr_mode`: `DCR_OFF` or `DCR_ON`. `DCR_OFF` keeps the configured static coding rate; `DCR_ON` applies
  per-packet CR selection.
- `dcr_min_cr` / `dcr_max_cr`: optional denominator clamps, where `0` means firmware default.
- `dcr_robust_airtime_pct`: rolling-window cap for non-urgent CR 4/8 airtime.
- `dcr_disable_neighbor_tracking`: disables local neighbor CR tracking.
- `dcr_telemetry_max_cr`, `dcr_user_min_cr`, and `dcr_alert_min_cr`: class-level policy clamps. By default,
  telemetry caps at CR 4/6, user traffic may compact down to CR 4/5 when the channel is pressured, and alerts stay
  at least CR 4/7.

Existing `coding_rate` remains the base/static CR. When DCR is off, it is the transmit CR. When DCR is enabled, the radio starts from that base and may choose a different CR immediately before TX.

## Runtime Flow

Outgoing decoded packets are classified before encryption in `Router::send()` and remembered in a local side cache. This avoids adding a protobuf field and lets relayed/encrypted packets still use earlier local context when available.

Immediately before transmit, `RadioInterface::chooseCodingRateForPacket()` builds a context from:

- packet class and priority
- retry state recorded by `NextHopRouter`
- relay/late-relay/last-hop context
- channel utilization, TX utilization, queue depth, and duty-cycle pressure
- predicted airtime for each candidate CR

The selected CR is applied through `setActiveCodingRate()`, transmission starts, and the backend restores the base CR after TX completion or TX-start failure.

Received RadioLib packets call `getLoRaRxHeaderInfo()` during airtime calculation. When `DCR_ON` is enabled, the normalized RX CR is then passed to `AirtimePolicy::observeRx()` for counters and local neighbor attribution.

## Policy Shape

The policy uses four internal levels:

- `SLIM`: CR 4/5
- `NORMAL`: CR 4/6
- `ROBUST`: CR 4/7
- `RESCUE`: CR 4/8

Routine telemetry, position, NodeInfo, map reports, range-test packets, and store-and-forward bulk prefer compact CRs. Text and other user-value packets are balanced. Routing/control traffic gets a robust bias. Alerts and detection events get the strongest bias, bounded by legal/duty-cycle constraints.

Retries only escalate aggressively when the failure looks quiet/link-related. Congested retries prefer more backoff and compact CR, because longer airtime does not fix collisions.

Relays choose their own CR per hop. A previous hop's CR is treated as an observation, not a command.

## Safety Rails

DCR has several clamps before a packet reaches the radio:

- config min/max CR
- telemetry/user/alert class bounds
- per-class airtime caps
- duty-cycle pressure bias
- non-urgent CR 4/8 token bucket
- the on/off mode gate

The token bucket is intentionally local. It prevents a node from spending a quiet channel entirely on CR 4/8 background traffic while still allowing urgent packets to bypass the clamp.

## Backend Notes

`RadioInterface` owns the common decision flow so RadioLib radios and the Portduino simulator do not duplicate policy code.

SX127x/RF95 and SX126x backends apply per-packet CR with normal RadioLib `setCodingRate(cr)`.

LR11x0 and SX128x static config currently restores their existing long-interleaving behavior where supported. DCR TX calls use normal interleaving and then restore the base static setting after TX.

## Testing Status

`test/test_dynamic_coding_rate` covers:

- default settings and clamps
- compact telemetry under congestion
- idle telemetry avoiding CR 4/8
- idle text using CR 4/7
- alert packets using CR 4/8
- quiet final retry escalation
- congested retry not jumping to CR 4/8
- CR 4/8 token-bucket clamping
- configured `min_cr` staying authoritative through later safety clamps
- off mode
- direct RX neighbor CR attribution

Hardware interop still needs real-radio validation across SX126x, SX127x/RF95, SX128x, LR11x0, static-CR nodes, and mixed DCR/static relays.
