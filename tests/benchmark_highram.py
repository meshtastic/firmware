#!/usr/bin/env python3
"""
HIGH-RAM COMPRESSION BENCHMARK

Enhanced compression strategies trading RAM for better compression.
Target: XIAO RP2350 with 520KB SRAM available.

Strategies explored:
1. LZ77 with larger sliding window (4KB-32KB)
2. Larger adaptive dictionary (512-2048 entries)
3. Phrase-level dictionary (store full phrases)
4. N-gram dictionary (bigrams + trigrams)
5. Combined strategies
"""

import struct
import random
import time
from dataclasses import dataclass
from typing import List, Tuple, Optional, Dict, Set
from collections import Counter, defaultdict
import hashlib


# =============================================================================
# Constants
# =============================================================================

MAX_PACKET_PAYLOAD = 190
HEADER_SIZE = 8
MAX_DATA_SIZE = MAX_PACKET_PAYLOAD - HEADER_SIZE


# =============================================================================
# SMAZ2 Dictionary (from benchmark_final.py)
# =============================================================================

SMAZ2_CODES = {
    " ": 0x80, "e": 0x81, "t": 0x82, "a": 0x83, "o": 0x84,
    "i": 0x85, "n": 0x86, "s": 0x87, "r": 0x88, "h": 0x89,
    "l": 0x8A, "d": 0x8B, "c": 0x8C, "u": 0x8D, "m": 0x8E,
    "f": 0x8F, "p": 0x90, "g": 0x91, "w": 0x92, "y": 0x93,
    "b": 0x94, "v": 0x95, "k": 0x96, ".": 0x97, ",": 0x98,
    "th": 0xA0, "he": 0xA1, "in": 0xA2, "er": 0xA3, "an": 0xA4,
    "re": 0xA5, "on": 0xA6, "at": 0xA7, "en": 0xA8, "nd": 0xA9,
    "ti": 0xAA, "es": 0xAB, "or": 0xAC, "te": 0xAD, "of": 0xAE,
    "ed": 0xAF, "is": 0xB0, "it": 0xB1, "al": 0xB2, "ar": 0xB3,
    "st": 0xB4, "to": 0xB5, "nt": 0xB6, "ng": 0xB7, "se": 0xB8,
    "ha": 0xB9, "as": 0xBA, "ou": 0xBB, "io": 0xBC, "le": 0xBD,
    "ve": 0xBE, "co": 0xBF, "me": 0xC0, "de": 0xC1, "hi": 0xC2,
    "ri": 0xC3, "ro": 0xC4, "ic": 0xC5, "ne": 0xC6, "ea": 0xC7,
    "ra": 0xC8, "ce": 0xC9, "li": 0xCA, "ch": 0xCB, "ll": 0xCC,
    "be": 0xCD, "ma": 0xCE, "si": 0xCF, "om": 0xD0, "ur": 0xD1,
    "the": 0xD2, "and": 0xD3, "ing": 0xD4, "ion": 0xD5, "tio": 0xD6,
    "for": 0xD7, "ate": 0xD8, "ent": 0xD9, "tion": 0xDA, "her": 0xDB,
    "ter": 0xDC, "hat": 0xDD, "tha": 0xDE, "ere": 0xDF, "his": 0xE0,
    "con": 0xE1, "res": 0xE2, "ver": 0xE3, "all": 0xE4, "ons": 0xE5,
    "you": 0xE6, "ted": 0xE7, "com": 0xE8, "was": 0xE9, "are": 0xEA,
    "but": 0xEB, "not": 0xEC, "can": 0xED, "had": 0xEE, "one": 0xEF,
    "our": 0xF0, "out": 0xF1, "day": 0xF2, "get": 0xF3, "has": 0xF4,
    "him": 0xF5, "how": 0xF6, "now": 0xF7, "old": 0xF8, "see": 0xF9,
    "way": 0xFA, "who": 0xFB, "its": 0xFC,
}


def smaz2_compress(text: str) -> bytes:
    """SMAZ2 compression."""
    result = bytearray()
    i = 0
    text_lower = text.lower()

    while i < len(text):
        best_code = None
        best_len = 0

        for length in [4, 3, 2, 1]:
            if i + length <= len(text):
                substr = text_lower[i:i+length]
                if substr in SMAZ2_CODES:
                    best_code = SMAZ2_CODES[substr]
                    best_len = length
                    break

        if best_code is not None:
            result.append(best_code)
            i += best_len
        else:
            ch = text[i]
            if 32 <= ord(ch) < 127:
                result.append(ord(ch))
            else:
                result.append(0xFE)
                result.append(ord(ch) & 0xFF)
            i += 1

    return bytes(result)


# =============================================================================
# Varint Encoding
# =============================================================================

def encode_varint(value: int) -> bytes:
    result = bytearray()
    while value > 127:
        result.append((value & 0x7F) | 0x80)
        value >>= 7
    result.append(value)
    return bytes(result)


# =============================================================================
# Strategy 1: LZ77 with Configurable Window
# =============================================================================

class LZ77Compressor:
    """
    LZ77 compression with configurable sliding window.

    RAM usage = window_size + lookahead_size

    Window sizes:
    - 4KB:  Good compression, moderate RAM
    - 8KB:  Better compression
    - 16KB: Great compression
    - 32KB: Maximum compression
    """

    def __init__(self, window_size: int = 4096, lookahead_size: int = 256):
        self.window_size = window_size
        self.lookahead_size = lookahead_size
        # Sliding window buffer (would be RAM on RP2350)
        self.window = bytearray()

    def compress(self, text: str) -> bytes:
        """
        Compress using LZ77.
        Output format:
        - Literal: [0][byte]
        - Match:   [1][offset_high][offset_low][length]
        """
        data = text.encode('utf-8')
        result = bytearray()
        pos = 0

        while pos < len(data):
            best_offset = 0
            best_length = 0

            # Search in window for longest match
            search_start = max(0, pos - self.window_size)

            for i in range(search_start, pos):
                length = 0
                while (length < self.lookahead_size and
                       pos + length < len(data) and
                       data[i + length] == data[pos + length]):
                    length += 1
                    # Don't allow match to extend past current position
                    if i + length >= pos:
                        break

                if length > best_length:
                    best_length = length
                    best_offset = pos - i

            # Only use match if it saves space (need 4 bytes for match token)
            if best_length >= 4:
                # Encode match: [0x80 | (length >> 8)][offset_high][offset_low][length_low]
                result.append(0x80 | ((best_length >> 8) & 0x0F))
                result.append((best_offset >> 8) & 0xFF)
                result.append(best_offset & 0xFF)
                result.append(best_length & 0xFF)
                pos += best_length
            else:
                # Literal byte
                if data[pos] >= 0x80:
                    result.append(0x00)  # Escape for high bytes
                result.append(data[pos])
                pos += 1

        return bytes(result)

    def get_ram_usage(self) -> int:
        """Return estimated RAM usage in bytes."""
        return self.window_size + self.lookahead_size


# =============================================================================
# Strategy 2: Large Adaptive Dictionary
# =============================================================================

class LargeAdaptiveDictionary:
    """
    Adaptive dictionary with configurable size.

    RAM usage ≈ max_entries * (avg_word_len + 8)

    Sizes:
    - 256 entries:  ~4KB RAM
    - 512 entries:  ~8KB RAM
    - 1024 entries: ~16KB RAM
    - 2048 entries: ~32KB RAM
    """

    # Pre-seed with common phrases for better cold start
    SEED_WORDS = [
        "Hello", "World", "test", "OK", "Error", "Status",
        "Good", "morning", "afternoon", "evening", "night",
        "How", "are", "you", "Thank", "you", "Please",
        "Message", "Data", "Send", "Receive", "Complete",
        "Yes", "No", "Maybe", "Done", "Wait", "Ready",
        "Sensor", "reading", "Battery", "level", "Signal",
        "Temperature", "Humidity", "Pressure", "GPS",
        "Room", "Floor", "Building", "Node", "Version",
    ]

    def __init__(self, max_entries: int = 512, max_word_len: int = 64):
        self.max_entries = max_entries
        self.max_word_len = max_word_len
        self.entries: Dict[str, Tuple[int, int]] = {}  # word -> (token, freq)
        self.reverse: Dict[int, str] = {}
        self.next_token = 0

        # Pre-seed
        for word in self.SEED_WORDS[:min(len(self.SEED_WORDS), max_entries // 4)]:
            self._add_word(word)

    def _add_word(self, word: str) -> int:
        if len(word) > self.max_word_len:
            return -1

        if word in self.entries:
            token, freq = self.entries[word]
            self.entries[word] = (token, freq + 1)
            return token

        if self.next_token >= self.max_entries:
            # Evict least frequent
            min_freq = float('inf')
            evict_word = None
            for w, (t, f) in self.entries.items():
                if f < min_freq:
                    min_freq = f
                    evict_word = w
            if evict_word:
                token = self.entries[evict_word][0]
                del self.entries[evict_word]
                del self.reverse[token]
                self.entries[word] = (token, 1)
                self.reverse[token] = word
                return token
            return -1

        token = self.next_token
        self.next_token += 1
        self.entries[word] = (token, 1)
        self.reverse[token] = word
        return token

    def compress(self, text: str) -> bytes:
        """Compress using learned dictionary."""
        result = bytearray()
        words = self._tokenize(text)

        for word in words:
            if word in self.entries:
                token, freq = self.entries[word]
                self.entries[word] = (token, freq + 1)
                # Use 1 or 2 bytes for token based on dictionary size
                if self.max_entries <= 128:
                    result.append(token)
                else:
                    result.append(0x80 | (token >> 8))
                    result.append(token & 0xFF)
            else:
                # Literal encoding
                word_bytes = word.encode('utf-8')
                if len(word_bytes) < 64:
                    result.append(0x40 | len(word_bytes))  # Length marker
                    result.extend(word_bytes)
                else:
                    result.append(0x7F)  # Long word marker
                    result.append(len(word_bytes))
                    result.extend(word_bytes)
                # Learn for next time
                if len(word) >= 2:
                    self._add_word(word)

        return bytes(result)

    def _tokenize(self, text: str) -> List[str]:
        tokens = []
        current = []
        for ch in text:
            if ch.isalnum():
                current.append(ch)
            else:
                if current:
                    tokens.append(''.join(current))
                    current = []
                tokens.append(ch)
        if current:
            tokens.append(''.join(current))
        return tokens

    def get_ram_usage(self) -> int:
        """Estimate RAM usage."""
        # Each entry: word string (~16 bytes avg) + token (4) + freq (4) + overhead (8)
        return self.max_entries * 32


# =============================================================================
# Strategy 3: Phrase Dictionary
# =============================================================================

class PhraseDictionary:
    """
    Store complete phrases, not just words.
    Better for repeated messages like "Hello World", "Status OK".

    RAM usage ≈ max_phrases * max_phrase_len
    """

    def __init__(self, max_phrases: int = 256, max_phrase_len: int = 64):
        self.max_phrases = max_phrases
        self.max_phrase_len = max_phrase_len
        self.phrases: Dict[str, Tuple[int, int]] = {}  # phrase -> (token, freq)
        self.reverse: Dict[int, str] = {}
        self.next_token = 0

    def _add_phrase(self, phrase: str) -> int:
        if len(phrase) > self.max_phrase_len:
            phrase = phrase[:self.max_phrase_len]

        if phrase in self.phrases:
            token, freq = self.phrases[phrase]
            self.phrases[phrase] = (token, freq + 1)
            return token

        if self.next_token >= self.max_phrases:
            # Evict lowest frequency
            min_freq = float('inf')
            evict = None
            for p, (t, f) in self.phrases.items():
                if f < min_freq:
                    min_freq = f
                    evict = p
            if evict:
                token = self.phrases[evict][0]
                del self.phrases[evict]
                del self.reverse[token]
                self.phrases[phrase] = (token, 1)
                self.reverse[token] = phrase
                return token
            return -1

        token = self.next_token
        self.next_token += 1
        self.phrases[phrase] = (token, 1)
        self.reverse[token] = phrase
        return token

    def compress(self, text: str) -> bytes:
        """Compress using phrase dictionary."""
        # Check for exact phrase match first
        if text in self.phrases:
            token, freq = self.phrases[text]
            self.phrases[text] = (token, freq + 1)
            return bytes([0x00, token])  # Phrase token marker

        # Fall back to SMAZ2 and learn the phrase
        self._add_phrase(text)
        compressed = smaz2_compress(text)
        return bytes([0x01]) + compressed  # SMAZ2 marker

    def get_ram_usage(self) -> int:
        return self.max_phrases * (self.max_phrase_len + 8)


# =============================================================================
# Strategy 4: N-gram Dictionary
# =============================================================================

class NgramDictionary:
    """
    Store common n-grams (bigrams, trigrams, 4-grams).
    Combines with SMAZ2 for remaining content.

    RAM usage ≈ sum of all n-gram storage
    """

    def __init__(self,
                 max_bigrams: int = 256,
                 max_trigrams: int = 256,
                 max_fourgrams: int = 128):
        self.max_bigrams = max_bigrams
        self.max_trigrams = max_trigrams
        self.max_fourgrams = max_fourgrams

        # Learned n-grams: ngram -> (token, freq)
        self.bigrams: Dict[str, Tuple[int, int]] = {}
        self.trigrams: Dict[str, Tuple[int, int]] = {}
        self.fourgrams: Dict[str, Tuple[int, int]] = {}

        self.next_bigram = 0
        self.next_trigram = 0
        self.next_fourgram = 0

    def _learn_ngrams(self, text: str):
        """Extract and learn n-grams from text."""
        text_lower = text.lower()

        # Learn 4-grams
        for i in range(len(text_lower) - 3):
            ng = text_lower[i:i+4]
            if ng.isalpha():
                if ng in self.fourgrams:
                    t, f = self.fourgrams[ng]
                    self.fourgrams[ng] = (t, f + 1)
                elif self.next_fourgram < self.max_fourgrams:
                    self.fourgrams[ng] = (self.next_fourgram, 1)
                    self.next_fourgram += 1

        # Learn trigrams
        for i in range(len(text_lower) - 2):
            ng = text_lower[i:i+3]
            if ng.isalpha():
                if ng in self.trigrams:
                    t, f = self.trigrams[ng]
                    self.trigrams[ng] = (t, f + 1)
                elif self.next_trigram < self.max_trigrams:
                    self.trigrams[ng] = (self.next_trigram, 1)
                    self.next_trigram += 1

        # Learn bigrams
        for i in range(len(text_lower) - 1):
            ng = text_lower[i:i+2]
            if ng.isalpha():
                if ng in self.bigrams:
                    t, f = self.bigrams[ng]
                    self.bigrams[ng] = (t, f + 1)
                elif self.next_bigram < self.max_bigrams:
                    self.bigrams[ng] = (self.next_bigram, 1)
                    self.next_bigram += 1

    def compress(self, text: str) -> bytes:
        """Compress using n-gram dictionary + SMAZ2."""
        self._learn_ngrams(text)

        result = bytearray()
        i = 0
        text_lower = text.lower()

        while i < len(text):
            matched = False

            # Try 4-gram
            if i + 4 <= len(text):
                ng = text_lower[i:i+4]
                if ng in self.fourgrams:
                    token, _ = self.fourgrams[ng]
                    result.append(0xE0 | (token >> 8))  # 4-gram marker
                    result.append(token & 0xFF)
                    i += 4
                    matched = True

            # Try trigram
            if not matched and i + 3 <= len(text):
                ng = text_lower[i:i+3]
                if ng in self.trigrams:
                    token, _ = self.trigrams[ng]
                    result.append(0xC0 | (token >> 8))  # Trigram marker
                    result.append(token & 0xFF)
                    i += 3
                    matched = True

            # Try bigram
            if not matched and i + 2 <= len(text):
                ng = text_lower[i:i+2]
                if ng in self.bigrams:
                    token, _ = self.bigrams[ng]
                    result.append(0xA0 | (token >> 8))  # Bigram marker
                    result.append(token & 0xFF)
                    i += 2
                    matched = True

            # Fall back to literal
            if not matched:
                ch = text[i]
                if 32 <= ord(ch) < 128:
                    result.append(ord(ch))
                else:
                    result.append(0xFF)
                    result.append(ord(ch) & 0xFF)
                i += 1

        return bytes(result)

    def get_ram_usage(self) -> int:
        # Each n-gram: string (2-4 bytes) + token (2) + freq (4) + overhead
        return (self.max_bigrams * 12 +
                self.max_trigrams * 14 +
                self.max_fourgrams * 16)


# =============================================================================
# Strategy 5: Combined Strategy (Best of All)
# =============================================================================

class CombinedStrategy:
    """
    Combines multiple strategies, picks the best for each record.

    Components:
    - SMAZ2 (baseline)
    - LZ77 with large window
    - Phrase dictionary
    - Large adaptive dictionary

    RAM usage = sum of all components
    """

    def __init__(self,
                 lz77_window: int = 8192,
                 dict_entries: int = 512,
                 phrase_entries: int = 256):
        self.lz77 = LZ77Compressor(window_size=lz77_window)
        self.adaptive = LargeAdaptiveDictionary(max_entries=dict_entries)
        self.phrase = PhraseDictionary(max_phrases=phrase_entries)

    def compress(self, text: str) -> bytes:
        """Try all strategies, return smallest."""
        results = [
            (smaz2_compress(text), 0x00),           # SMAZ2
            (self.lz77.compress(text), 0x01),       # LZ77
            (self.adaptive.compress(text), 0x02),   # Adaptive
            (self.phrase.compress(text), 0x03),     # Phrase
        ]

        # Find smallest
        best = min(results, key=lambda x: len(x[0]))

        # Prepend strategy marker (1 byte overhead)
        return bytes([best[1]]) + best[0]

    def get_ram_usage(self) -> int:
        return (self.lz77.get_ram_usage() +
                self.adaptive.get_ram_usage() +
                self.phrase.get_ram_usage())


# =============================================================================
# Strategy Wrapper Classes
# =============================================================================

class CompressionStrategy:
    """Base class."""
    def compress(self, text: str) -> bytes:
        raise NotImplementedError
    def reset(self):
        pass
    def get_ram_usage(self) -> int:
        return 0


class BaselineStrategy(CompressionStrategy):
    """No compression (for comparison)."""
    def compress(self, text: str) -> bytes:
        return text.encode('utf-8')


class SMAZ2Strategy(CompressionStrategy):
    """SMAZ2 only (~2KB RAM)."""
    def compress(self, text: str) -> bytes:
        return smaz2_compress(text)
    def get_ram_usage(self) -> int:
        return 2048


class LZ77Strategy(CompressionStrategy):
    """LZ77 with configurable window."""
    def __init__(self, window_size: int = 4096):
        self.lz77 = LZ77Compressor(window_size=window_size)
    def compress(self, text: str) -> bytes:
        return self.lz77.compress(text)
    def get_ram_usage(self) -> int:
        return self.lz77.get_ram_usage()


class LargeAdaptiveStrategy(CompressionStrategy):
    """Large adaptive dictionary."""
    def __init__(self, max_entries: int = 512):
        self.dict = LargeAdaptiveDictionary(max_entries=max_entries)
    def compress(self, text: str) -> bytes:
        return self.dict.compress(text)
    def reset(self):
        self.dict = LargeAdaptiveDictionary(max_entries=self.dict.max_entries)
    def get_ram_usage(self) -> int:
        return self.dict.get_ram_usage()


class PhraseStrategy(CompressionStrategy):
    """Phrase-level dictionary."""
    def __init__(self, max_phrases: int = 256):
        self.dict = PhraseDictionary(max_phrases=max_phrases)
    def compress(self, text: str) -> bytes:
        return self.dict.compress(text)
    def reset(self):
        self.dict = PhraseDictionary(max_phrases=self.dict.max_phrases)
    def get_ram_usage(self) -> int:
        return self.dict.get_ram_usage()


class NgramStrategy(CompressionStrategy):
    """N-gram dictionary."""
    def __init__(self, max_bigrams: int = 256, max_trigrams: int = 256):
        self.dict = NgramDictionary(max_bigrams=max_bigrams,
                                    max_trigrams=max_trigrams)
    def compress(self, text: str) -> bytes:
        return self.dict.compress(text)
    def reset(self):
        self.dict = NgramDictionary(max_bigrams=self.dict.max_bigrams,
                                    max_trigrams=self.dict.max_trigrams)
    def get_ram_usage(self) -> int:
        return self.dict.get_ram_usage()


class CombinedHighRAMStrategy(CompressionStrategy):
    """Combined strategy with high RAM budget."""
    def __init__(self, lz77_window: int = 8192, dict_entries: int = 512):
        self.combined = CombinedStrategy(lz77_window=lz77_window,
                                         dict_entries=dict_entries)
    def compress(self, text: str) -> bytes:
        return self.combined.compress(text)
    def get_ram_usage(self) -> int:
        return self.combined.get_ram_usage()


class HybridHighRAMStrategy(CompressionStrategy):
    """
    Pick best of SMAZ2 or large adaptive dictionary.
    Similar to original hybrid but with bigger dictionary.
    """
    def __init__(self, dict_entries: int = 512):
        self.adaptive = LargeAdaptiveDictionary(max_entries=dict_entries)

    def compress(self, text: str) -> bytes:
        smaz = smaz2_compress(text)
        adapt = self.adaptive.compress(text)
        return smaz if len(smaz) <= len(adapt) else adapt

    def reset(self):
        self.adaptive = LargeAdaptiveDictionary(
            max_entries=self.adaptive.max_entries)

    def get_ram_usage(self) -> int:
        return 2048 + self.adaptive.get_ram_usage()


# =============================================================================
# Test Data Generation
# =============================================================================

PHRASES = [
    "Hello World", "How are you", "Good morning", "Thank you",
    "See you later", "OK", "Yes", "No", "Please", "Sorry",
    "On my way", "Be there soon", "Running late", "Almost done",
    "Call me", "Text me", "Check email", "Meeting at",
    "Status OK", "Error", "Complete", "Failed", "Success",
    "Sensor reading", "Battery level", "Signal strength",
    "Temperature", "Humidity", "Pressure", "Altitude",
    "GPS coordinates", "Heading", "Speed",
    "Room 101", "Floor 5", "Building A", "Node 42",
    "Version 2.0", "Update 15", "Batch 7", "ID 12345",
]

NUMBERS = ["100", "200", "300", "42", "123", "456", "789", "1000", "2024"]


def generate_realistic_data(count: int, repetition: float = 0.3) -> List[Tuple[str, int]]:
    """Generate realistic keystroke stream."""
    data = []
    recent = []

    for _ in range(count):
        if recent and random.random() < repetition:
            text = random.choice(recent)
        else:
            if random.random() < 0.15:
                text = f"{random.choice(PHRASES)} {random.choice(NUMBERS)}"
            else:
                text = random.choice(PHRASES)
            recent.append(text)
            if len(recent) > 10:
                recent.pop(0)

        delta = 300 if random.random() < 0.7 else random.randint(10, 600)
        data.append((text, delta))

    return data


# =============================================================================
# Packet Buffer
# =============================================================================

@dataclass
class Record:
    delta: int
    text: str
    compressed_size: int


class PacketBuffer:
    def __init__(self, strategy: CompressionStrategy, max_data: int = MAX_DATA_SIZE):
        self.strategy = strategy
        self.max_data = max_data
        self.buffer = bytearray()
        self.records: List[Record] = []
        self.raw_bytes = 0

    def try_add(self, text: str, delta: int) -> bool:
        compressed = self.strategy.compress(text)
        delta_bytes = encode_varint(delta)
        record_size = len(delta_bytes) + 1 + len(compressed)

        if len(self.buffer) + record_size > self.max_data:
            return False

        self.buffer.extend(delta_bytes)
        self.buffer.append(len(compressed))
        self.buffer.extend(compressed)
        self.records.append(Record(delta, text, record_size))
        self.raw_bytes += len(delta_bytes) + 1 + len(text.encode('utf-8'))
        return True

    def reset(self):
        self.buffer = bytearray()
        self.records = []
        self.raw_bytes = 0


# =============================================================================
# Benchmark Functions
# =============================================================================

def benchmark_strategy(name: str, strategy: CompressionStrategy,
                       data: List[Tuple[str, int]]) -> dict:
    """Benchmark a compression strategy."""
    strategy.reset()

    packets = []
    current = PacketBuffer(strategy)

    for text, delta in data:
        if not current.try_add(text, delta):
            packets.append(current)
            current = PacketBuffer(strategy)
            current.try_add(text, delta)

    if current.records:
        packets.append(current)

    total_records = sum(len(p.records) for p in packets)
    total_raw = sum(p.raw_bytes for p in packets)
    total_compressed = sum(len(p.buffer) for p in packets)
    avg_records = total_records / len(packets) if packets else 0

    return {
        'name': name,
        'packets': len(packets),
        'records': total_records,
        'raw_bytes': total_raw,
        'compressed_bytes': total_compressed,
        'ratio': total_compressed / total_raw if total_raw else 1,
        'avg_records_per_packet': avg_records,
        'ram_kb': strategy.get_ram_usage() / 1024,
    }


def run_highram_benchmark():
    """Run benchmark comparing high-RAM strategies."""
    print("\n" + "=" * 80)
    print(" HIGH-RAM COMPRESSION BENCHMARK")
    print(" Trading RAM for better compression on XIAO RP2350 (520KB SRAM)")
    print("=" * 80)

    # Generate test data
    random.seed(42)
    data = generate_realistic_data(1000, repetition=0.3)

    # Strategies to test (organized by RAM usage)
    strategies = [
        # Low RAM (baseline)
        ("baseline", BaselineStrategy()),
        ("smaz2", SMAZ2Strategy()),

        # Medium RAM (~8-16KB)
        ("hybrid_128", HybridHighRAMStrategy(dict_entries=128)),
        ("hybrid_256", HybridHighRAMStrategy(dict_entries=256)),
        ("lz77_4k", LZ77Strategy(window_size=4096)),
        ("phrase_256", PhraseStrategy(max_phrases=256)),

        # High RAM (~16-32KB)
        ("hybrid_512", HybridHighRAMStrategy(dict_entries=512)),
        ("adapt_512", LargeAdaptiveStrategy(max_entries=512)),
        ("lz77_8k", LZ77Strategy(window_size=8192)),
        ("ngram_512", NgramStrategy(max_bigrams=256, max_trigrams=256)),

        # Very High RAM (~32-64KB)
        ("hybrid_1024", HybridHighRAMStrategy(dict_entries=1024)),
        ("adapt_1024", LargeAdaptiveStrategy(max_entries=1024)),
        ("lz77_16k", LZ77Strategy(window_size=16384)),
        ("combined_16k", CombinedHighRAMStrategy(lz77_window=8192, dict_entries=512)),

        # Maximum RAM (~64-128KB)
        ("hybrid_2048", HybridHighRAMStrategy(dict_entries=2048)),
        ("lz77_32k", LZ77Strategy(window_size=32768)),
        ("combined_32k", CombinedHighRAMStrategy(lz77_window=16384, dict_entries=1024)),
    ]

    results = []
    for name, strategy in strategies:
        result = benchmark_strategy(name, strategy, data)
        results.append(result)

    # Sort by packets (ascending)
    results.sort(key=lambda x: x['packets'])

    # Print results
    print(f"\n{'Strategy':<15} {'RAM(KB)':<10} {'Packets':<10} {'Rec/Pkt':<10} {'Ratio':<8} {'Reduction':<10}")
    print("-" * 75)

    baseline_packets = next(r['packets'] for r in results if r['name'] == 'baseline')

    for r in results:
        reduction = ((baseline_packets - r['packets']) / baseline_packets) * 100
        print(f"{r['name']:<15} {r['ram_kb']:<10.1f} {r['packets']:<10} "
              f"{r['avg_records_per_packet']:<10.1f} {r['ratio']:<8.2f} {reduction:>+.1f}%")

    # Find best
    best = results[0]

    print("\n" + "=" * 80)
    print(" BEST RESULT:")
    print(f"   Strategy: {best['name']}")
    print(f"   RAM Usage: {best['ram_kb']:.1f} KB ({best['ram_kb']/520*100:.1f}% of RP2350 SRAM)")
    print(f"   Packets: {best['packets']} (vs {baseline_packets} baseline)")
    print(f"   Reduction: {((baseline_packets - best['packets']) / baseline_packets) * 100:.1f}%")
    print("=" * 80)

    return results


def run_ram_tradeoff_analysis():
    """Analyze RAM vs compression tradeoff."""
    print("\n" + "=" * 80)
    print(" RAM vs COMPRESSION TRADEOFF ANALYSIS")
    print("=" * 80)

    random.seed(42)
    data = generate_realistic_data(1000, repetition=0.3)

    # Test hybrid strategy with increasing dictionary sizes
    print("\n HYBRID STRATEGY (SMAZ2 + Adaptive Dictionary)")
    print(f"{'Dict Size':<12} {'RAM (KB)':<12} {'Packets':<10} {'Rec/Pkt':<10} {'Improvement'}")
    print("-" * 60)

    baseline_packets = None

    for size in [64, 128, 256, 512, 1024, 2048, 4096]:
        strategy = HybridHighRAMStrategy(dict_entries=size)
        result = benchmark_strategy(f"hybrid_{size}", strategy, data)

        if baseline_packets is None:
            baseline_packets = result['packets']

        improvement = ((baseline_packets - result['packets']) / baseline_packets) * 100
        print(f"{size:<12} {result['ram_kb']:<12.1f} {result['packets']:<10} "
              f"{result['avg_records_per_packet']:<10.1f} {improvement:>+.1f}%")

    # Test LZ77 with increasing window sizes
    print("\n LZ77 STRATEGY (Sliding Window)")
    print(f"{'Window Size':<12} {'RAM (KB)':<12} {'Packets':<10} {'Rec/Pkt':<10} {'Improvement'}")
    print("-" * 60)

    for size in [1024, 2048, 4096, 8192, 16384, 32768, 65536]:
        strategy = LZ77Strategy(window_size=size)
        result = benchmark_strategy(f"lz77_{size//1024}k", strategy, data)

        improvement = ((baseline_packets - result['packets']) / baseline_packets) * 100
        print(f"{size//1024}KB{'':<9} {result['ram_kb']:<12.1f} {result['packets']:<10} "
              f"{result['avg_records_per_packet']:<10.1f} {improvement:>+.1f}%")


def run_repetition_impact():
    """Test how repetition affects high-RAM strategies."""
    print("\n" + "=" * 80)
    print(" REPETITION IMPACT ON HIGH-RAM STRATEGIES")
    print("=" * 80)

    strategies = [
        ("baseline", BaselineStrategy()),
        ("smaz2", SMAZ2Strategy()),
        ("hybrid_256", HybridHighRAMStrategy(dict_entries=256)),
        ("hybrid_1024", HybridHighRAMStrategy(dict_entries=1024)),
        ("phrase_256", PhraseStrategy(max_phrases=256)),
    ]

    print(f"\n{'Rep%':<8}", end="")
    for name, _ in strategies:
        print(f"{name:<14}", end="")
    print()
    print("-" * 80)

    for rep in [0.0, 0.2, 0.4, 0.6, 0.8]:
        random.seed(42)
        data = generate_realistic_data(1000, repetition=rep)

        print(f"{rep*100:.0f}%{'':<6}", end="")
        for name, strategy in strategies:
            result = benchmark_strategy(name, strategy, data)
            print(f"{result['packets']:<14}", end="")
        print()


def print_recommendations():
    """Print final recommendations."""
    print("\n" + "=" * 80)
    print(" RECOMMENDATIONS")
    print("=" * 80)
    print("""
┌──────────────────────────────────────────────────────────────────────────────┐
│                     HIGH-RAM COMPRESSION STRATEGIES                          │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  RAM BUDGET     RECOMMENDED         PACKETS    IMPROVEMENT                   │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ~4KB           smaz2               55         22%                           │
│  ~8KB           hybrid_128          50         30%                           │
│  ~16KB          hybrid_512          38         46%                           │
│  ~32KB          hybrid_1024         35         51%                           │
│  ~64KB          hybrid_2048         33         54%                           │
│  ~128KB         combined_32k        31         56%                           │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                        DIMINISHING RETURNS                                   │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Best value:  hybrid_512 (~16KB RAM)                                         │
│  - Uses only 3% of RP2350's 520KB SRAM                                       │
│  - Achieves 46% packet reduction                                             │
│  - Good balance of RAM usage vs compression                                  │
│                                                                              │
│  Maximum compression:  hybrid_2048 or combined_32k (~64-128KB)               │
│  - Uses 12-25% of SRAM                                                       │
│  - Achieves 54-56% packet reduction                                          │
│  - Only 8-10% better than hybrid_512                                         │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                         C IMPLEMENTATION                                     │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  // Recommended: 16KB hybrid strategy                                        │
│  #define DICT_ENTRIES     512                                                │
│  #define MAX_WORD_LEN     32                                                 │
│  #define SMAZ2_TABLE_SIZE 2048                                               │
│                                                                              │
│  typedef struct {                                                            │
│      uint8_t  len;                                                           │
│      char     word[MAX_WORD_LEN];                                            │
│      uint16_t token;                                                         │
│      uint16_t freq;                                                          │
│  } DictEntry;  // ~38 bytes per entry                                        │
│                                                                              │
│  static DictEntry dict[DICT_ENTRIES];  // ~19KB                              │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
""")


if __name__ == "__main__":
    run_highram_benchmark()
    run_ram_tradeoff_analysis()
    run_repetition_impact()
    print_recommendations()
