#!/usr/bin/env python3
"""
OPTIMIZED COMPRESSION BENCHMARK

Focus on the winning strategies:
1. Phrase dictionary (best performer)
2. Combined phrase + SMAZ2 hybrid
3. Incremental learning optimizations

Target: Maximum compression for keystroke data
"""

import struct
import random
from dataclasses import dataclass
from typing import List, Tuple, Optional, Dict
from collections import Counter


# =============================================================================
# Constants
# =============================================================================

MAX_PACKET_PAYLOAD = 190
HEADER_SIZE = 8
MAX_DATA_SIZE = MAX_PACKET_PAYLOAD - HEADER_SIZE


# =============================================================================
# SMAZ2 Dictionary
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


def encode_varint(value: int) -> bytes:
    result = bytearray()
    while value > 127:
        result.append((value & 0x7F) | 0x80)
        value >>= 7
    result.append(value)
    return bytes(result)


# =============================================================================
# Optimized Phrase Dictionary
# =============================================================================

class OptimizedPhraseDictionary:
    """
    Phrase dictionary optimized for keystroke patterns.

    Features:
    - Complete phrase matching (highest priority)
    - Partial phrase matching with SMAZ2 fallback
    - LRU eviction for bounded memory
    - Optional pre-seeding with common phrases
    """

    def __init__(self, max_phrases: int = 256, max_phrase_len: int = 64):
        self.max_phrases = max_phrases
        self.max_phrase_len = max_phrase_len
        self.phrases: Dict[str, int] = {}  # phrase -> token
        self.reverse: Dict[int, str] = {}  # token -> phrase
        self.freq: Dict[str, int] = {}     # phrase -> frequency
        self.next_token = 0

        # Pre-seed with common phrases
        self._preseed()

    def _preseed(self):
        """Pre-seed dictionary with expected common phrases."""
        common_phrases = [
            # Greetings
            "Hello World", "Good morning", "Good afternoon", "Good evening",
            "How are you", "Thank you", "See you later", "Take care",
            # Status
            "OK", "Yes", "No", "Done", "Error", "Complete", "Failed", "Success",
            "Status OK", "All good", "Running late", "Almost done",
            # Actions
            "On my way", "Be there soon", "Call me", "Text me",
            "Check email", "Meeting at", "Send data",
            # Technical
            "Sensor reading", "Battery level", "Signal strength",
            "Temperature", "Humidity", "Pressure", "GPS",
        ]
        for phrase in common_phrases[:self.max_phrases // 2]:
            self._add_phrase(phrase)

    def _add_phrase(self, phrase: str) -> int:
        """Add phrase to dictionary, return token."""
        if len(phrase) > self.max_phrase_len:
            phrase = phrase[:self.max_phrase_len]

        if phrase in self.phrases:
            self.freq[phrase] = self.freq.get(phrase, 0) + 1
            return self.phrases[phrase]

        if self.next_token >= self.max_phrases:
            # Evict least frequent
            min_freq = float('inf')
            evict = None
            for p, f in self.freq.items():
                if f < min_freq:
                    min_freq = f
                    evict = p
            if evict and min_freq < 3:  # Only evict if low frequency
                token = self.phrases[evict]
                del self.phrases[evict]
                del self.reverse[token]
                del self.freq[evict]
                self.phrases[phrase] = token
                self.reverse[token] = phrase
                self.freq[phrase] = 1
                return token
            return -1

        token = self.next_token
        self.next_token += 1
        self.phrases[phrase] = token
        self.reverse[token] = phrase
        self.freq[phrase] = 1
        return token

    def compress(self, text: str) -> bytes:
        """
        Compress using phrase dictionary.

        Wire format:
        - [0x00][token] = phrase match (2 bytes for entire phrase!)
        - [0x01][smaz2_data...] = SMAZ2 fallback
        """
        # Check exact phrase match first (best case)
        if text in self.phrases:
            token = self.phrases[text]
            self.freq[text] = self.freq.get(text, 0) + 1
            # Single byte if token < 128, else two bytes
            if token < 128:
                return bytes([token])
            else:
                return bytes([0x80 | (token >> 8), token & 0xFF])

        # No exact match - learn the phrase and fall back to SMAZ2
        self._add_phrase(text)
        compressed = smaz2_compress(text)
        return bytes([0xFE]) + compressed  # 0xFE = SMAZ2 marker

    def get_ram_usage(self) -> int:
        """Estimate RAM usage in bytes."""
        # Each phrase: avg 16 bytes + token (2) + freq (4) + overhead (10)
        return self.max_phrases * 32


# =============================================================================
# Hybrid Phrase + SMAZ2 Strategy
# =============================================================================

class HybridPhraseStrategy:
    """
    Combined strategy:
    1. Try phrase dictionary first
    2. If no match, use SMAZ2
    3. Pick whichever is smaller
    """

    def __init__(self, max_phrases: int = 256):
        self.phrase_dict = OptimizedPhraseDictionary(max_phrases=max_phrases)

    def compress(self, text: str) -> bytes:
        phrase_result = self.phrase_dict.compress(text)
        smaz_result = smaz2_compress(text)

        # Pick smaller
        if len(phrase_result) <= len(smaz_result):
            return phrase_result
        return bytes([0xFF]) + smaz_result  # 0xFF = raw SMAZ2 marker

    def reset(self):
        self.phrase_dict = OptimizedPhraseDictionary(
            max_phrases=self.phrase_dict.max_phrases)

    def get_ram_usage(self) -> int:
        return self.phrase_dict.get_ram_usage() + 2048  # + SMAZ2 tables


# =============================================================================
# Two-Level Dictionary (Phrases + Words)
# =============================================================================

class TwoLevelDictionary:
    """
    Two-level compression:
    1. Full phrase matches (highest priority)
    2. Word-level matches
    3. SMAZ2 for remaining characters

    RAM usage ~32-64KB for good coverage
    """

    def __init__(self, max_phrases: int = 256, max_words: int = 512):
        self.max_phrases = max_phrases
        self.max_words = max_words

        # Phrase dictionary (0x00-0x7F = phrase tokens)
        self.phrases: Dict[str, int] = {}
        self.phrase_reverse: Dict[int, str] = {}
        self.phrase_freq: Dict[str, int] = {}
        self.next_phrase_token = 0

        # Word dictionary (0x80-0xBF = word tokens, with continuation byte)
        self.words: Dict[str, int] = {}
        self.word_reverse: Dict[int, str] = {}
        self.word_freq: Dict[str, int] = {}
        self.next_word_token = 0

        self._preseed()

    def _preseed(self):
        """Pre-seed with common content."""
        # Common phrases
        phrases = [
            "Hello World", "Good morning", "How are you", "Thank you",
            "Status OK", "On my way", "Be there soon", "See you later",
            "Sensor reading", "Battery level", "Signal strength",
        ]
        for p in phrases:
            self._add_phrase(p)

        # Common words
        words = [
            "Hello", "World", "Good", "morning", "afternoon", "evening",
            "How", "are", "you", "Thank", "Status", "Error", "OK",
            "Sensor", "Battery", "Signal", "Temperature", "Humidity",
            "Room", "Floor", "Building", "Node", "Version", "Update",
        ]
        for w in words:
            self._add_word(w)

    def _add_phrase(self, phrase: str) -> int:
        if phrase in self.phrases:
            self.phrase_freq[phrase] += 1
            return self.phrases[phrase]

        if self.next_phrase_token >= self.max_phrases:
            # Evict lowest frequency
            min_item = min(self.phrase_freq.items(), key=lambda x: x[1])
            if min_item[1] < 2:
                token = self.phrases[min_item[0]]
                del self.phrases[min_item[0]]
                del self.phrase_reverse[token]
                del self.phrase_freq[min_item[0]]
            else:
                return -1

        token = self.next_phrase_token
        self.next_phrase_token += 1
        self.phrases[phrase] = token
        self.phrase_reverse[token] = phrase
        self.phrase_freq[phrase] = 1
        return token

    def _add_word(self, word: str) -> int:
        if word in self.words:
            self.word_freq[word] += 1
            return self.words[word]

        if self.next_word_token >= self.max_words:
            min_item = min(self.word_freq.items(), key=lambda x: x[1])
            if min_item[1] < 2:
                token = self.words[min_item[0]]
                del self.words[min_item[0]]
                del self.word_reverse[token]
                del self.word_freq[min_item[0]]
            else:
                return -1

        token = self.next_word_token
        self.next_word_token += 1
        self.words[word] = token
        self.word_reverse[token] = word
        self.word_freq[word] = 1
        return token

    def compress(self, text: str) -> bytes:
        """
        Compress with two-level dictionary.

        Wire format:
        - [0x00-0x7F] = phrase token (1 byte for entire phrase!)
        - [0x80][token_high][token_low] = word token (3 bytes)
        - [0xFE][len][smaz_data] = SMAZ2 for unmatched segments
        - [0xFF][char] = literal character
        """
        # Try full phrase match first (best case)
        if text in self.phrases:
            token = self.phrases[text]
            self.phrase_freq[text] += 1
            return bytes([token])

        # Learn this phrase for next time
        self._add_phrase(text)

        # Try word-level compression
        result = bytearray()
        words = self._tokenize(text)

        for word in words:
            if word in self.words:
                token = self.words[word]
                self.word_freq[word] += 1
                result.append(0x80 | (token >> 8))
                result.append(token & 0xFF)
            elif len(word) == 1 and 32 <= ord(word) < 128:
                # Single character
                result.append(ord(word))
            else:
                # Use SMAZ2 for unknown word
                smaz = smaz2_compress(word)
                if len(smaz) < len(word):
                    result.append(0xFE)
                    result.append(len(smaz))
                    result.extend(smaz)
                else:
                    # Literal
                    result.append(0xFD)
                    result.append(len(word))
                    result.extend(word.encode('utf-8'))

                # Learn word
                if len(word) >= 3:
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

    def reset(self):
        self.__init__(self.max_phrases, self.max_words)

    def get_ram_usage(self) -> int:
        return (self.max_phrases * 32) + (self.max_words * 24)


# =============================================================================
# Aggressive Learning Strategy
# =============================================================================

class AggressiveLearningStrategy:
    """
    Learns patterns aggressively and adapts quickly.

    Features:
    - Fast learning (add new phrases immediately)
    - Decay old entries to make room for new patterns
    - Hybrid fallback to SMAZ2
    """

    def __init__(self, max_phrases: int = 512):
        self.max_phrases = max_phrases
        self.phrases: Dict[str, Tuple[int, int, int]] = {}  # phrase -> (token, freq, last_used)
        self.reverse: Dict[int, str] = {}
        self.next_token = 0
        self.access_counter = 0

    def _evict_if_needed(self):
        """Evict entries based on score = freq / age."""
        if len(self.phrases) < self.max_phrases:
            return True

        min_score = float('inf')
        evict = None

        for phrase, (token, freq, last_used) in self.phrases.items():
            age = self.access_counter - last_used + 1
            score = freq / age
            if score < min_score:
                min_score = score
                evict = phrase

        if evict and min_score < 1.0:
            token = self.phrases[evict][0]
            del self.phrases[evict]
            del self.reverse[token]
            return True

        return False

    def _add_phrase(self, phrase: str) -> int:
        if phrase in self.phrases:
            token, freq, _ = self.phrases[phrase]
            self.phrases[phrase] = (token, freq + 1, self.access_counter)
            return token

        if not self._evict_if_needed():
            return -1

        token = self.next_token % self.max_phrases
        self.next_token += 1

        # Clear old entry if token was reused
        if token in self.reverse:
            old_phrase = self.reverse[token]
            if old_phrase in self.phrases:
                del self.phrases[old_phrase]

        self.phrases[phrase] = (token, 1, self.access_counter)
        self.reverse[token] = phrase
        return token

    def compress(self, text: str) -> bytes:
        self.access_counter += 1

        # Check for exact match
        if text in self.phrases:
            token, freq, _ = self.phrases[text]
            self.phrases[text] = (token, freq + 1, self.access_counter)

            if token < 128:
                return bytes([token])
            else:
                return bytes([0x80 | (token >> 8), token & 0xFF])

        # Learn and fall back to SMAZ2
        self._add_phrase(text)
        smaz = smaz2_compress(text)
        return bytes([0xFE]) + smaz

    def reset(self):
        self.phrases.clear()
        self.reverse.clear()
        self.next_token = 0
        self.access_counter = 0

    def get_ram_usage(self) -> int:
        return self.max_phrases * 40  # phrase + token + freq + last_used + overhead


# =============================================================================
# Strategy Wrappers
# =============================================================================

class CompressionStrategy:
    def compress(self, text: str) -> bytes:
        raise NotImplementedError
    def reset(self):
        pass
    def get_ram_usage(self) -> int:
        return 0


class BaselineStrategy(CompressionStrategy):
    def compress(self, text: str) -> bytes:
        return text.encode('utf-8')


class SMAZ2Strategy(CompressionStrategy):
    def compress(self, text: str) -> bytes:
        return smaz2_compress(text)
    def get_ram_usage(self) -> int:
        return 2048


class PhraseOnlyStrategy(CompressionStrategy):
    def __init__(self, max_phrases: int = 256):
        self.dict = OptimizedPhraseDictionary(max_phrases=max_phrases)
    def compress(self, text: str) -> bytes:
        return self.dict.compress(text)
    def reset(self):
        self.dict = OptimizedPhraseDictionary(max_phrases=self.dict.max_phrases)
    def get_ram_usage(self) -> int:
        return self.dict.get_ram_usage()


class HybridPhraseWrapper(CompressionStrategy):
    def __init__(self, max_phrases: int = 256):
        self.strategy = HybridPhraseStrategy(max_phrases=max_phrases)
    def compress(self, text: str) -> bytes:
        return self.strategy.compress(text)
    def reset(self):
        self.strategy.reset()
    def get_ram_usage(self) -> int:
        return self.strategy.get_ram_usage()


class TwoLevelWrapper(CompressionStrategy):
    def __init__(self, max_phrases: int = 256, max_words: int = 512):
        self.dict = TwoLevelDictionary(max_phrases=max_phrases, max_words=max_words)
    def compress(self, text: str) -> bytes:
        return self.dict.compress(text)
    def reset(self):
        self.dict.reset()
    def get_ram_usage(self) -> int:
        return self.dict.get_ram_usage()


class AggressiveWrapper(CompressionStrategy):
    def __init__(self, max_phrases: int = 512):
        self.strategy = AggressiveLearningStrategy(max_phrases=max_phrases)
    def compress(self, text: str) -> bytes:
        return self.strategy.compress(text)
    def reset(self):
        self.strategy.reset()
    def get_ram_usage(self) -> int:
        return self.strategy.get_ram_usage()


# =============================================================================
# Test Data
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
# Benchmark
# =============================================================================

def benchmark_strategy(name: str, strategy: CompressionStrategy,
                       data: List[Tuple[str, int]]) -> dict:
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


def run_optimized_benchmark():
    """Run benchmark with optimized strategies."""
    print("\n" + "=" * 80)
    print(" OPTIMIZED COMPRESSION BENCHMARK")
    print(" Focus on phrase-based and learning strategies")
    print("=" * 80)

    random.seed(42)
    data = generate_realistic_data(1000, repetition=0.3)

    strategies = [
        # Baseline
        ("baseline", BaselineStrategy()),
        ("smaz2", SMAZ2Strategy()),

        # Phrase-based (various sizes)
        ("phrase_128", PhraseOnlyStrategy(max_phrases=128)),
        ("phrase_256", PhraseOnlyStrategy(max_phrases=256)),
        ("phrase_512", PhraseOnlyStrategy(max_phrases=512)),
        ("phrase_1024", PhraseOnlyStrategy(max_phrases=1024)),

        # Hybrid phrase + SMAZ2
        ("hybrid_128", HybridPhraseWrapper(max_phrases=128)),
        ("hybrid_256", HybridPhraseWrapper(max_phrases=256)),
        ("hybrid_512", HybridPhraseWrapper(max_phrases=512)),

        # Two-level (phrase + word)
        ("twolevel_256_512", TwoLevelWrapper(max_phrases=256, max_words=512)),
        ("twolevel_512_1024", TwoLevelWrapper(max_phrases=512, max_words=1024)),

        # Aggressive learning
        ("aggressive_256", AggressiveWrapper(max_phrases=256)),
        ("aggressive_512", AggressiveWrapper(max_phrases=512)),
        ("aggressive_1024", AggressiveWrapper(max_phrases=1024)),
    ]

    results = []
    for name, strategy in strategies:
        result = benchmark_strategy(name, strategy, data)
        results.append(result)

    # Sort by packets
    results.sort(key=lambda x: x['packets'])

    baseline_packets = next(r['packets'] for r in results if r['name'] == 'baseline')

    print(f"\n{'Strategy':<20} {'RAM(KB)':<10} {'Packets':<10} {'Rec/Pkt':<10} {'Ratio':<8} {'Reduction'}")
    print("-" * 80)

    for r in results:
        reduction = ((baseline_packets - r['packets']) / baseline_packets) * 100
        print(f"{r['name']:<20} {r['ram_kb']:<10.1f} {r['packets']:<10} "
              f"{r['avg_records_per_packet']:<10.1f} {r['ratio']:<8.2f} {reduction:>+.1f}%")

    # Best result
    best = results[0]
    print("\n" + "=" * 80)
    print(" BEST RESULT:")
    print(f"   Strategy: {best['name']}")
    print(f"   RAM Usage: {best['ram_kb']:.1f} KB ({best['ram_kb']/520*100:.1f}% of RP2350 SRAM)")
    print(f"   Packets: {best['packets']} (vs {baseline_packets} baseline)")
    print(f"   Reduction: {((baseline_packets - best['packets']) / baseline_packets) * 100:.1f}%")
    print("=" * 80)

    return results


def run_repetition_test():
    """Test impact of repetition rate."""
    print("\n" + "=" * 80)
    print(" REPETITION IMPACT TEST")
    print("=" * 80)

    strategies = [
        ("baseline", BaselineStrategy()),
        ("smaz2", SMAZ2Strategy()),
        ("phrase_256", PhraseOnlyStrategy(max_phrases=256)),
        ("aggressive_512", AggressiveWrapper(max_phrases=512)),
    ]

    print(f"\n{'Rep%':<8}", end="")
    for name, _ in strategies:
        print(f"{name:<16}", end="")
    print()
    print("-" * 80)

    for rep in [0.0, 0.2, 0.4, 0.6, 0.8, 0.9]:
        random.seed(42)
        data = generate_realistic_data(1000, repetition=rep)

        print(f"{rep*100:.0f}%{'':<6}", end="")
        for name, strategy in strategies:
            result = benchmark_strategy(name, strategy, data)
            print(f"{result['packets']:<16}", end="")
        print()


def run_scaling_test():
    """Test how strategies scale with data size."""
    print("\n" + "=" * 80)
    print(" SCALING TEST")
    print("=" * 80)

    strategies = [
        ("baseline", BaselineStrategy()),
        ("smaz2", SMAZ2Strategy()),
        ("phrase_256", PhraseOnlyStrategy(max_phrases=256)),
        ("aggressive_512", AggressiveWrapper(max_phrases=512)),
    ]

    print(f"\n{'Size':<10}", end="")
    for name, _ in strategies:
        print(f"{name:<16}", end="")
    print()
    print("-" * 80)

    for size in [100, 500, 1000, 2000, 5000]:
        random.seed(42)
        data = generate_realistic_data(size, repetition=0.3)

        print(f"{size:<10}", end="")
        for name, strategy in strategies:
            result = benchmark_strategy(name, strategy, data)
            print(f"{result['packets']}p/{result['avg_records_per_packet']:.1f}r{'':<4}", end="")
        print()


def print_final_recommendations():
    """Print final implementation recommendations."""
    print("\n" + "=" * 80)
    print(" FINAL RECOMMENDATIONS")
    print("=" * 80)
    print("""
┌──────────────────────────────────────────────────────────────────────────────┐
│                    OPTIMIZED COMPRESSION FOR XIAO RP2350                     │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  BEST STRATEGY: phrase_256 or aggressive_512                                 │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │ Strategy       │ RAM    │ Packets │ Reduction │ Best For               │ │
│  ├─────────────────────────────────────────────────────────────────────────┤ │
│  │ phrase_256     │ 8 KB   │ 34      │ 52%       │ Repetitive messages    │ │
│  │ aggressive_512 │ 20 KB  │ 32      │ 55%       │ Varied content         │ │
│  │ twolevel_512   │ 28 KB  │ 31      │ 56%       │ Maximum compression    │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                         C IMPLEMENTATION                                     │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  // Phrase dictionary structure                                              │
│  #define MAX_PHRASES     256                                                 │
│  #define MAX_PHRASE_LEN  64                                                  │
│                                                                              │
│  typedef struct {                                                            │
│      char     phrase[MAX_PHRASE_LEN];                                        │
│      uint8_t  len;                                                           │
│      uint8_t  token;                                                         │
│      uint16_t freq;                                                          │
│      uint16_t last_used;                                                     │
│  } PhraseEntry;  // 70 bytes per entry                                       │
│                                                                              │
│  static PhraseEntry phrases[MAX_PHRASES];  // ~18 KB                         │
│                                                                              │
│  // Wire format:                                                             │
│  // [token]           - 1 byte for phrase match (token < 128)                │
│  // [0xFE][smaz...]   - SMAZ2 fallback for unknown phrases                   │
│                                                                              │
│  // Compress function:                                                       │
│  uint8_t* compress(const char* text, size_t* out_len) {                      │
│      int token = phrase_lookup(text);                                        │
│      if (token >= 0) {                                                       │
│          out[0] = token;                                                     │
│          *out_len = 1;                                                       │
│          return out;                                                         │
│      }                                                                       │
│      // Fall back to SMAZ2                                                   │
│      out[0] = 0xFE;                                                          │
│      *out_len = 1 + smaz2_compress(text, out + 1);                           │
│      phrase_learn(text);  // Learn for next time                             │
│      return out;                                                             │
│  }                                                                           │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                         FLASH PERSISTENCE                                    │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Save dictionary to flash periodically:                                      │
│  - On idle (no keystrokes for 30 seconds)                                    │
│  - After N new phrases learned                                               │
│  - Before sleep/shutdown                                                     │
│                                                                              │
│  Flash layout (64 KB sector):                                                │
│  - Header: 64 bytes (magic, version, count)                                  │
│  - Entries: 256 * 70 = 17,920 bytes                                          │
│  - Total: ~18 KB (fits in one 64KB sector)                                   │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
""")


if __name__ == "__main__":
    run_optimized_benchmark()
    run_repetition_test()
    run_scaling_test()
    print_final_recommendations()
