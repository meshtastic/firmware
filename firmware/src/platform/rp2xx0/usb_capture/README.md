# USB Capture Module for Meshtastic (RP2350)

This module captures USB keyboard keystrokes using PIO hardware on the RP2350 Pico and makes them available to Meshtastic running on Core 0.

## Architecture

### Dual-Core Design
- **Core 0**: Runs Meshtastic firmware + USB Capture consumer
- **Core 1**: Runs USB capture loop (PIO-based bitbanging)

### Inter-Core Communication
- Lock-free ring buffer queue (1024 events, 32KB)
- Core 1 (producer) pushes keystroke events
- Core 0 (consumer) pops events for processing/transmission

### Data Flow
```
USB Keyboard → GPIO Pins → PIO State Machines (Core 1)
  → Raw USB Packets → Bit Unstuffing → Validation
  → HID Report Decoding → Keystroke Events → Queue
  → Core 0 Consumer → Meshtastic Modules
```

## Hardware Setup

### GPIO Pin Connections (RP2350)
Connect USB D+/D- lines to consecutive GPIO pins:

```
USB Keyboard    RP2350 Pico
-----------     ------------
D+ (Data+)  →   GPIO 16 (DP_INDEX)
D- (Data-)  →   GPIO 17 (DM_INDEX)
SYNC        →   GPIO 18 (START_INDEX)
GND         →   GND
```

**CRITICAL**: Pins MUST be consecutive (16, 17, 18) as configured in `config.h`

### USB Speed Configuration
- **Low Speed**: 1.5 Mbps (most keyboards)
- **Full Speed**: 12 Mbps (some modern keyboards)

Configure in `USBCaptureModule::init()` or `config.h`

## File Structure

### Core USB Capture Files
```
usb_capture/
├── config.h                      # GPIO pins, USB protocol constants
├── types.h                       # Data structures (keystroke_event_t, etc.)
├── keystroke_queue.h/cpp         # Lock-free inter-core queue
├── pio_manager.c/h               # PIO state machine management
├── usb_capture.pio               # PIO assembly for USB bitbanging
├── okhi.pio.h                    # Generated PIO C header
├── usb_protocol.h/cpp            # USB CRC16, validation functions
├── capture_v2.cpp                # Core 1 main loop
├── packet_processor_core1.h/cpp # Packet validation and decoding
├── keyboard_decoder_core1.h/cpp # HID report to keystroke conversion
├── stats.h                       # Statistics stubs
├── cpu_monitor.h                 # CPU monitoring stubs
└── README.md                     # This file
```

### Meshtastic Module Files
```
src/modules/
├── USBCaptureModule.h            # Module header
└── USBCaptureModule.cpp          # Module implementation (Core 0 consumer)
```

## Integration Steps

### 1. PlatformIO Configuration

Add to `platformio.ini`:

```ini
[env:rp2350_usb_capture]
extends = rp2040_base
board = rpipico2  # Or your RP2350 board
build_flags =
    ${rp2040_base.build_flags}
    -DARCH_RP2040
    -DPICO_RP2350=1
lib_deps =
    ${rp2040_base.lib_deps}
```

### 2. Build System Integration

Add to CMakeLists.txt or build configuration:

```cmake
# Add USB capture source files
target_sources(firmware PRIVATE
    src/platform/rp2xx0/usb_capture/keystroke_queue.cpp
    src/platform/rp2xx0/usb_capture/usb_protocol.cpp
    src/platform/rp2xx0/usb_capture/pio_manager.c
    src/platform/rp2xx0/usb_capture/capture_v2.cpp
    src/platform/rp2xx0/usb_capture/packet_processor_core1.cpp
    src/platform/rp2xx0/usb_capture/keyboard_decoder_core1.cpp
    src/modules/USBCaptureModule.cpp
)

# Add include directories
target_include_directories(firmware PRIVATE
    src/platform/rp2xx0/usb_capture
)
```

### 3. Module Registration

Add to `src/modules/Modules.cpp`:

```cpp
#ifdef ARCH_RP2040
#include "modules/USBCaptureModule.h"
#endif

void setupModules()
{
    // ... existing modules ...

    #ifdef ARCH_RP2040
    usbCaptureModule = new USBCaptureModule();
    usbCaptureModule->init();
    #endif
}
```

### 4. Compile and Flash

```bash
pio run -e rp2350_usb_capture -t upload
```

## Usage

### Starting USB Capture

The module starts automatically on boot:
1. Core 0 initializes Meshtastic
2. USBCaptureModule::init() starts Core 1
3. Core 1 begins USB capture immediately
4. Core 0 polls queue every 100ms

### Monitoring Keystrokes

Connect to serial console (115200 baud):

```
[INFO] USB Capture Module initializing...
[INFO] Starting Core1 for USB capture...
[INFO] Core1 started and USB capture running
[INFO] Keystroke: CHAR 'h' (scancode=0x0b, mod=0x00)
[INFO] Keystroke: CHAR 'e' (scancode=0x08, mod=0x00)
[INFO] Keystroke: CHAR 'l' (scancode=0x0f, mod=0x00)
[INFO] Keystroke: CHAR 'l' (scancode=0x0f, mod=0x00)
[INFO] Keystroke: CHAR 'o' (scancode=0x12, mod=0x00)
[INFO] Keystroke: ENTER
```

### Queue Statistics

Every 10 seconds, queue diagnostics are logged:

```
[DEBUG] Queue stats: count=0, dropped=0
```

## Extending the Module

### Send Keystrokes Over Mesh

Modify `USBCaptureModule::processKeystrokeQueue()`:

```cpp
void USBCaptureModule::processKeystrokeQueue()
{
    keystroke_event_t event;

    while (keystroke_queue_pop(keystroke_queue, &event))
    {
        if (event.type == KEYSTROKE_TYPE_CHAR)
        {
            // Send character over mesh
            sendTextMessage(String(event.character));
        }
    }
}
```

### Batch Keystrokes

Accumulate keystrokes and send periodically:

```cpp
String keystroke_buffer;

void USBCaptureModule::processKeystrokeQueue()
{
    keystroke_event_t event;

    while (keystroke_queue_pop(keystroke_queue, &event))
    {
        if (event.type == KEYSTROKE_TYPE_CHAR)
        {
            keystroke_buffer += event.character;
        }
        else if (event.type == KEYSTROKE_TYPE_ENTER)
        {
            // Send accumulated buffer
            sendTextMessage(keystroke_buffer);
            keystroke_buffer = "";
        }
    }
}
```

## Troubleshooting

### No Keystrokes Captured

1. **Check GPIO pins**: Verify D+/D- connected to correct pins
2. **Check USB speed**: Try switching LOW ↔ FULL speed
3. **Check keyboard type**: Some wireless keyboards won't work
4. **Check power**: Ensure keyboard has power (try USB hub)

### Core 1 Not Starting

1. **Check multicore support**: RP2350 required
2. **Check build flags**: Ensure `-DPICO_RP2350=1`
3. **Check serial output**: Look for "Core1 started" message

### Queue Overflows

If `dropped` count increases:
1. Increase queue size in `keystroke_queue.h`
2. Increase Core 0 polling frequency
3. Optimize `processKeystrokeQueue()` performance

## Performance

### Resource Usage
- **RAM**: ~36KB (32KB queue + 4KB structures)
- **Flash**: ~8KB (code + PIO programs)
- **CPU**: Core 1 100% (dedicated), Core 0 <1% (polling)

### Latency
- **Capture to Queue**: <10μs (Core 1 processing)
- **Queue to Core 0**: 0-100ms (polling interval)
- **End-to-End**: Typically <150ms

## Known Limitations

1. **RP2350 Only**: Requires PIO hardware and dual-core
2. **USB 2.0 Only**: No USB 3.0 support
3. **Keyboards Only**: HID mouse/gamepad not supported
4. **No Hotplug**: Keyboard must be connected at boot
5. **GPIO Pins Fixed**: Consecutive pins required by PIO

## Future Enhancements

- [ ] Configuration via Meshtastic app
- [ ] Support for USB mice
- [ ] Encryption of captured keystrokes
- [ ] Flash storage for offline buffering
- [ ] Mesh protocol for keystroke transmission
- [ ] Dynamic USB speed detection
- [ ] GPIO pin configuration at runtime

## License

SPDX-License-Identifier: BSD-3-Clause (USB capture core)
SPDX-License-Identifier: GPL-3.0-only (Meshtastic module)
