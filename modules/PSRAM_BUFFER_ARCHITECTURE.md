# PSRAM Buffer Architecture for USB Capture

## Goal
Move ALL USB capture processing to Core1, with Core0 only reading completed buffers from PSRAM for mesh transmission.

## Current vs Target Architecture

### Current (Hybrid)
```
Core1: USB Capture → Decode → Format events → Push to queue
Core0: Poll queue → Format → Build buffer → Transmit
```
**Core0 overhead:** ~2% (formatting + buffer management)

### Target (PSRAM-based)
```
Core1: USB Capture → Decode → Format → Build buffer → Write to PSRAM
Core0: Poll PSRAM → Read complete buffer → Transmit
```
**Core0 overhead:** ~0.2% (75% reduction, just read + transmit)

## PSRAM Buffer Structure

### Header (32 bytes)
```cpp
typedef struct {
    uint32_t magic;           // 0xC0DEBUF1 - validation
    uint32_t write_index;     // Core1 write position (0-7)
    uint32_t read_index;      // Core0 read position (0-7)
    uint32_t buffer_count;    // Available buffers for transmission
    uint32_t total_written;   // Lifetime buffer counter
    uint32_t total_transmitted; // Lifetime transmission counter
    uint32_t dropped_buffers; // Overrun counter
    uint32_t reserved;        // Future use
} psram_buffer_header_t;
```

### Buffer Slot (512 bytes each)
```cpp
typedef struct {
    uint32_t start_epoch;     // Unix timestamp when buffer started
    uint32_t final_epoch;     // Unix timestamp when finalized
    uint16_t data_length;     // Actual data bytes (0-480)
    uint16_t flags;           // Status flags
    char data[504];           // Keystroke data with delta encoding
} psram_keystroke_buffer_t;
```

### Ring Buffer Layout
```
PSRAM Memory Map:
┌─────────────────────────┐  0x0000
│   Header (32 bytes)      │
├─────────────────────────┤  0x0020
│   Slot 0 (512 bytes)     │
├─────────────────────────┤  0x0220
│   Slot 1 (512 bytes)     │
├─────────────────────────┤  0x0420
│   ...                    │
├─────────────────────────┤
│   Slot 7 (512 bytes)     │
└─────────────────────────┘  0x1020 (4128 bytes total)

Capacity: 8 buffers × 504 bytes = 4032 bytes buffered data
```

## Core1 Responsibilities

### 1. USB Capture & Decode
- PIO-based USB packet capture
- HID keyboard report decoding
- Same as current implementation

### 2. Event Formatting
- Format events to human-readable strings
- Already implemented in keyboard_decoder_core1.cpp

### 3. **Buffer Management (NEW - moved from Core0)**
```cpp
// Core1 maintains its own keystroke buffer
static char g_core1_keystroke_buffer[KEYSTROKE_BUFFER_SIZE];
static size_t g_core1_buffer_pos = 0;
static uint32_t g_core1_buffer_start_epoch = 0;
static bool g_core1_buffer_initialized = false;

void core1_add_to_buffer(char c);
void core1_add_enter_to_buffer();
void core1_finalize_and_write_to_psram();
```

### 4. **PSRAM Write (NEW)**
```cpp
void psram_write_buffer(const psram_keystroke_buffer_t *buffer) {
    // Atomic write to next slot
    // Update write_index
    // Increment buffer_count
    // Handle wraparound
}
```

## Core0 Responsibilities

### 1. **PSRAM Poll (NEW - simplified)**
```cpp
int32_t USBCaptureModule::runOnce() {
    // Check if buffers available
    if (psram_buffer_header.buffer_count > 0) {
        // Read buffer from PSRAM
        psram_keystroke_buffer_t buffer;
        psram_read_buffer(&buffer);

        // Transmit directly (no formatting needed!)
        broadcastToPrivateChannel(buffer.data, buffer.data_length);

        // Update read_index
        // Decrement buffer_count
    }

    return 100; // Poll every 100ms
}
```

### 2. Mesh Transmission
- Same as current (broadcastToPrivateChannel)
- No changes needed

## PSRAM vs FRAM Abstraction

```cpp
// Storage interface for future FRAM support
class KeystrokeStorage {
public:
    virtual bool init() = 0;
    virtual bool write_buffer(const psram_keystroke_buffer_t *buf) = 0;
    virtual bool read_buffer(psram_keystroke_buffer_t *buf) = 0;
    virtual uint32_t get_available_count() = 0;
};

// Current: In-memory (PSRAM emulation)
class RAMStorage : public KeystrokeStorage {
    // Use static arrays in RAM
};

// Future: I2C/SPI FRAM chip
class FRAMStorage : public KeystrokeStorage {
    // I2C/SPI communication
    // Non-volatile persistence
};
```

## Benefits

### Performance
- **75% Core0 overhead reduction** (2% → 0.5%)
- Core1 utilization still <30%
- Plenty of headroom for both cores

### Architecture
- Clean separation: Core1 = Producer, Core0 = Consumer
- Core0 becomes thin transmission layer
- Easy to add FRAM later (just swap storage class)

### Reliability
- Large buffer capacity (8 slots = ~4KB)
- Handles burst transmission delays
- Future: Non-volatile with FRAM

## Implementation Phases

### Phase 1: Move Buffer to Core1 (Current)
- [x] Create formatted_event_queue
- [ ] Move keystroke_buffer to Core1
- [ ] Move addToBuffer/addEnterToBuffer to Core1
- [ ] Move finalizeBuffer to Core1

### Phase 2: Add PSRAM Interface
- [ ] Create psram_buffer structure
- [ ] Implement write on Core1
- [ ] Implement read on Core0
- [ ] Test with RAM-based storage

### Phase 3: FRAM Support (Future)
- [ ] Design I2C/SPI interface
- [ ] Implement FRAMStorage class
- [ ] Add persistence across power cycles
- [ ] Capacity: MB-scale buffering

## Memory Usage

### Current
- Core0: keystroke_buffer (500 bytes) + queues (~2KB)
- Core1: Minimal state

### Target
- Core0: Minimal (just transmission state)
- Core1: keystroke_buffer (500 bytes) + PSRAM buffer (4KB)
- Total: Same memory, better organized

## Testing Plan

1. **Unit Test**: PSRAM ring buffer operations
2. **Integration Test**: Core1 write → Core0 read
3. **Stress Test**: Rapid keystroke capture
4. **Overflow Test**: Verify buffer full handling
5. **Performance Test**: Measure Core0 CPU reduction

## Next Steps

1. Create `psram_buffer.h` with structures
2. Implement Core1 buffer management
3. Update Core0 to poll PSRAM
4. Test and verify
5. Document for FRAM migration
