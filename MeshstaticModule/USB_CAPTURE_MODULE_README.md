# USBCaptureModule - Independent Core 1 USB Capture

**Single Responsibility**: Capture USB keystrokes on Core 1 → Store in lock-free queue

---

## 🎯 What This Module Does

**ONLY ONE JOB**: Capture USB keystrokes and make them available in a queue.

- ✅ Captures USB data using PIO (Core 1)
- ✅ Decodes keystrokes
- ✅ Stores in lock-free queue (256 events)
- ✅ Provides consumer interface for other modules
- ❌ NO CSV batching (that's another module's job)
- ❌ NO flash storage (that's another module's job)
- ❌ NO transmission (that's another module's job)

---

## 📦 Files

```
USBCaptureModule.h          API header (5.8 KB)
USBCaptureModule.c          Implementation (10.2 KB)
test_usb_capture.c          Test program (3.1 KB)
```

---

## 🚀 Quick Start

### Core 1 Integration (Producer)

```c
#include "USBCaptureModule.h"

// In capture_controller_core1_main_v2():

// 1. Initialize
usb_capture_config_t config = {
    .dp_pin = 20,
    .dm_pin = 21,
    .full_speed_mode = false
};
usb_capture_module_init(&config);
usb_capture_module_start();

// 2. Process in loop (reads PIO, pushes to queue)
while (running) {
    usb_capture_module_process();
}

// 3. Stop
usb_capture_module_stop();
```

### Consumer Integration (Any Module)

```c
// Core 0 or Core 1 consumer (CSV batcher, LoRa TX, etc.)

while (usb_capture_module_available()) {
    keystroke_event_t event;

    if (usb_capture_module_pop(&event)) {
        // Process event (batch it, transmit it, log it, etc.)
        printf("Got keystroke: '%c'\n", event.character);
    }
}
```

---

## 🎮 API Reference

### Initialization (Core 1)

```c
bool usb_capture_module_init(const usb_capture_config_t* config);
bool usb_capture_module_start(void);
bool usb_capture_module_is_running(void);
```

### Processing (Core 1)

```c
// Call this in Core 1 loop
uint32_t usb_capture_module_process(void);
```

### Consumer Interface (Core 0/Core 1)

```c
bool usb_capture_module_available(void);        // Check if events in queue
uint32_t usb_capture_module_get_count(void);   // Get queue size
bool usb_capture_module_pop(keystroke_event_t* event);   // Dequeue event
bool usb_capture_module_peek(keystroke_event_t* event);  // Peek without removing
```

### Statistics (Core 0/Core 1)

```c
void usb_capture_module_get_stats(usb_capture_stats_t* stats);
void usb_capture_module_print_stats(void);
```

---

## 📊 Keystroke Event Structure

```c
typedef struct {
    uint32_t timestamp_us;      // Microsecond timestamp
    uint8_t scancode;           // HID scancode (0x04 = 'A', etc.)
    uint8_t modifier;           // Modifier flags (0x02 = Shift, etc.)
    char character;             // ASCII character ('a', 'B', '\n', etc.)
    keystroke_type_t type;      // Event type (CHAR, ENTER, etc.)
} keystroke_event_t;
```

---

## 🔄 Data Flow

```
Core 1 (USB Capture)                    Core 0 (Consumers)
┌────────────────────┐                  ┌────────────────────┐
│ PIO Hardware       │                  │                    │
│      ↓             │                  │  CSV Batch Module  │
│ Decode Keystroke   │                  │  (reads queue)     │
│      ↓             │                  │      ↓             │
│ usb_capture_module_│  Lock-Free Queue │  Creates CSV       │
│ process()          │  ──────────────> │      ↓             │
│      ↓             │                  │  Saves to Flash    │
│ Push to Queue      │                  │                    │
│                    │                  ├────────────────────┤
└────────────────────┘                  │                    │
                                        │  LoRa TX Module    │
                                        │  (reads queue)     │
                                        │      ↓             │
                                        │  Transmit          │
                                        │                    │
                                        └────────────────────┘
```

---

## ✅ What's Complete

- [x] Lock-free ring buffer queue (256 events)
- [x] Thread-safe for Core1→Core0
- [x] Producer interface (Core 1)
- [x] Consumer interface (Core 0/Core 1)
- [x] Statistics tracking
- [x] Clean, minimal API
- [x] No dependencies on batching/storage
- [x] Ready for integration

---

## 📝 Integration into capture_v2.cpp

**Location**: `/Users/rstown/Desktop/Projects/STE/client_pico/lib/USBCapture/capture_v2.cpp`

```cpp
#include "USBCaptureModule.h"

void capture_controller_core1_main_v2(void)
{
    // Initialize USB capture module
    usb_capture_config_t config = {
        .dp_pin = 20,
        .dm_pin = 21,
        .full_speed_mode = false
    };
    usb_capture_module_init(&config);
    usb_capture_module_start();

    while (g_capture_running_v2)
    {
        // ... existing PIO FIFO reading ...

        // After keystroke decode:
        if (keystroke_valid) {
            // Option 1: Call existing queue push (keep this)
            keystroke_queue_push(g_keystroke_queue_v2, &event);

            // Option 2: Or replace with USBCaptureModule
            // (The module's process() function would do this internally)
        }
    }

    usb_capture_module_stop();
}
```

---

## 📖 Files You Need

**Copy to your project**:
- `USBCaptureModule.h` - API header
- `USBCaptureModule.c` - Implementation

**That's it!** Just 2 files for a complete Core 1 USB capture module with queue.

---

**Module Status**: ✅ Ready for Core 1 integration
**Dependencies**: None (self-contained)
**Queue**: Lock-free (256 events)
**Size**: ~16 KB total
