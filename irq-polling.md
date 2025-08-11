### IRQ polling flow (SX127x/RA‑08H on Elecrow CRT01262M)

Scope
- Variant `elecrow_crt01262m` with RA‑08H (SX127x) uses polling instead of GPIO IRQs.
- Applies to `RF95Interface` + `RadioLibInterface` polling path.

Why polling
- `LORA_DIO0` is `RADIOLIB_NC` and `RF95_USE_POLLING` is enabled in `variant.h` → GPIO IRQ not available; use timer-based polling.

Semtech SX1276 RegIrqFlags (0x12) mapping
- 0x80: RxTimeout
- 0x40: RxDone
- 0x20: PayloadCrcError
- 0x10: ValidHeader
- 0x08: TxDone
- 0x04: CadDone
- 0x02: FhssChangeChannel
- 0x01: CadDetected

High-level flow
- startReceive()
  - Puts radio into RX, sets `isReceiving = true`, schedules first poll via `schedulePoll(1ms)`.
- onNotify(POLL_EVENT)
  - Process up to two events per tick (TX then RX) to avoid extra latency when both are pending.
  - After each handled event, `startReceive()` is called to re-arm RX; if still active, schedule next poll.
- checkPendingInterrupt() [RF95Interface]
  - Read `irq = lora->getIRQFlags()` once; log `flags=0x%04x` and decoded bits.
  - If TX_DONE bit is set and `isSending()==true` → clear TX_DONE and return ISR_TX.
  - If RX_DONE bit is set → do not clear (let `readData()` clear) and return ISR_RX.
  - Otherwise, clear non-terminal bits to prevent stickiness: CRC_ERROR, RX_TIMEOUT, CAD_DONE, CAD_DETECTED, FHSS_CHANGE_CHANNEL. Do not clear VALID_HEADER (used to detect active RX).
  - Return ISR_NONE.
- handleTransmitInterrupt()
  - Completes send, clears TX power state; `startReceive()` is called by caller.
- handleReceiveInterrupt()
  - Uses `iface->getPacketLength()` then `iface->readData()`; on success constructs `MeshPacket` and delivers; on error increments `rxBad` and logs airtime as RX_ALL (noise).

Scenarios handled
- TX_DONE only (0x0008):
  - Poll detects TX, clears flag, completes send, re-arms RX in same tick.
- RX_DONE only (0x0040):
  - Poll returns ISR_RX without clearing; `readData()` consumes flags; packet delivered.
- TX_DONE and RX_DONE set (rare back-to-back):
  - First loop handles TX (only if `isSending()==true`), second loop handles RX; no extra poll tick.
- VALID_HEADER only (0x0010):
  - Indicates active reception; not cleared by poller; no action yet; next polls will see RX_DONE or timeout.
- PAYLOAD_CRC_ERROR (0x0020):
  - Cleared to avoid masking the next RX; counted as noise when it leads to read errors.
- RX_TIMEOUT (0x0080):
  - Cleared; no further action.
- CAD events (0x0001, 0x0004) or FHSS change (0x0002):
  - Cleared; no further action in LoRa RX mode.
- No flags (0x0000):
  - Return ISR_NONE; next poll scheduled only if still RX/TX active.

Flag handling matrix

| Flag | Bit | Hex | When observed | Action | Cleared by | Where cleared |
|---|---:|---:|---|---|---|---|
| RxTimeout | 7 | 0x80 | RX window elapsed without packet | Ignore | Poller | `checkPendingInterrupt()` clears to avoid sticky timeouts |
| RxDone | 6 | 0x40 | End of packet received | Handle receive | RadioLib `readData()` | Not cleared in poller; consumed during `handleReceiveInterrupt()` |
| PayloadCrcError | 5 | 0x20 | CRC check failed | Ignore | Poller | Cleared in `checkPendingInterrupt()` to prevent masking next RX |
| ValidHeader | 4 | 0x10 | Header detected / active RX | Observe only | RadioLib / hardware | Not cleared by poller (preserves active RX detection) |
| TxDone | 3 | 0x08 | Transmit completed | Handle transmit (only if `isSending()` true) | Poller | Cleared in `checkPendingInterrupt()`, then `handleTransmitInterrupt()` runs |
| CadDone | 2 | 0x04 | CAD finished | Ignore | Poller | Cleared in `checkPendingInterrupt()` |
| FhssChangeChannel | 1 | 0x02 | FHSS hop occurred | Ignore | Poller | Cleared in `checkPendingInterrupt()` |
| CadDetected | 0 | 0x01 | CAD detected activity | Ignore | Poller | Cleared in `checkPendingInterrupt()` |


Performance/robustness
- One register read per poll; at most one `clearIrqFlags()` per poll when only non-terminal bits are present.
- Early return on TX/RX; two-event loop eliminates extra tick when TX and RX are both pending.
- RX_DONE is never cleared in the poller (prevents `readData()` races).
- VALID_HEADER not cleared (preserves active-reception detection).

Logging (DEBUG)
- Raw IRQ flags and decoded bits are printed for each non-zero poll to aid field diagnostics.

Variant specifics
- `elecrow_crt01262m/variant.h` sets `LORA_DIO0 = RADIOLIB_NC` and `RF95_USE_POLLING = 1` → polling path is always active.

Implementation notes
- TX_DONE handling is gated by `isSending()` to avoid misclassifying other bits during RX.
- Masks are implemented as explicit SX1276 constants (0x80/0x40/0x20/0x10/0x08/0x04/0x02/0x01) to avoid macro mismatches.
- Debug logs include both raw flags and decoded bits: `POLL IRQ flags=0x%04x` and `POLL bits tx=%d rx=%d vh=%d`.


