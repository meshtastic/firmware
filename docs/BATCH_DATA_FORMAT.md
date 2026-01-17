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

## References

### Compression Algorithms
- [Delta Encoding - Wikipedia](https://en.wikipedia.org/wiki/Delta_encoding)
- [Sprintz: Time Series Compression for IoT](https://arxiv.org/pdf/1808.02515)
- [Time-series Compression Algorithms](https://www.tigerdata.com/blog/time-series-compression-algorithms-explained)
- [Dynamic Bit Packing for Sensors](https://www.mdpi.com/1424-8220/23/20/8575)

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
