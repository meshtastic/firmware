# Batch Data Format Design

## Problem Statement

Slave devices capture keystrokes and need to transmit them to the master over Meshtastic with:
- **Accurate timestamps** - Unix epoch for when data was captured
- **Compact encoding** - LoRa has limited payload (~200 bytes recommended)
- **Reliable delivery** - Data integrity across mesh network
- **Memory efficiency** - Slaves have limited RAM/storage

### Current Approach

```
[Unix Epoch (4 bytes)] [Keystrokes until Enter or 5-min timeout]
[Delta (2 bytes)] [Keystrokes...]
...
```

**Limitations:**
- Delta overflow: `uint16` max = 65,535 seconds (~18 hours), then what?
- 200-byte batch limit reached quickly with timestamps overhead
- No compression of keystroke data
- Single-packet assumption may not scale

---

## Constraints Analysis

### Meshtastic Payload Limits

| Metric | Value | Source |
|--------|-------|--------|
| Max LoRa packet | 256 bytes | [Meshtastic Forum](https://meshtastic.discourse.group/t/lora-packet-size-in-meshtastic/3288) |
| Max usable payload | 228-237 bytes | Protocol overhead |
| **Recommended payload** | **< 200 bytes** | Reliability consideration |
| Typical packets | 40-70 bytes | Real-world usage |

**Key insight:** Messages > 200 bytes get fragmented, increasing loss probability in multi-hop networks.

### Slave Device Constraints

| Resource | Typical Limit |
|----------|---------------|
| RAM | 256KB - 512KB |
| Flash | 4MB - 16MB |
| Processing | Low-power MCU |

---

## Research: Encoding Techniques

### 1. Delta Encoding

Store differences between values instead of absolute values.

```
Absolute: [1700000000, 1700000060, 1700000120] = 12 bytes
Delta:    [1700000000, 60, 60] = 6 bytes (base + deltas)
```

**Source:** [Wikipedia - Delta Encoding](https://en.wikipedia.org/wiki/Delta_encoding)

### 2. Delta-of-Deltas

For regular intervals, store the difference of differences.

```
Timestamps:     [0, 60, 120, 180, 240]
Deltas:         [60, 60, 60, 60]
Delta-of-Delta: [60, 0, 0, 0]  <- Often just 0s, can use 1 bit!
```

**Best for:** Regular 5-minute intervals (our use case!)

**Source:** [Gorilla Algorithm - Facebook](https://www.tigerdata.com/blog/time-series-compression-algorithms-explained)

### 3. Variable-Length Integers (Varint)

Encode integers using fewer bytes for smaller values.

| Value Range | Bytes Used |
|-------------|------------|
| 0 - 127 | 1 byte |
| 128 - 16,383 | 2 bytes |
| 16,384 - 2,097,151 | 3 bytes |

**How it works:** MSB (bit 7) indicates continuation. Lower 7 bits are payload.

```c
// Encoding example
void encode_varint(uint32_t value, uint8_t* buf, int* len) {
    *len = 0;
    while (value > 127) {
        buf[(*len)++] = (value & 0x7F) | 0x80;  // Set MSB
        value >>= 7;
    }
    buf[(*len)++] = value & 0x7F;  // Clear MSB (last byte)
}
```

**Source:** [Protocol Buffers Encoding](https://protobuf.dev/programming-guides/encoding/)

### 4. ZigZag Encoding (for signed values)

Maps signed integers to unsigned for efficient varint encoding.

```
0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3, 2 -> 4, ...
Formula: (n << 1) ^ (n >> 31)  // for 32-bit
```

**Source:** [Protocol Buffers ZigZag](https://protobuf.dev/programming-guides/encoding/)

### 5. Sprintz Algorithm (IoT-optimized)

- State-of-the-art compression for time-series
- **< 1KB memory** requirement
- Virtually no latency
- Combines delta coding + zigzag + bit packing

**Source:** [Sprintz Paper](https://arxiv.org/pdf/1808.02515)

### 6. Run-Length Encoding (RLE)

Compress repeated values.

```
[60, 60, 60, 60, 60] -> [5, 60] (count, value)
```

**Best for:** Regular intervals like our 5-minute deltas.

---

## Solution Proposals

### Option A: Enhanced Delta with Varint

**Format:**
```
Batch Header (6 bytes):
  [Batch ID: 2 bytes]
  [Base Timestamp: 4 bytes (Unix epoch)]

Records (variable):
  [Delta: 1-3 bytes (varint)]
  [Length: 1 byte]
  [Data: N bytes]
  ...
```

**Delta Overflow Solution:** Use varint - supports up to 2,097,151 seconds (~24 days) in 3 bytes.

**Pros:**
- Simple implementation
- Handles overflow gracefully
- No compression complexity

**Cons:**
- No data compression
- Keystroke data sent as-is

**Example:**
```
Base: 1700000000 (4 bytes)
Record 1: delta=0 (1 byte), len=5, "Hello"
Record 2: delta=300 (2 bytes), len=6, "World!"
Total: 4 + 1 + 1 + 5 + 2 + 1 + 6 = 20 bytes
```

---

### Option B: Segmented Batches with Continuation

**Format:**
```
Batch Header (8 bytes):
  [Batch ID: 2 bytes]
  [Segment: 1 byte] (0 = first, 1-254 = continuation, 255 = last)
  [Total Segments: 1 byte]
  [Base Timestamp: 4 bytes] (only in segment 0)

Records:
  [Delta: varint]
  [Length: 1 byte]
  [Data: N bytes]
```

**Pros:**
- Supports unlimited data per batch
- Clear reassembly protocol
- Each segment independent

**Cons:**
- More complex state management
- Segment loss requires retransmit

**Example (200-byte batch split into 2 segments):**
```
Segment 0: [ID=1][Seg=0][Total=2][TS=1700000000][Records...]
Segment 1: [ID=1][Seg=255][Total=2][Records continued...]
```

---

### Option C: Delta-RLE Hybrid

**Format:**
```
Batch Header (6 bytes):
  [Batch ID: 2 bytes]
  [Base Timestamp: 4 bytes]

Record Group (RLE for regular intervals):
  [Type: 1 byte] 0x00 = RLE group, 0x01 = irregular

  If RLE (type=0x00):
    [Count: 1 byte]
    [Delta: varint] (repeated delta value)
    [Records: count * (len + data)]

  If Irregular (type=0x01):
    [Delta: varint]
    [Length: 1 byte]
    [Data: N bytes]
```

**Pros:**
- Optimal for regular 5-minute intervals
- Falls back to irregular for Enter-key events
- High compression ratio

**Cons:**
- More complex parsing
- Mixed mode adds overhead

**Example (5 records at 5-min intervals):**
```
Without RLE: 5 * (2 + 1 + data) = 15 bytes overhead
With RLE:    1 + 1 + 2 + 5*(1 + data) = 9 bytes overhead
Savings: 40% on timing data
```

---

### Option D: Sliding Window with Epoch Reset

**Format:**
```
Batch Header (7 bytes):
  [Batch ID: 2 bytes]
  [Base Timestamp: 4 bytes]
  [Window Size: 1 byte] (max delta in this batch, for validation)

Records:
  [Flags: 1 byte]
    Bit 0: Has new epoch (1 = next 4 bytes are new timestamp)
    Bit 1-7: Reserved

  [New Timestamp: 4 bytes] (only if flag bit 0 set)
  [Delta: 1-2 bytes] (from current epoch)
  [Length: 1 byte]
  [Data: N bytes]
```

**Delta Overflow Solution:** Insert new epoch marker when delta would overflow.

**Pros:**
- Handles gaps gracefully (device off, no keystrokes)
- Self-documenting time windows
- Clear recovery from time jumps

**Cons:**
- Extra bytes for epoch resets
- Slightly complex flag parsing

---

### Option E: Multi-Packet Protocol (RECOMMENDED)

**For large data, implement fragmentation similar to LoRaWAN SCHC.**

**Format:**
```
Packet Header (4 bytes):
  [Batch ID: 2 bytes]
  [Packet Info: 1 byte]
    Bits 0-5: Packet number (0-63)
    Bit 6: More fragments flag
    Bit 7: Reserved
  [Checksum: 1 byte] (CRC-8 of payload)

First Packet Only (additional 4 bytes):
  [Base Timestamp: 4 bytes]
  [Total Expected: 1 byte]

Payload (up to 190 bytes):
  [Records...]
```

**Record Format (same across all packets):**
```
  [Delta: varint, 1-3 bytes]
  [Length: 1 byte, 0 = Enter key marker]
  [Data: 0-255 bytes]
```

**Special Cases:**
- `Length = 0`: Enter key pressed (no data, just timestamp marker)
- `Length = 255`: Continuation in next record (for data > 254 bytes)

**Reassembly on Master:**
1. Buffer packets by Batch ID
2. Verify all packets received (0 to N where N has More=0)
3. Concatenate payloads
4. Parse records
5. ACK or request retransmit

**Pros:**
- Scalable to any data size
- Works within Meshtastic constraints
- Built-in error detection
- Industry-standard approach (SCHC-inspired)

**Cons:**
- Most complex implementation
- Requires packet buffering on master
- Retransmit logic needed

**Source:** [LoRa Alliance Fragmentation Spec](https://resources.lora-alliance.org/technical-specifications/ts004-2-0-0-fragmented-data-block-transport)

---

## Comparison Matrix

| Feature | Option A | Option B | Option C | Option D | Option E |
|---------|----------|----------|----------|----------|----------|
| Complexity | Low | Medium | Medium | Medium | High |
| Max Data | 190 bytes | Unlimited | 190 bytes | 190 bytes | Unlimited |
| Compression | None | None | Good | None | None |
| Overflow Handling | Varint | Varint | Varint | Epoch Reset | Varint |
| Error Recovery | None | Segment retx | None | None | Packet retx |
| Memory (Slave) | Low | Medium | Low | Low | Medium |
| Memory (Master) | Low | Medium | Low | Low | High |

---

## Recommended Approach

### Phase 1: Start with Option A (Simple)

For initial implementation:

```c
// Batch format (fits in single 200-byte packet)
typedef struct {
    uint16_t batch_id;
    uint32_t base_timestamp;
    uint8_t record_count;
    uint8_t reserved;
    // Followed by records...
} BatchHeader;  // 8 bytes

// Each record
typedef struct {
    // Delta is varint (1-3 bytes)
    uint8_t data_length;
    // Data follows (0-254 bytes)
} RecordHeader;
```

**Varint Implementation:**
```c
int encode_varint(uint32_t value, uint8_t* buf) {
    int len = 0;
    while (value > 127) {
        buf[len++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    buf[len++] = value;
    return len;
}

uint32_t decode_varint(uint8_t* buf, int* bytes_read) {
    uint32_t result = 0;
    int shift = 0;
    *bytes_read = 0;

    do {
        result |= (buf[*bytes_read] & 0x7F) << shift;
        shift += 7;
    } while (buf[(*bytes_read)++] & 0x80);

    return result;
}
```

### Phase 2: Add Fragmentation (Option E) When Needed

When batches exceed 190 bytes, upgrade to multi-packet protocol.

### Phase 3: Add Compression (Option C) If Bandwidth Constrained

If regular intervals dominate, add RLE for delta compression.

---

## Wire Format Specification

### Current Protocol: TimestampedBatch (MessageType 0x05)

```
Offset  Size  Field
------  ----  -----
0       4     Batch ID (uint32)
4       4     Batch Timestamp (uint32, Unix epoch)
8       1     Record Count (uint8)
9       1     Reserved

For each record:
+0      2     Delta seconds (uint16) <- PROBLEM: Max 18 hours
+2      1     Data length (uint8)
+3      N     Data (variable)
```

### Proposed: TimestampedBatchV2 (MessageType 0x06)

```
Offset  Size  Field
------  ----  -----
0       1     Version (0x02)
1       1     Flags
              Bit 0: Has continuation packet
              Bit 1: Uses compression
              Bit 2-7: Reserved
2       2     Batch ID (uint16)
4       4     Batch Timestamp (uint32, Unix epoch)
8       1     Record Count (uint8)
9       1     CRC-8 (of records only)

For each record:
+0      1-3   Delta seconds (varint)
+N      1     Data length (uint8, 0 = Enter marker)
+N+1    M     Data (variable, 0-254 bytes)
```

**Changes:**
- Varint for delta (supports up to 24 days)
- Version byte for future compatibility
- CRC-8 for integrity
- Flags for extensions

---

## Data Compression Alternatives

Beyond timestamp encoding, we need to compress the actual keystroke data. Here are the options researched:

### 1. SMAZ2 (Current Approach)

**Source:** [GitHub - antirez/smaz2](https://github.com/antirez/smaz2)

SMAZ2 is designed specifically for short messages on LoRa and embedded devices.

| Metric | Value |
|--------|-------|
| RAM Usage | < 2KB |
| Best For | English text, short messages |
| Compression | 40-50% typical |
| Worst Case | Rarely enlarges input |

**How it works:**
- Pre-computed bigrams and words tables (256 words)
- Encodes common patterns efficiently
- Falls back gracefully for unknown patterns

**Example compression:**
```
"This is a small string" -> 50% smaller
"the end"                -> 58% smaller
"foobar"                 -> 34% smaller
"not-a-g00d-Exampl333"   -> 15% LARGER (numbers hurt)
```

**Pros:**
- Designed for LoRa specifically
- Very low memory footprint
- Good for English text

**Cons:**
- Poor with numbers/symbols
- Fixed dictionary (can't adapt)
- English-biased

---

### 2. Heatshrink (LZSS-based)

**Source:** [GitHub - atomicobject/heatshrink](https://github.com/atomicobject/heatshrink)

Ultra-lightweight compression for real-time embedded systems.

| Metric | Value |
|--------|-------|
| RAM Usage | 50-300 bytes |
| ROM Usage | ~800 bytes (AVR) |
| Best For | General data, streaming |
| Compression | Variable (data-dependent) |

**Configuration options:**
```c
// Typical embedded config
#define HEATSHRINK_WINDOW_BITS 8   // 256-byte window
#define HEATSHRINK_LOOKAHEAD_BITS 4 // 16-byte lookahead

// Severely constrained
#define HEATSHRINK_WINDOW_BITS 5   // 32-byte window
#define HEATSHRINK_LOOKAHEAD_BITS 3 // 8-byte lookahead
```

**Pros:**
- Extremely low memory (50 bytes possible!)
- Streaming compression (no buffering needed)
- Works with any data type
- Well-tested on Arduino/ESP32

**Cons:**
- Lower compression ratio than SMAZ for text
- Needs tuning for optimal results
- More CPU cycles

---

### 3. Huffman Encoding (Static Dictionary)

**Source:** [Wikipedia - Huffman coding](https://en.wikipedia.org/wiki/Huffman_coding)

Assign variable-length codes based on character frequency.

| Metric | Value |
|--------|-------|
| RAM Usage | ~256 bytes (for table) |
| Best For | Known character distribution |
| Compression | ~48% typical for text |

**Custom keystroke dictionary approach:**

```c
// Pre-computed for English keystrokes
// Most common: e, t, a, o, i, n, s, r, h, l
static const HuffmanCode keystroke_table[] = {
    {'e', 0b110,     3},   // 3 bits
    {'t', 0b111,     3},
    {'a', 0b1000,    4},
    {' ', 0b1001,    4},   // Space is common!
    {'o', 0b1010,    4},
    // ... less common = more bits
    {'q', 0b11111110, 8},
    {'z', 0b11111111, 8},
};
```

**Pros:**
- Optimal for known distributions
- Zero overhead if table is pre-shared
- Simple decode (bit-by-bit tree walk)

**Cons:**
- Overhead negates gains for short strings
- Fixed to expected distribution
- Bit manipulation complexity

---

### 4. Adaptive Dictionary (Learn-as-you-go)

Build a dictionary from observed patterns specific to this user's typing.

**Concept:**
```
Initial: Empty dictionary
After "Hello": Add "Hello" = token 0
After "Hello World": Add "World" = token 1, " " = token 2
Next "Hello": Emit token 0 instead of 5 bytes
```

**Implementation sketch:**
```c
#define MAX_DICT_ENTRIES 64
#define MAX_WORD_LEN 16

typedef struct {
    char word[MAX_WORD_LEN];
    uint8_t len;
    uint16_t frequency;
} DictEntry;

// Sync dictionary periodically with master
// Master can request dictionary dump
// Or include mini-dictionary in batch header
```

**Pros:**
- Adapts to user's vocabulary
- Better compression over time
- Handles domain-specific terms

**Cons:**
- Cold start (no compression initially)
- Dictionary sync complexity
- Memory for dictionary storage

---

### 5. Hybrid: SMAZ2 + Circular Buffer

**Your current approach - analyzed:**

```
┌─────────────────────────────────────────┐
│           Circular Buffer               │
│  ┌─────┬─────┬─────┬─────┬─────┐       │
│  │ Raw │ Raw │ Raw │ ... │ Raw │        │
│  │ Key │ Key │ Key │     │ Key │        │
│  └─────┴─────┴─────┴─────┴─────┘       │
│            │                            │
│            ▼                            │
│  ┌─────────────────────────────┐       │
│  │    SMAZ2 Compression        │       │
│  │    (on-the-fly)             │       │
│  └─────────────────────────────┘       │
│            │                            │
│            ▼                            │
│  ┌─────────────────────────────┐       │
│  │  Compressed Output Buffer   │       │
│  │  (stop when full/no gain)   │       │
│  └─────────────────────────────┘       │
└─────────────────────────────────────────┘
```

**When to stop compressing:**
1. Output buffer reaches limit (190 bytes)
2. Compression ratio drops below threshold (e.g., 0.9)
3. Delta timestamp would overflow

**Pros:**
- Already implemented
- Good balance of complexity vs. compression
- Handles variable input gracefully

**Cons:**
- SMAZ2 struggles with numbers
- Fixed English dictionary

---

### 6. Two-Stage Compression

Combine timestamp optimization with data compression.

```
Stage 1: Delta-RLE for timestamps (save 40% on timing)
Stage 2: SMAZ2/Heatshrink for data (save 40-50% on content)

Total savings: Up to 70% in ideal cases
```

**Format:**
```
[Header: 8 bytes]
[Compressed Timestamps: RLE-encoded deltas]
[Compressed Data: SMAZ2/Heatshrink encoded]
```

**Decode order:**
1. Decompress timestamps -> get record boundaries
2. Decompress data -> get keystroke content
3. Merge by index

---

### 7. Miniz (zlib-compatible)

**Source:** [GitHub - lbernstone/miniz-esp32](https://github.com/lbernstone/miniz-esp32)

Full zlib/Deflate implementation, available in ESP32 ROM.

| Metric | Value |
|--------|-------|
| RAM Usage | ~44KB (default), tunable |
| Best For | Larger data blocks |
| Compression | Excellent (industry standard) |

**ESP32 ROM functions available:**
```c
#include "rom/miniz.h"
// tdefl_* for compression
// tinfl_* for decompression
```

**Pros:**
- Best compression ratios
- Industry standard (decompress anywhere)
- Already in ESP32 ROM (no code size penalty)

**Cons:**
- High RAM usage (32KB dictionary default)
- Overkill for small messages
- Better for batching multiple records

---

## Compression Comparison Matrix

| Algorithm | RAM | Compression | Speed | Best For |
|-----------|-----|-------------|-------|----------|
| **SMAZ2** | 2KB | 40-50% | Fast | Short English text |
| **Heatshrink** | 50-300B | 20-40% | Medium | Streaming, any data |
| **Huffman** | 256B | ~48% | Fast | Known distribution |
| **Adaptive Dict** | 1-4KB | 50-70%* | Medium | Repetitive input |
| **Miniz** | 44KB | 60-80% | Slow | Large batches |

*After warm-up period

---

## Recommended Compression Strategy

### For Keystroke Data:

**Primary: SMAZ2** (your current approach)
- Keep using for text compression
- Good fit for English keystrokes

**Enhancement: Add number handling**
```c
// Before SMAZ2, encode numbers specially
// "test123" -> "test" + [NUM_MARKER, 1, 2, 3]
// Decode: expand NUM_MARKER sequences
```

**Fallback: Heatshrink**
- When SMAZ2 expands input (numbers, symbols)
- Flag in header: `compression_type` field

### Batch Format with Compression Flag:

```
Byte 0: Flags
  Bit 0-1: Compression type
    00 = None (raw)
    01 = SMAZ2
    10 = Heatshrink
    11 = Reserved
  Bit 2: Has continuation
  Bit 3-7: Reserved

Byte 1: Uncompressed size (for validation)
Byte 2-N: Compressed data
```

---

## References

### Compression Algorithms
- [Delta Encoding - Wikipedia](https://en.wikipedia.org/wiki/Delta_encoding)
- [Sprintz: Time Series Compression for IoT](https://arxiv.org/pdf/1808.02515)
- [Time-series Compression Algorithms](https://www.tigerdata.com/blog/time-series-compression-algorithms-explained)
- [Dynamic Bit Packing for Sensors](https://www.mdpi.com/1424-8220/23/20/8575)

### Text/Data Compression Libraries
- [SMAZ2 - Short message compression for LoRa](https://github.com/antirez/smaz2)
- [SMAZ - Original short string compression](https://github.com/antirez/smaz)
- [Heatshrink - Embedded compression library](https://github.com/atomicobject/heatshrink)
- [Miniz-ESP32 - zlib for ESP32](https://github.com/lbernstone/miniz-esp32)
- [Huffman Coding - Wikipedia](https://en.wikipedia.org/wiki/Huffman_coding)

### Variable-Length Encoding
- [Protocol Buffers Encoding](https://protobuf.dev/programming-guides/encoding/)
- [Variable-Length Quantity - Wikipedia](https://en.wikipedia.org/wiki/Variable-length_quantity)
- [GitVarInt - ZigZag + Varint](https://github.com/pvginkel/GitVarInt)

### Fragmentation Protocols
- [RFC 9011 - SCHC over LoRaWAN](https://datatracker.ietf.org/doc/html/rfc9011)
- [LoRa Alliance Fragmentation Spec](https://resources.lora-alliance.org/technical-specifications/ts004-2-0-0-fragmented-data-block-transport)
- [LoRaFFEC - Fragmentation & FEC](https://hal.science/hal-02861091v1/file/FragmentationAndForwardErrorCorrectionForLoRaWANSmallMTUNetworks_HAL.pdf)

### Meshtastic Constraints
- [Meshtastic Packet Size Discussion](https://meshtastic.discourse.group/t/lora-packet-size-in-meshtastic/3288)
- [Meshtastic Encryption Limitations](https://meshtastic.org/docs/about/overview/encryption/limitations/)
