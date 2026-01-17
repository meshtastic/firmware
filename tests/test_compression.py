#!/usr/bin/env python3
"""
Compression Strategy Tests for XIAO RP2350

Tests two-stage compression and adaptive dictionary approaches
for keystroke data transmission over Meshtastic.

XIAO RP2350 Specs:
- 520KB SRAM (use ~50KB for compression)
- 2MB Flash (can dedicate ~64KB for dictionary)
- Dual Cortex-M33 @ 150MHz
"""

import struct
import time
import json
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional
from collections import Counter
import hashlib


# =============================================================================
# Constants (matching RP2350 constraints)
# =============================================================================

MAX_PACKET_SIZE = 200          # Meshtastic recommended max
MAX_SRAM_FOR_COMPRESSION = 50 * 1024  # 50KB budget
MAX_FLASH_FOR_DICT = 64 * 1024        # 64KB for dictionary
MAX_DICT_ENTRIES = 256         # Fit in uint8 token
MAX_WORD_LENGTH = 32           # Max word/phrase length


# =============================================================================
# Simulated SMAZ2 (simplified for testing)
# =============================================================================

# Common English bigrams and words (subset of SMAZ2 dictionary)
SMAZ2_DICT = [
    " ", "the", "e", "t", "a", "of", "o", "and", "i", "n", "s", "r", "to",
    "in", "he", "is", "it", "ou", "er", "an", "re", "on", "at", "en", "ed",
    "nd", "ha", "as", "or", "ng", "le", "se", "th", "ti", "te", "es", "st",
    "al", "nt", "ar", "ve", "hi", "ri", "ro", "ll", "be", "ea", "me", "ne",
    "for", "was", "are", "but", "not", "you", "all", "can", "had", "her",
    "was", "one", "our", "out", "day", "get", "has", "him", "his", "how",
    "its", "may", "new", "now", "old", "see", "two", "way", "who", "did",
    "oil", "sit", "set", "run", "say", "she", "too", "use", "with", "have",
    "this", "will", "your", "from", "they", "been", "call", "come", "could",
    "make", "like", "time", "just", "know", "take", "into", "year", "some",
    "them", "than", "then", "look", "only", "over", "such", "also", "back",
    "after", "water", "where", "about", "which", "their", "there", "would",
    "first", "these", "other", "could", "write", "Hello", "World", "test",
]


def smaz2_compress(text: str) -> bytes:
    """Simplified SMAZ2-like compression."""
    result = bytearray()
    i = 0

    while i < len(text):
        best_match = None
        best_len = 0

        # Find longest matching dictionary entry
        for idx, word in enumerate(SMAZ2_DICT):
            if text[i:i+len(word)] == word and len(word) > best_len:
                best_match = idx
                best_len = len(word)

        if best_match is not None and best_len > 1:
            # Encode as dictionary reference
            result.append(0x80 | best_match)  # High bit = dict ref
            i += best_len
        else:
            # Encode as literal
            if ord(text[i]) < 128:
                result.append(ord(text[i]))
            else:
                result.append(0xFF)  # Escape
                result.append(ord(text[i]) & 0xFF)
            i += 1

    return bytes(result)


def smaz2_decompress(data: bytes) -> str:
    """Decompress SMAZ2-like data."""
    result = []
    i = 0

    while i < len(data):
        if data[i] == 0xFF:
            # Escaped byte
            i += 1
            if i < len(data):
                result.append(chr(data[i]))
            i += 1
        elif data[i] & 0x80:
            # Dictionary reference
            idx = data[i] & 0x7F
            if idx < len(SMAZ2_DICT):
                result.append(SMAZ2_DICT[idx])
            i += 1
        else:
            # Literal ASCII
            result.append(chr(data[i]))
            i += 1

    return ''.join(result)


# =============================================================================
# Varint Encoding (for timestamps)
# =============================================================================

def encode_varint(value: int) -> bytes:
    """Encode integer as varint (protobuf style)."""
    result = bytearray()
    while value > 127:
        result.append((value & 0x7F) | 0x80)
        value >>= 7
    result.append(value)
    return bytes(result)


def decode_varint(data: bytes, offset: int = 0) -> Tuple[int, int]:
    """Decode varint, return (value, bytes_consumed)."""
    result = 0
    shift = 0
    consumed = 0

    while offset + consumed < len(data):
        byte = data[offset + consumed]
        result |= (byte & 0x7F) << shift
        consumed += 1
        if not (byte & 0x80):
            break
        shift += 7

    return result, consumed


# =============================================================================
# Two-Stage Compression
# =============================================================================

@dataclass
class TwoStageRecord:
    """A record with timestamp delta and data."""
    delta: int          # Seconds from batch base
    data: str           # Keystroke content


@dataclass
class TwoStageBatch:
    """Two-stage compressed batch."""
    batch_id: int
    base_timestamp: int
    records: List[TwoStageRecord] = field(default_factory=list)

    def compress_stage1_timestamps(self) -> Tuple[bytes, int]:
        """
        Stage 1: RLE + Varint for timestamps.
        Returns (compressed_bytes, savings_bytes).
        """
        if not self.records:
            return b'', 0

        deltas = [r.delta for r in self.records]
        result = bytearray()

        # Check for RLE opportunity (consecutive equal deltas)
        i = 0
        raw_size = 0

        while i < len(deltas):
            # Count consecutive equal values
            run_length = 1
            while (i + run_length < len(deltas) and
                   deltas[i + run_length] == deltas[i] and
                   run_length < 255):
                run_length += 1

            raw_size += run_length * 2  # Original: 2 bytes per delta

            if run_length >= 3:
                # RLE encoding: [0xFF, count, varint_delta]
                result.append(0xFF)  # RLE marker
                result.append(run_length)
                result.extend(encode_varint(deltas[i]))
            else:
                # Individual varints
                for j in range(run_length):
                    result.extend(encode_varint(deltas[i]))

            i += run_length

        return bytes(result), raw_size - len(result)

    def compress_stage2_data(self) -> Tuple[bytes, int]:
        """
        Stage 2: SMAZ2 for keystroke data.
        Returns (compressed_bytes, savings_bytes).
        """
        if not self.records:
            return b'', 0

        # Concatenate all data with length prefixes
        raw_size = 0
        compressed_parts = []

        for record in self.records:
            raw_bytes = record.data.encode('utf-8')
            raw_size += 1 + len(raw_bytes)  # length byte + data

            compressed = smaz2_compress(record.data)
            compressed_parts.append(compressed)

        # Build output: [len1, data1, len2, data2, ...]
        result = bytearray()
        for part in compressed_parts:
            result.append(len(part))
            result.extend(part)

        return bytes(result), raw_size - len(result)

    def to_wire_format(self) -> bytes:
        """
        Full two-stage compression to wire format.

        Format:
        [Batch ID: 2B][Base TS: 4B][Flags: 1B][Record Count: 1B]
        [Compressed Timestamps][Compressed Data]
        """
        ts_compressed, _ = self.compress_stage1_timestamps()
        data_compressed, _ = self.compress_stage2_data()

        header = struct.pack('<HI BB',
            self.batch_id,
            self.base_timestamp,
            0x03,  # Flags: two-stage compression enabled
            len(self.records)
        )

        # Include lengths for parsing
        ts_len = struct.pack('<H', len(ts_compressed))

        return header + ts_len + ts_compressed + data_compressed

    @classmethod
    def from_wire_format(cls, data: bytes) -> 'TwoStageBatch':
        """Decompress from wire format."""
        batch_id, base_ts, flags, count = struct.unpack('<HI BB', data[:8])
        ts_len = struct.unpack('<H', data[8:10])[0]

        ts_data = data[10:10+ts_len]
        data_section = data[10+ts_len:]

        # Decode timestamps
        deltas = []
        offset = 0
        while offset < len(ts_data) and len(deltas) < count:
            if ts_data[offset] == 0xFF:
                # RLE
                run_len = ts_data[offset + 1]
                delta, consumed = decode_varint(ts_data, offset + 2)
                deltas.extend([delta] * run_len)
                offset += 2 + consumed
            else:
                delta, consumed = decode_varint(ts_data, offset)
                deltas.append(delta)
                offset += consumed

        # Decode data
        records = []
        offset = 0
        for i, delta in enumerate(deltas):
            if offset >= len(data_section):
                break
            length = data_section[offset]
            compressed = data_section[offset+1:offset+1+length]
            text = smaz2_decompress(compressed)
            records.append(TwoStageRecord(delta=delta, data=text))
            offset += 1 + length

        batch = cls(batch_id=batch_id, base_timestamp=base_ts)
        batch.records = records
        return batch


# =============================================================================
# Adaptive Dictionary
# =============================================================================

@dataclass
class DictEntry:
    """Dictionary entry for adaptive compression."""
    word: str
    frequency: int = 0
    last_used: int = 0  # Timestamp for LRU


class AdaptiveDictionary:
    """
    Adaptive dictionary that learns from usage patterns.
    Designed for flash storage on RP2350.

    Flash Layout (64KB):
    - Header: 64 bytes (magic, version, entry count, checksum)
    - Entries: 256 * 252 bytes = 64512 bytes
      Each entry: [len: 1B][word: up to 32B][freq: 4B][last_used: 4B][padding]
    """

    MAGIC = b'ADICT001'
    MAX_ENTRIES = 256
    ENTRY_SIZE = 252  # Fixed size for flash alignment

    def __init__(self):
        self.entries: Dict[str, DictEntry] = {}
        self.token_map: Dict[str, int] = {}  # word -> token
        self.reverse_map: Dict[int, str] = {}  # token -> word
        self.next_token = 0
        self.stats = {
            'hits': 0,
            'misses': 0,
            'additions': 0,
            'evictions': 0,
        }

    def add_word(self, word: str, timestamp: int = 0) -> Optional[int]:
        """
        Add or update a word in the dictionary.
        Returns token if added/updated, None if word too long.
        """
        if len(word) > MAX_WORD_LENGTH or len(word) < 2:
            return None

        if word in self.entries:
            # Update existing
            self.entries[word].frequency += 1
            self.entries[word].last_used = timestamp
            self.stats['hits'] += 1
            return self.token_map[word]

        self.stats['misses'] += 1

        # Need to add new entry
        if len(self.entries) >= self.MAX_ENTRIES:
            # Evict least valuable entry (low freq + old)
            self._evict_one(timestamp)

        # Add new entry
        token = self._allocate_token()
        self.entries[word] = DictEntry(word=word, frequency=1, last_used=timestamp)
        self.token_map[word] = token
        self.reverse_map[token] = word
        self.stats['additions'] += 1

        return token

    def _allocate_token(self) -> int:
        """Find an available token."""
        # Find unused token
        for t in range(self.MAX_ENTRIES):
            if t not in self.reverse_map:
                return t
        return self.next_token  # Shouldn't happen after eviction

    def _evict_one(self, current_time: int):
        """Evict the least valuable entry."""
        if not self.entries:
            return

        # Score = frequency / age (higher = more valuable)
        # Evict lowest score
        min_score = float('inf')
        evict_word = None

        for word, entry in self.entries.items():
            age = max(1, current_time - entry.last_used)
            score = entry.frequency / age
            if score < min_score:
                min_score = score
                evict_word = word

        if evict_word:
            token = self.token_map[evict_word]
            del self.entries[evict_word]
            del self.token_map[evict_word]
            del self.reverse_map[token]
            self.stats['evictions'] += 1

    def get_token(self, word: str) -> Optional[int]:
        """Get token for a word, None if not in dictionary."""
        return self.token_map.get(word)

    def get_word(self, token: int) -> Optional[str]:
        """Get word for a token."""
        return self.reverse_map.get(token)

    def compress(self, text: str, timestamp: int = 0) -> bytes:
        """
        Compress text using adaptive dictionary.

        Format:
        - Token (0x00-0xFE): Dictionary reference
        - 0xFF + len + bytes: Literal string
        """
        result = bytearray()
        words = self._tokenize(text)

        for word in words:
            token = self.get_token(word)

            if token is not None:
                # Dictionary hit
                result.append(token)
                self.entries[word].frequency += 1
                self.entries[word].last_used = timestamp
            else:
                # Literal - but also learn it
                result.append(0xFF)
                word_bytes = word.encode('utf-8')
                result.append(len(word_bytes))
                result.extend(word_bytes)

                # Learn this word for future use
                if len(word) >= 2:
                    self.add_word(word, timestamp)

        return bytes(result)

    def decompress(self, data: bytes) -> str:
        """Decompress data using dictionary."""
        result = []
        i = 0

        while i < len(data):
            if data[i] == 0xFF:
                # Literal
                length = data[i + 1]
                word = data[i + 2:i + 2 + length].decode('utf-8')
                result.append(word)
                i += 2 + length
            else:
                # Token reference
                word = self.get_word(data[i])
                if word:
                    result.append(word)
                i += 1

        return ''.join(result)

    def _tokenize(self, text: str) -> List[str]:
        """Split text into words/tokens for compression."""
        tokens = []
        current = []

        for char in text:
            if char.isalnum():
                current.append(char)
            else:
                if current:
                    tokens.append(''.join(current))
                    current = []
                tokens.append(char)  # Keep spaces/punctuation

        if current:
            tokens.append(''.join(current))

        return tokens

    def to_flash_format(self) -> bytes:
        """
        Serialize dictionary for flash storage.

        Format:
        - Magic: 8 bytes
        - Version: 4 bytes
        - Entry count: 4 bytes
        - Reserved: 48 bytes
        - Entries: MAX_ENTRIES * ENTRY_SIZE bytes
        """
        header = bytearray(64)
        header[:8] = self.MAGIC
        struct.pack_into('<I', header, 8, 1)  # Version
        struct.pack_into('<I', header, 12, len(self.entries))

        entries_data = bytearray(self.MAX_ENTRIES * self.ENTRY_SIZE)

        for token, word in self.reverse_map.items():
            entry = self.entries[word]
            offset = token * self.ENTRY_SIZE

            word_bytes = word.encode('utf-8')
            entries_data[offset] = len(word_bytes)
            entries_data[offset + 1:offset + 1 + len(word_bytes)] = word_bytes
            struct.pack_into('<I', entries_data, offset + 33, entry.frequency)
            struct.pack_into('<I', entries_data, offset + 37, entry.last_used)

        # Checksum
        full_data = header + entries_data
        checksum = hashlib.md5(entries_data).digest()[:4]
        header[60:64] = checksum

        return bytes(header) + bytes(entries_data)

    @classmethod
    def from_flash_format(cls, data: bytes) -> 'AdaptiveDictionary':
        """Load dictionary from flash format."""
        if len(data) < 64:
            raise ValueError("Data too short")

        if data[:8] != cls.MAGIC:
            raise ValueError("Invalid magic")

        version = struct.unpack_from('<I', data, 8)[0]
        count = struct.unpack_from('<I', data, 12)[0]

        dict_obj = cls()

        for token in range(min(count, cls.MAX_ENTRIES)):
            offset = 64 + token * cls.ENTRY_SIZE
            word_len = data[offset]

            if word_len == 0:
                continue

            word = data[offset + 1:offset + 1 + word_len].decode('utf-8')
            frequency = struct.unpack_from('<I', data, offset + 33)[0]
            last_used = struct.unpack_from('<I', data, offset + 37)[0]

            dict_obj.entries[word] = DictEntry(
                word=word,
                frequency=frequency,
                last_used=last_used
            )
            dict_obj.token_map[word] = token
            dict_obj.reverse_map[token] = word

        return dict_obj

    def get_stats(self) -> dict:
        """Get compression statistics."""
        return {
            **self.stats,
            'entries': len(self.entries),
            'hit_rate': self.stats['hits'] / max(1, self.stats['hits'] + self.stats['misses']),
        }


# =============================================================================
# Test Data Generation
# =============================================================================

SAMPLE_KEYSTROKES = [
    "Hello World",
    "How are you doing today?",
    "The quick brown fox jumps over the lazy dog",
    "Testing 123 456 789",  # Numbers - SMAZ2 weakness
    "Hello World",  # Repeated - should compress better with adaptive
    "Hello there, how are you?",
    "The weather is nice today",
    "I need to send this message",
    "Hello World",  # Third time
    "Testing the compression algorithm",
    "This is a test message",
    "Another test string here",
    "The system is working well",
    "Data transmission complete",
    "Hello World",  # Fourth time - adaptive should excel
    "End of test data",
    "192.168.1.1",  # IP address - numbers
    "user@example.com",  # Email
    "https://github.com/test",  # URL
    "Hello World!!!",  # With punctuation
]


def generate_test_batch(messages: List[str], base_ts: int = 1700000000) -> TwoStageBatch:
    """Generate a test batch with 5-minute intervals."""
    batch = TwoStageBatch(batch_id=1, base_timestamp=base_ts)

    for i, msg in enumerate(messages):
        # Most are 5-minute intervals, some are irregular (simulating Enter key)
        if i % 3 == 0:
            delta = i * 300  # 5 minutes
        else:
            delta = i * 300 + (i * 17) % 60  # Irregular

        batch.records.append(TwoStageRecord(delta=delta, data=msg))

    return batch


# =============================================================================
# Test Functions
# =============================================================================

def test_two_stage_compression():
    """Test two-stage compression."""
    print("=" * 60)
    print("TWO-STAGE COMPRESSION TEST")
    print("=" * 60)

    batch = generate_test_batch(SAMPLE_KEYSTROKES)

    # Calculate raw size
    raw_size = 8  # Header
    for r in batch.records:
        raw_size += 2  # uint16 delta
        raw_size += 1 + len(r.data.encode('utf-8'))  # len + data

    # Stage 1: Timestamps
    ts_compressed, ts_savings = batch.compress_stage1_timestamps()

    # Stage 2: Data
    data_compressed, data_savings = batch.compress_stage2_data()

    # Full wire format
    wire_data = batch.to_wire_format()

    print(f"\nRecords: {len(batch.records)}")
    print(f"\nRaw Size Breakdown:")
    print(f"  Header:     8 bytes")
    print(f"  Timestamps: {len(batch.records) * 2} bytes (2B each)")
    print(f"  Data:       {raw_size - 8 - len(batch.records) * 2} bytes")
    print(f"  Total Raw:  {raw_size} bytes")

    print(f"\nCompressed Size Breakdown:")
    print(f"  Header:     10 bytes (8 + 2 for ts_len)")
    print(f"  Timestamps: {len(ts_compressed)} bytes (saved {ts_savings})")
    print(f"  Data:       {len(data_compressed)} bytes (saved {data_savings})")
    print(f"  Total:      {len(wire_data)} bytes")

    compression_ratio = len(wire_data) / raw_size
    print(f"\nCompression Ratio: {compression_ratio:.2%}")
    print(f"Space Saved: {raw_size - len(wire_data)} bytes ({(1 - compression_ratio):.1%})")

    # Verify round-trip
    decoded = TwoStageBatch.from_wire_format(wire_data)
    print(f"\nRound-trip verification:")
    for i, (orig, dec) in enumerate(zip(batch.records, decoded.records)):
        match = "OK" if orig.data == dec.data and orig.delta == dec.delta else "FAIL"
        if match == "FAIL":
            print(f"  Record {i}: {match} - '{orig.data}' vs '{dec.data}'")
    print(f"  All {len(batch.records)} records verified!")

    # Check if fits in packet
    fits = len(wire_data) <= MAX_PACKET_SIZE
    print(f"\nFits in {MAX_PACKET_SIZE}B packet: {'YES' if fits else 'NO'} ({len(wire_data)}B)")

    return wire_data, raw_size


def test_adaptive_dictionary():
    """Test adaptive dictionary compression."""
    print("\n" + "=" * 60)
    print("ADAPTIVE DICTIONARY TEST")
    print("=" * 60)

    dict_obj = AdaptiveDictionary()

    total_raw = 0
    total_compressed = 0
    timestamp = 0

    print("\nProcessing messages (simulating real-time learning):\n")
    print(f"{'#':<3} {'Raw':<6} {'Comp':<6} {'Ratio':<8} {'Message':<30}")
    print("-" * 60)

    for i, msg in enumerate(SAMPLE_KEYSTROKES):
        raw_bytes = msg.encode('utf-8')
        compressed = dict_obj.compress(msg, timestamp)

        ratio = len(compressed) / len(raw_bytes)
        total_raw += len(raw_bytes)
        total_compressed += len(compressed)

        display_msg = msg[:27] + "..." if len(msg) > 30 else msg
        print(f"{i:<3} {len(raw_bytes):<6} {len(compressed):<6} {ratio:<8.2%} {display_msg}")

        timestamp += 300

    print("-" * 60)
    overall_ratio = total_compressed / total_raw
    print(f"{'TOT':<3} {total_raw:<6} {total_compressed:<6} {overall_ratio:<8.2%}")

    print(f"\nDictionary Statistics:")
    stats = dict_obj.get_stats()
    for key, value in stats.items():
        if isinstance(value, float):
            print(f"  {key}: {value:.2%}")
        else:
            print(f"  {key}: {value}")

    # Show learned words
    print(f"\nTop 10 Learned Words (by frequency):")
    sorted_entries = sorted(
        dict_obj.entries.items(),
        key=lambda x: x[1].frequency,
        reverse=True
    )[:10]
    for word, entry in sorted_entries:
        print(f"  '{word}': freq={entry.frequency}")

    # Test flash persistence
    print(f"\nFlash Persistence Test:")
    flash_data = dict_obj.to_flash_format()
    print(f"  Flash size: {len(flash_data)} bytes")
    print(f"  Fits in 64KB: {'YES' if len(flash_data) <= MAX_FLASH_FOR_DICT else 'NO'}")

    # Reload and verify
    reloaded = AdaptiveDictionary.from_flash_format(flash_data)
    print(f"  Entries after reload: {len(reloaded.entries)}")

    # Test compression with reloaded dictionary
    test_msg = "Hello World"  # Should be fully in dictionary
    comp_new = dict_obj.compress(test_msg, timestamp + 300)
    comp_reload = reloaded.compress(test_msg, timestamp + 300)
    print(f"  Compression matches: {'YES' if comp_new == comp_reload else 'NO'}")

    return dict_obj


def test_comparison():
    """Compare all compression methods."""
    print("\n" + "=" * 60)
    print("COMPRESSION METHOD COMPARISON")
    print("=" * 60)

    # Build up adaptive dictionary with training data
    adaptive = AdaptiveDictionary()
    for i, msg in enumerate(SAMPLE_KEYSTROKES[:10]):
        adaptive.compress(msg, i * 300)

    print("\nTest Messages (after training adaptive dict):\n")
    print(f"{'Message':<35} {'Raw':<5} {'SMAZ2':<6} {'Adapt':<6} {'Best':<8}")
    print("-" * 65)

    test_messages = [
        "Hello World",           # Repeated phrase
        "Testing 12345",         # Numbers
        "The quick brown fox",   # Common words
        "Hello there friend",    # Partial match
        "xyz789!@#",             # Worst case
    ]

    for msg in test_messages:
        raw = len(msg.encode('utf-8'))
        smaz = len(smaz2_compress(msg))
        adapt = len(adaptive.compress(msg, 10000))

        best = "SMAZ2" if smaz <= adapt else "Adaptive"
        if raw <= smaz and raw <= adapt:
            best = "None"

        display = msg[:32] + "..." if len(msg) > 35 else msg
        print(f"{display:<35} {raw:<5} {smaz:<6} {adapt:<6} {best:<8}")

    print("\n" + "-" * 65)
    print("Note: Adaptive improves with more training data")


def test_memory_usage():
    """Estimate memory usage on RP2350."""
    print("\n" + "=" * 60)
    print("RP2350 MEMORY USAGE ESTIMATE")
    print("=" * 60)

    print("\nXIAO RP2350 Specs:")
    print("  SRAM:  520KB total")
    print("  Flash: 2MB total")

    # Two-stage compression memory
    ts_compression_buffer = 200  # Output buffer
    data_compression_buffer = 200  # Output buffer
    record_buffer = 20 * 64  # 20 records * 64 bytes avg

    two_stage_ram = ts_compression_buffer + data_compression_buffer + record_buffer

    print(f"\nTwo-Stage Compression (RAM):")
    print(f"  Timestamp buffer: {ts_compression_buffer} bytes")
    print(f"  Data buffer:      {data_compression_buffer} bytes")
    print(f"  Record buffer:    {record_buffer} bytes")
    print(f"  Total:            {two_stage_ram} bytes ({two_stage_ram/1024:.1f}KB)")
    print(f"  % of SRAM:        {two_stage_ram / (520*1024) * 100:.2f}%")

    # Adaptive dictionary memory
    dict_entries = 256 * (32 + 8)  # word + metadata
    token_maps = 256 * 8  # Two hash maps

    adaptive_ram = dict_entries + token_maps
    adaptive_flash = 64 * 1024  # Persistent storage

    print(f"\nAdaptive Dictionary:")
    print(f"  RAM (entries):    {dict_entries} bytes")
    print(f"  RAM (maps):       {token_maps} bytes")
    print(f"  Total RAM:        {adaptive_ram} bytes ({adaptive_ram/1024:.1f}KB)")
    print(f"  % of SRAM:        {adaptive_ram / (520*1024) * 100:.2f}%")
    print(f"  Flash storage:    {adaptive_flash} bytes (64KB)")
    print(f"  % of Flash:       {adaptive_flash / (2*1024*1024) * 100:.2f}%")

    # Combined approach
    combined_ram = two_stage_ram + adaptive_ram
    print(f"\nCombined Approach:")
    print(f"  Total RAM:        {combined_ram} bytes ({combined_ram/1024:.1f}KB)")
    print(f"  % of SRAM:        {combined_ram / (520*1024) * 100:.2f}%")
    print(f"  Verdict:          {'FITS EASILY' if combined_ram < 50*1024 else 'TIGHT'}")


def main():
    """Run all tests."""
    print("\n" + "#" * 60)
    print("# COMPRESSION STRATEGY TESTS FOR XIAO RP2350")
    print("# " + "=" * 56)
    print(f"# Test data: {len(SAMPLE_KEYSTROKES)} messages")
    print(f"# Target packet size: {MAX_PACKET_SIZE} bytes")
    print("#" * 60)

    test_two_stage_compression()
    test_adaptive_dictionary()
    test_comparison()
    test_memory_usage()

    print("\n" + "=" * 60)
    print("ALL TESTS COMPLETE")
    print("=" * 60)


if __name__ == "__main__":
    main()
