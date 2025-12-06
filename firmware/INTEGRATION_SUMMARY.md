# USB Capture Module - Meshtastic Integration Summary

## Overview

A complete USB keyboard capture module has been created for the Meshtastic firmware on RP2350 Pico. The module runs on Core 1, capturing bitbanged USB data through GPIO pins, and provides a queue interface for Core 0 consumption.

## Files Created

### Core USB Capture Module (`src/platform/rp2xx0/usb_capture/`)

| File | Purpose | Status |
|------|---------|--------|
| `config.h` | GPIO pins, USB protocol constants | ✅ Created |
| `types.h` | Data structures (keystroke_event_t, etc.) | ✅ Created |
| `keystroke_queue.h` | Lock-free queue interface | ✅ Created |
| `keystroke_queue.cpp` | Queue implementation | ✅ Created |
| `usb_protocol.h` | USB validation functions | ✅ Created |
| `usb_protocol.cpp` | CRC16, PID validation implementation | ✅ Created |
| `stats.h` | Statistics stubs | ✅ Created |
| `cpu_monitor.h` | CPU monitoring stubs | ✅ Created |
| `keyboard_decoder_core1.h` | HID decoder interface | ✅ Created |
| `okhi.pio.h` | Generated PIO programs | ✅ Created |
| `README.md` | Module documentation | ✅ Created |

### Existing Files (Already Present)

| File | Purpose | Status |
|------|---------|--------|
| `pio_manager.c` | PIO state machine management | ✅ Existing |
| `pio_manager.h` | PIO manager interface | ⚠️ Need to verify |
| `usb_capture.pio` | PIO assembly program | ✅ Existing |
| `capture_v2.cpp` | Core 1 main loop | ✅ Existing |
| `packet_processor_core1.h` | Packet processor interface | ✅ Existing |
| `packet_processor_core1.cpp` | Packet validation/decoding | ✅ Existing |
| `keyboard_decoder_core1.cpp` | HID to keystroke conversion | ✅ Existing |

### Meshtastic Module Files (`src/modules/`)

| File | Purpose | Status |
|------|---------|--------|
| `USBCaptureModule.h` | Module interface | ✅ Created |
| `USBCaptureModule.cpp` | Core 0 consumer implementation | ✅ Created |

## Integration Checklist

### 1. Build System Configuration

#### PlatformIO (`platformio.ini`)

Add the following configuration:

```ini
[env:rp2350_usb_capture]
extends = rp2040_base
board = rpipico2
build_flags =
    ${rp2040_base.build_flags}
    -DARCH_RP2040
    -DPICO_RP2350=1
lib_deps =
    ${rp2040_base.lib_deps}
```

#### CMakeLists.txt (if using CMake)

```cmake
# Add USB capture source files
if(RP2350)
    target_sources(firmware PRIVATE
        src/platform/rp2xx0/usb_capture/keystroke_queue.cpp
        src/platform/rp2xx0/usb_capture/usb_protocol.cpp
        src/platform/rp2xx0/usb_capture/pio_manager.c
        src/platform/rp2xx0/usb_capture/capture_v2.cpp
        src/platform/rp2xx0/usb_capture/packet_processor_core1.cpp
        src/platform/rp2xx0/usb_capture/keyboard_decoder_core1.cpp
        src/modules/USBCaptureModule.cpp
    )

    target_include_directories(firmware PRIVATE
        src/platform/rp2xx0/usb_capture
    )
endif()
```

### 2. Module Registration

#### In `src/modules/Modules.cpp`

Add at the top:

```cpp
#ifdef ARCH_RP2040
#include "modules/USBCaptureModule.h"
#endif
```

Add in `setupModules()`:

```cpp
void setupModules()
{
    // ... existing modules ...

    #ifdef ARCH_RP2040
    usbCaptureModule = new USBCaptureModule();
    usbCaptureModule->init();
    #endif
}
```

### 3. Hardware Setup

#### GPIO Pin Connections

```
USB Keyboard    RP2350 Pico
-----------     ------------
D+ (Data+)  →   GPIO 16 (DP_INDEX)
D- (Data-)  →   GPIO 17 (DM_INDEX)
SYNC        →   GPIO 18 (START_INDEX)
GND         →   GND
```

**Note**: Pins are configured as GPIO 16, 17, 18 in `config.h` (pins must be consecutive)

### 4. Missing Files Check

#### Verify pio_manager.h exists

```bash
ls -la src/platform/rp2xx0/usb_capture/pio_manager.h
```

If missing, create from the reference in `/Users/rstown/Desktop/Projects/STE/client_pico/lib/USBCapture/pio_manager.h`

### 5. Compilation

```bash
# Using PlatformIO
pio run -e rp2350_usb_capture

# Flash to device
pio run -e rp2350_usb_capture -t upload

# Monitor serial output
pio device monitor -b 115200
```

## Testing the Module

### 1. Connect Hardware

1. Wire USB D+/D- to GPIO 20/21
2. Connect USB keyboard
3. Power the RP2350

### 2. Monitor Serial Output

Expected output:

```
[INFO] USB Capture Module initializing...
[INFO] Starting Core1 for USB capture...
[INFO] Core1 started and USB capture running
```

### 3. Type on Keyboard

Expected output:

```
[INFO] Keystroke: CHAR 'h' (scancode=0x0b, mod=0x00)
[INFO] Keystroke: CHAR 'e' (scancode=0x08, mod=0x00)
[INFO] Keystroke: CHAR 'l' (scancode=0x0f, mod=0x00)
[INFO] Keystroke: CHAR 'l' (scancode=0x0f, mod=0x00)
[INFO] Keystroke: CHAR 'o' (scancode=0x12, mod=0x00)
```

### 4. Check Queue Statistics

Every 10 seconds:

```
[DEBUG] Queue stats: count=0, dropped=0
```

## Architecture Overview

### Dual-Core Operation

```
┌─────────────────────────────────────────────────────────┐
│                     RP2350 PICO                         │
│                                                         │
│  ┌────────────────────┐       ┌────────────────────┐  │
│  │     CORE 0         │       │     CORE 1         │  │
│  │                    │       │                    │  │
│  │  Meshtastic        │       │  USB Capture       │  │
│  │  Firmware          │       │  Loop              │  │
│  │                    │       │                    │  │
│  │  USBCaptureModule  │◄──────┤  capture_v2.cpp    │  │
│  │  (Consumer)        │ Queue │  (Producer)        │  │
│  │                    │       │                    │  │
│  │  - Poll queue      │       │  - PIO capture     │  │
│  │  - Log keystrokes  │       │  - Bit unstuffing  │  │
│  │  - Send over mesh  │       │  - HID decoding    │  │
│  │                    │       │  - Queue push      │  │
│  └────────────────────┘       └────────────────────┘  │
│           ▲                            │               │
│           │                            │               │
│           └────── Shared Queue ────────┘               │
│              (Lock-free ring buffer)                   │
│                                                         │
│  ┌────────────────────────────────────────────────┐   │
│  │              PIO State Machines                 │   │
│  │   GPIO 20/21/22 ──► USB Signal Capture         │   │
│  └────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
                   USB Keyboard (D+/D-)
```

### Data Flow

```
USB Keyboard
    │
    ├─ D+ ──► GPIO 20
    └─ D- ──► GPIO 21
           │
           ▼
    PIO State Machines (Core 1)
           │
           ▼
    Raw 31-bit packets
           │
           ▼
    Bit Unstuffing
           │
           ▼
    USB Protocol Validation
    (SYNC, PID, CRC)
           │
           ▼
    HID Report Decoding
           │
           ▼
    Keystroke Events
           │
           ▼
    Lock-free Queue
           │
           ▼
    Core 0 Consumer
    (USBCaptureModule)
           │
           ├─► Log to Serial
           ├─► Send over Mesh
           └─► Store to Flash
```

## Key Questions Answered

### Q: Which GPIO pins to use?

**A**: GPIO 20, 21, 22 (configured in `config.h`). Pins MUST be consecutive due to PIO hardware requirements.

### Q: How does Core 0 start Core 1?

**A**: In `USBCaptureModule::init()`:

```cpp
multicore_launch_core1(capture_controller_core1_main_v2);
capture_controller_start_v2(&controller);  // Send 0x69696969 command
```

### Q: Where do keystrokes go?

**A**: Currently logged to serial. Modify `USBCaptureModule::processKeystrokeQueue()` to send over mesh or store.

### Q: What module should consume the queue?

**A**: `USBCaptureModule` on Core 0. It won't interfere with Meshtastic services - it just polls the queue every 100ms.

## Next Steps

### Immediate (Required for Basic Operation)

1. ✅ Create all header files - **DONE**
2. ✅ Create queue implementation - **DONE**
3. ✅ Create USB protocol utilities - **DONE**
4. ✅ Create consumer module - **DONE**
5. ⚠️ Verify `pio_manager.h` exists or create it
6. ⚠️ Add module to `Modules.cpp`
7. ⚠️ Configure PlatformIO build
8. ⚠️ Test on hardware

### Future Enhancements

1. Send keystrokes over Meshtastic mesh network
2. Add configuration via Meshtastic app
3. Batch keystrokes before transmission
4. Add encryption for captured data
5. Support USB mice and other HID devices
6. Add flash storage for offline buffering
7. Implement mesh protocol for keystroke packets

## Troubleshooting

### Build Errors

**Problem**: Cannot find header files
**Solution**: Ensure `src/platform/rp2xx0/usb_capture` is in include path

**Problem**: Multicore functions undefined
**Solution**: Verify `-DPICO_RP2350=1` build flag is set

### Runtime Errors

**Problem**: Core 1 doesn't start
**Solution**: Check serial output for "Core1 started" message, verify RP2350 board

**Problem**: No keystrokes captured
**Solution**:
- Verify GPIO pin connections (D+ to GPIO 20, D- to GPIO 21)
- Try switching USB speed (LOW ↔ FULL) in `config.h`
- Check keyboard is powered (try USB hub)

**Problem**: Queue overflows (dropped > 0)
**Solution**:
- Increase queue size in `keystroke_queue.h`
- Decrease polling interval in `USBCaptureModule::runOnce()`
- Optimize `processKeystrokeQueue()` performance

## Support

For questions or issues:
1. Check `src/platform/rp2xx0/usb_capture/README.md`
2. Review integration steps in this document
3. Verify hardware connections match GPIO pin configuration
4. Check serial output for error messages

## License

- USB Capture Core: BSD-3-Clause
- Meshtastic Module: GPL-3.0-only
