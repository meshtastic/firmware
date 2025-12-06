# Meshstatic Module - Quick Start Guide

**Component 1: Batch Manager** - Ready to Use! ✅

---

## 📦 What You Got

```
~/Desktop/ste/MeshstaticModule/
├── meshstatic_batch.h              # Public API (include this)
├── meshstatic_batch.c              # Implementation
├── test_batch.c                    # Working test suite
├── Makefile                        # Build system
├── README.md                       # Full documentation
├── IMPLEMENTATION_SUMMARY.md       # Technical details
└── QUICK_START.md                  # This file
```

---

## 🚀 Quick Test

```bash
cd ~/Desktop/ste/MeshstaticModule
make test
```

**Expected Output:**
```
✓ Build complete: test_batch
Running Component 1 tests...
...
=== All Tests Complete ===
Component 1 is ready for integration!
```

---

## 📝 Minimal Usage Example

```c
#include "meshstatic_batch.h"

// 1. Initialize
meshstatic_batch_t batch;
meshstatic_batch_init(&batch);

// 2. Add keystrokes (from USB capture)
meshstatic_batch_add(&batch, 0x04, 0x00, 'a', timestamp_us);
meshstatic_batch_add(&batch, 0x05, 0x00, 'b', timestamp_us);

// 3. Check if full (200-byte limit)
if (meshstatic_batch_is_full(&batch)) {
    // 4. Get CSV string
    const char* csv = meshstatic_batch_get_csv(&batch);

    // 5. [Save to flash - Component 2]
    // save_to_flash(csv, batch.meta.batch_id);

    // 6. Reset for next batch
    meshstatic_batch_reset(&batch);
}
```

---

## 📊 CSV Output Format

```csv
timestamp_us,scancode,modifier,character
1234567890,0x04,0x00,a
1234568000,0x05,0x02,B
1234569000,0x28,0x00,↵
```

**Field Meanings:**
- `timestamp_us`: Microsecond timestamp (when keystroke captured)
- `scancode`: HID scancode (0x04 = 'A', 0x05 = 'B', etc.)
- `modifier`: Modifier flags (0x02 = Shift, 0x01 = Ctrl, etc.)
- `character`: ASCII character

---

## 📏 Limits

| Constraint | Value |
|------------|-------|
| Max CSV size | 200 bytes |
| Max keystrokes per batch | ~4 |
| Memory per batch | 252 bytes |

---

## 🔧 API Functions

```c
// Initialize batch
void meshstatic_batch_init(meshstatic_batch_t* batch);

// Add keystroke to batch
bool meshstatic_batch_add(meshstatic_batch_t* batch,
                          uint8_t scancode,
                          uint8_t modifier,
                          char character,
                          uint32_t timestamp_us);

// Check if batch full
bool meshstatic_batch_is_full(const meshstatic_batch_t* batch);

// Get CSV string
const char* meshstatic_batch_get_csv(const meshstatic_batch_t* batch);

// Get CSV length
uint32_t meshstatic_batch_get_csv_length(const meshstatic_batch_t* batch);

// Reset batch
void meshstatic_batch_reset(meshstatic_batch_t* batch);

// Get statistics
void meshstatic_batch_get_stats(const meshstatic_batch_t* batch,
                                uint32_t* count,
                                uint32_t* csv_length,
                                uint32_t* batch_id);
```

---

## ✅ What Works Now

- ✅ CSV batch creation
- ✅ 200-byte limit enforcement
- ✅ Automatic batch ID tracking
- ✅ Zero dynamic allocation
- ✅ Core 1 compatible
- ✅ Fully tested

---

## 🔲 What's Next (Component 2)

- 🔲 Flash storage integration (LittleFS)
- 🔲 File operations (create, delete, list)
- 🔲 Batch file management
- 🔲 Power-loss recovery testing

---

## 💡 Integration Tips

### For RP2350 Core 1 Integration

**Add to `capture_v2.cpp`:**

```c
#include "meshstatic_batch.h"

static meshstatic_batch_t g_meshstatic_batch;

// In capture_controller_core1_main_v2():
meshstatic_batch_init(&g_meshstatic_batch);

// After keystroke decode:
if (keystroke_valid) {
    meshstatic_batch_add(&g_meshstatic_batch,
                        scancode,
                        modifier,
                        character,
                        timestamp_us);

    if (meshstatic_batch_is_full(&g_meshstatic_batch)) {
        // Component 2: Save CSV to flash
        // [To be implemented next session]
        meshstatic_batch_reset(&g_meshstatic_batch);
    }
}
```

---

## 📖 More Info

- **Full Documentation**: `README.md`
- **Technical Details**: `IMPLEMENTATION_SUMMARY.md`
- **Test Code**: `test_batch.c`

---

## 🐛 Troubleshooting

**Q: Batch fills up after only 4 keystrokes?**
A: This is expected! The 200-byte CSV limit means ~4 keystrokes max per batch.

**Q: How do I change the batch size?**
A: The 200-byte limit is a requirement. To change it, modify `MESHSTATIC_MAX_BATCH_SIZE` in `meshstatic_batch.h`.

**Q: Can I use this without RP2350?**
A: Yes! Component 1 is standalone C code. It compiles with any C11 compiler.

---

## 📞 Contact

Ready for Component 2 (Storage Manager) in the next session!

**Status**: Component 1 ✅ COMPLETE and TESTED
