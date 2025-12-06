# Meshstatic Module - Component 1: Batch Manager

**Independent keystroke batch manager with CSV output and 200-byte file limit**

---

## Overview

Component 1 of the Meshstatic Module provides CSV-based keystroke batching with strict size limits. This module runs independently and prepares keystroke data for storage and transmission.

### Features

- ✅ **CSV Format**: Human-readable output with header
- ✅ **200-Byte Limit**: Automatic enforcement of file size constraint
- ✅ **Independent Operation**: No dependencies on LoRa or Core 0
- ✅ **Core 1 Compatible**: Designed for RP2350 Core 1 execution
- ✅ **Zero Dynamic Allocation**: Fixed-size buffers for embedded systems

---

## Architecture

```
Keystroke Capture (Core 1)
       ↓
meshstatic_batch_add()
       ↓
CSV Buffer (max 200 bytes)
       ↓
meshstatic_batch_is_full() → true
       ↓
[Ready for Component 2: Storage]
```

---

## CSV Format

### Header (42 bytes)
```csv
timestamp_us,scancode,modifier,character
```

### Data Rows (~25 bytes each)
```csv
1234567890,0x04,0x00,a
1234568000,0x05,0x02,B
1234569000,0x28,0x00,↵
```

**Fields:**
- `timestamp_us`: Microsecond timestamp (uint32_t)
- `scancode`: HID scancode (hex, e.g., 0x04 for 'A')
- `modifier`: Modifier flags (hex, e.g., 0x02 for Shift)
- `character`: ASCII character

---

## API Reference

### Initialization

```c
meshstatic_batch_t batch;
meshstatic_batch_init(&batch);
```

### Adding Keystrokes

```c
bool success = meshstatic_batch_add(
    &batch,
    0x04,           // scancode (HID 'A')
    0x00,           // modifier (none)
    'a',            // character
    1234567890      // timestamp_us
);
```

### Checking Batch Status

```c
if (meshstatic_batch_is_full(&batch)) {
    // Batch ready for flushing
    const char* csv = meshstatic_batch_get_csv(&batch);
    uint32_t length = meshstatic_batch_get_csv_length(&batch);

    // [Component 2 will save this CSV to flash]

    meshstatic_batch_reset(&batch);
}
```

### Statistics

```c
uint32_t count, csv_length, batch_id;
meshstatic_batch_get_stats(&batch, &count, &csv_length, &batch_id);
```

---

## Memory Usage

| Item | Size |
|------|------|
| CSV Buffer | 200 bytes |
| Keystroke Array | ~28 bytes (4 keystrokes max) |
| Metadata | 24 bytes |
| **Total per Batch** | **~252 bytes** |

---

## Limits and Constraints

| Constraint | Value | Reason |
|------------|-------|--------|
| Max CSV Size | 200 bytes | Per requirement |
| Max Keystrokes | ~4 per batch | CSV size limit |
| CSV Line Length | ~25 bytes avg | Header + data fields |
| Header Size | 42 bytes | Fixed CSV header |

---

## Building and Testing

### Compile Test Program

```bash
cd ~/Desktop/ste/MeshstaticModule
gcc test_batch.c meshstatic_batch.c -o test_batch
```

### Run Tests

```bash
./test_batch
```

### Expected Output

```
╔═══════════════════════════════════════════════════════════╗
║   Meshstatic Batch Manager Test Suite                    ║
║   Component 1: CSV Batch with 200-Byte Limit             ║
╚═══════════════════════════════════════════════════════════╝

=== Test 1: Batch Initialization ===
✓ Batch initialized
========================================
Batch Statistics:
  Batch ID:      1
  Keystrokes:    0 / 4
  CSV Length:    42 / 200 bytes
  Needs Flush:   NO
  Time Range:    0 - 0 us
========================================
✓ CSV header present

=== Test 2: Add Keystrokes ===
✓ Added keystroke: 'H' (scancode=0x0B, ts=...)
✓ Added keystroke: 'e' (scancode=0x08, ts=...)
...

=== Test 3: 200-Byte Limit Enforcement ===
✓ Batch reached limit after 4 keystrokes
✓ CSV length within 200-byte limit: 195 bytes

=== All Tests Complete ===
Component 1 is ready for integration!
```

---

## Integration with RP2350 (Future)

### Core 1 Integration Point

```c
// In capture_v2.cpp, after keystroke decode:
#include "meshstatic_batch.h"

static meshstatic_batch_t g_meshstatic_batch;

void capture_controller_core1_main_v2(void) {
    meshstatic_batch_init(&g_meshstatic_batch);

    while (g_capture_running) {
        // ... existing USB capture code ...

        // After successful keystroke decode:
        if (keystroke_valid) {
            meshstatic_batch_add(&g_meshstatic_batch,
                                scancode,
                                modifier,
                                character,
                                timestamp_us);

            // Check if batch ready
            if (meshstatic_batch_is_full(&g_meshstatic_batch)) {
                // Component 2: Save CSV to flash
                // [To be implemented in next session]
                meshstatic_batch_reset(&g_meshstatic_batch);
            }
        }
    }
}
```

---

## Next Steps (Component 2)

Component 2 will provide:

1. **Flash Storage Interface** (`meshstatic_storage.h/c`)
   - Save CSV batches to LittleFS
   - Manage batch files (create, delete, list)
   - Handle 200-byte file limit

2. **File Naming Convention**
   - Format: `batch_XXXXX.csv` (e.g., `batch_00001.csv`)
   - Sequential numbering
   - Auto-cleanup of old files

3. **Transfer Preparation**
   - Export batch files for transmission
   - Batch metadata tracking
   - Ready for Component 3 (transfer module)

---

## License

BSD-3-Clause
