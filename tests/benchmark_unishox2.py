#!/usr/bin/env python3
"""
UNISHOX2 vs SMAZ2 BENCHMARK

Unishox2: Bit-level Huffman-like encoding with character frequency codes
SMAZ2: Byte-aligned bigram + word dictionary

Comparing for keystroke capture scenarios.
"""

import random
from typing import List, Tuple, Dict
from dataclasses import dataclass


# =============================================================================
# UNISHOX2 IMPLEMENTATION (Simplified Python port)
# Based on https://github.com/siara-cc/Unishox2
# =============================================================================

class Unishox2:
    """
    Simplified Unishox2 implementation.

    Key concepts:
    1. Characters encoded with variable-length bit codes
    2. Frequent chars get shorter codes (Huffman-like)
    3. Character sets: ALPHA, SYM, NUM with switch codes
    4. Common sequences pre-encoded
    """

    # Character frequency order (most frequent first)
    ALPHA_CHARS = " etaoinsrhldcumfpgwybvkxjqz"

    # Vertical codes (bit patterns) - shorter for frequent chars
    # Format: (bits, length)
    VCODES = [
        (0b00, 2),      # 0: most frequent
        (0b010, 3),     # 1
        (0b011, 3),     # 2
        (0b100, 3),     # 3
        (0b1010, 4),    # 4
        (0b1011, 4),    # 5
        (0b1100, 4),    # 6
        (0b11010, 5),   # 7
        (0b11011, 5),   # 8
        (0b11100, 5),   # 9
        (0b111010, 6),  # 10
        (0b111011, 6),  # 11
        (0b111100, 6),  # 12
        (0b1111010, 7), # 13
        (0b1111011, 7), # 14
        (0b1111100, 7), # 15
        (0b11111010, 8),# 16
        (0b11111011, 8),# 17
        (0b11111100, 8),# 18
        (0b11111101, 8),# 19
        (0b11111110, 8),# 20
        (0b11111111, 8),# 21+
    ]

    # Set switch codes
    SWITCH_ALPHA = (0b00, 2)
    SWITCH_SYM = (0b01, 2)
    SWITCH_NUM = (0b10, 2)

    # Symbol set
    SYM_CHARS = " .,;:!?'\"-()[]{}@#$%&*+=<>/\\|~`^_"

    # Numeric set
    NUM_CHARS = "0123456789+-*/.,:;()%"

    # Common sequences (pre-encoded)
    SEQUENCES = {
        "://": 0,
        "https": 1,
        "http": 2,
        "www.": 3,
        ".com": 4,
        ".org": 5,
        "the ": 6,
        "ing ": 7,
        "tion": 8,
        " the": 9,
    }

    def __init__(self):
        # Build lookup tables
        self.alpha_map = {c: i for i, c in enumerate(self.ALPHA_CHARS)}
        self.sym_map = {c: i for i, c in enumerate(self.SYM_CHARS)}
        self.num_map = {c: i for i, c in enumerate(self.NUM_CHARS)}

    def compress(self, text: str) -> bytes:
        """Compress using Unishox2 algorithm."""
        bits = []
        current_set = 'ALPHA'
        i = 0
        text_lower = text.lower()

        while i < len(text):
            # Try sequence match
            seq_matched = False
            for seq, seq_id in self.SEQUENCES.items():
                if text_lower[i:].startswith(seq):
                    # Emit sequence code
                    bits.extend([1, 1, 1, 1, 1, 0])  # Sequence marker
                    # Emit sequence ID (4 bits)
                    for b in range(4):
                        bits.append((seq_id >> (3-b)) & 1)
                    i += len(seq)
                    seq_matched = True
                    break

            if seq_matched:
                continue

            ch = text_lower[i]

            # Determine character set and position
            if ch in self.alpha_map:
                if current_set != 'ALPHA':
                    bits.extend([0, 0])  # Switch to ALPHA
                    current_set = 'ALPHA'
                pos = self.alpha_map[ch]

            elif ch in self.num_map:
                if current_set != 'NUM':
                    bits.extend([1, 0])  # Switch to NUM
                    current_set = 'NUM'
                pos = self.num_map[ch]

            elif ch in self.sym_map:
                if current_set != 'SYM':
                    bits.extend([0, 1])  # Switch to SYM
                    current_set = 'SYM'
                pos = self.sym_map[ch]

            else:
                # Unknown char - emit as literal
                bits.extend([1, 1, 1, 1, 1, 1, 1, 1])  # Literal marker
                for b in range(8):
                    bits.append((ord(ch) >> (7-b)) & 1)
                i += 1
                continue

            # Emit vertical code for position
            if pos < len(self.VCODES):
                code, length = self.VCODES[pos]
                for b in range(length):
                    bits.append((code >> (length-1-b)) & 1)
            else:
                # Extended position
                bits.extend([1, 1, 1, 1, 1, 1])
                for b in range(5):
                    bits.append((pos >> (4-b)) & 1)

            i += 1

        # Pack bits into bytes
        result = bytearray()
        for i in range(0, len(bits), 8):
            byte = 0
            for j in range(8):
                if i + j < len(bits):
                    byte |= bits[i + j] << (7 - j)
            result.append(byte)

        return bytes(result)

    def get_ram_usage(self) -> int:
        return 512  # Minimal - just lookup tables

    def get_name(self) -> str:
        return "unishox2"


# =============================================================================
# SMAZ2 IMPLEMENTATION
# =============================================================================

class SMAZ2:
    """Original SMAZ2 for comparison."""

    BIGRAMS = (
        "intherreheanonesorteattistenntartondalitseediseangoulecomenerirode"
        "raioicliofasetvetasihamaecomceelllcaurlachhidihofonsotacnarssoprrts"
        "assusnoiltsemctgeloeebetrnipeiepancpooldaadviunamutwimoshyoaiewowos"
        "fiepttmiopiaweagsuiddoooirspplscaywaigeirylytuulivimabty"
    )

    WORDS = [
        "that", "this", "with", "from", "your", "have", "more", "will",
        "home", "about", "page", "search", "free", "other", "information", "time",
        "they", "what", "which", "their", "news", "there", "only", "when",
        "contact", "here", "business", "also", "help", "view", "online", "first",
        "been", "would", "were", "some", "these", "click", "like", "service",
        "than", "find", "date", "back", "people", "list", "name", "just",
        "over", "year", "into", "email", "health", "world", "next", "used",
        "work", "last", "most", "music", "data", "make", "them", "should",
        "product", "post", "city", "policy", "number", "such", "please", "available",
        "copyright", "support", "message", "after", "best", "software", "then", "good",
        "video", "well", "where", "info", "right", "public", "high", "school",
        "through", "each", "order", "very", "privacy", "book", "item", "company",
    ]

    def __init__(self):
        self.bigram_map = {}
        for i in range(0, len(self.BIGRAMS), 2):
            bg = self.BIGRAMS[i:i+2]
            self.bigram_map[bg] = i // 2
        self.word_map = {w: i for i, w in enumerate(self.WORDS)}

    def compress(self, text: str) -> bytes:
        result = bytearray()
        verbatim = bytearray()
        i = 0
        text_lower = text.lower()

        def flush():
            nonlocal verbatim
            while verbatim:
                chunk = verbatim[:5]
                verbatim = verbatim[5:]
                result.append(len(chunk))
                result.extend(chunk)

        while i < len(text):
            matched = False

            # Try word
            for wlen in range(min(16, len(text) - i), 3, -1):
                word = text_lower[i:i+wlen]
                if word in self.word_map:
                    flush()
                    result.append(6)
                    result.append(self.word_map[word])
                    i += wlen
                    matched = True
                    break

            if matched:
                continue

            # Try bigram
            if i + 2 <= len(text):
                bg = text_lower[i:i+2]
                if bg in self.bigram_map:
                    flush()
                    result.append(128 + self.bigram_map[bg])
                    i += 2
                    continue

            verbatim.append(ord(text[i]))
            i += 1

        flush()
        return bytes(result)

    def get_ram_usage(self) -> int:
        return 4096

    def get_name(self) -> str:
        return "smaz2"


# =============================================================================
# SMAZ2 EXTENDED (with keystroke vocabulary)
# =============================================================================

class SMAZ2Extended(SMAZ2):
    """SMAZ2 with extended keystroke vocabulary."""

    EXTRA_WORDS = [
        "username", "password", "login", "admin", "user", "test", "welcome",
        "https", "http", "www", "com", "org", "gmail", "google", "github",
        "facebook", "twitter", "amazon", "youtube", "linkedin",
        "meeting", "thanks", "tomorrow", "today", "hello", "morning",
        "afternoon", "evening", "please", "sorry", "okay",
    ]

    def __init__(self):
        super().__init__()
        all_words = self.WORDS + self.EXTRA_WORDS
        self.word_map = {w: i for i, w in enumerate(all_words)}

    def get_name(self) -> str:
        return "smaz2_ext"


# =============================================================================
# TEST DATA
# =============================================================================

def generate_test_data() -> List[Tuple[str, str]]:
    """Generate diverse test data."""
    return [
        # URLs
        ("URL", "https://www.google.com"),
        ("URL", "https://github.com/user/repo"),
        ("URL", "http://example.com/path?query=test"),

        # Emails
        ("Email", "john.doe@gmail.com"),
        ("Email", "admin@company.org"),
        ("Email", "support@example.com"),

        # Passwords
        ("Password", "Password123!"),
        ("Password", "Welcome2024!"),
        ("Password", "Admin@12345"),

        # Usernames
        ("Username", "john_doe_2024"),
        ("Username", "admin_user"),
        ("Username", "test123"),

        # Messages
        ("Message", "Hello World"),
        ("Message", "Good morning everyone"),
        ("Message", "Thanks for your help"),
        ("Message", "Meeting at 3pm tomorrow"),
        ("Message", "The quick brown fox jumps"),

        # Mixed
        ("Mixed", "Login: admin Password: secret123"),
        ("Mixed", "Visit https://example.com for more info"),
        ("Mixed", "Contact support@company.com"),
    ]


# =============================================================================
# BENCHMARK
# =============================================================================

class BaselineCompressor:
    def compress(self, text: str) -> bytes:
        return text.encode('utf-8')
    def get_ram_usage(self) -> int:
        return 0
    def get_name(self) -> str:
        return "baseline"


def run_benchmark():
    """Run comparison benchmark."""
    print("=" * 85)
    print(" UNISHOX2 vs SMAZ2 BENCHMARK")
    print(" Comparing bit-level vs byte-level compression")
    print("=" * 85)

    test_data = generate_test_data()

    compressors = [
        BaselineCompressor(),
        SMAZ2(),
        SMAZ2Extended(),
        Unishox2(),
    ]

    print("\n INDIVIDUAL STRING COMPRESSION:")
    print("-" * 85)
    print(f"{'Type':<10} {'String':<35} {'Raw':>5} ", end="")
    for c in compressors[1:]:
        print(f"{c.get_name():>12}", end="")
    print()
    print("-" * 85)

    totals = {c.get_name(): 0 for c in compressors}
    total_raw = 0

    for type_name, text in test_data:
        raw_len = len(text.encode('utf-8'))
        total_raw += raw_len
        totals['baseline'] += raw_len

        print(f"{type_name:<10} {text[:35]:<35} {raw_len:>5} ", end="")

        for comp in compressors[1:]:
            compressed = comp.compress(text)
            comp_len = len(compressed)
            totals[comp.get_name()] += comp_len
            savings = (1 - comp_len / raw_len) * 100
            print(f"{comp_len:>4} ({savings:>+4.0f}%)", end="")
        print()

    print("-" * 85)
    print(f"{'TOTAL':<10} {'':<35} {total_raw:>5} ", end="")
    for comp in compressors[1:]:
        t = totals[comp.get_name()]
        savings = (1 - t / total_raw) * 100
        print(f"{t:>4} ({savings:>+4.0f}%)", end="")
    print()

    # Summary
    print("\n" + "=" * 85)
    print(" SUMMARY")
    print("=" * 85)
    print(f"\n{'Compressor':<15} {'RAM':>10} {'Total Bytes':>15} {'Compression':>15}")
    print("-" * 60)

    for comp in compressors:
        t = totals[comp.get_name()]
        savings = (1 - t / total_raw) * 100
        print(f"{comp.get_name():<15} {comp.get_ram_usage():>10} {t:>15} {savings:>+14.1f}%")

    return totals


def run_type_breakdown():
    """Analyze compression by data type."""
    print("\n" + "=" * 85)
    print(" COMPRESSION BY DATA TYPE")
    print("=" * 85)

    test_data = generate_test_data()

    compressors = [
        ("smaz2", SMAZ2()),
        ("smaz2_ext", SMAZ2Extended()),
        ("unishox2", Unishox2()),
    ]

    # Group by type
    by_type = {}
    for type_name, text in test_data:
        if type_name not in by_type:
            by_type[type_name] = []
        by_type[type_name].append(text)

    print(f"\n{'Data Type':<12}", end="")
    for name, _ in compressors:
        print(f"{name:>15}", end="")
    print()
    print("-" * 60)

    for type_name, texts in by_type.items():
        print(f"{type_name:<12}", end="")

        for name, comp in compressors:
            total_raw = sum(len(t.encode('utf-8')) for t in texts)
            total_comp = sum(len(comp.compress(t)) for t in texts)
            savings = (1 - total_comp / total_raw) * 100
            print(f"{savings:>+14.1f}%", end="")
        print()


def print_analysis():
    """Print analysis and recommendations."""
    print("\n" + "=" * 85)
    print(" ANALYSIS: UNISHOX2 vs SMAZ2")
    print("=" * 85)
    print("""
┌──────────────────────────────────────────────────────────────────────────────────────┐
│                         ALGORITHM COMPARISON                                          │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                       │
│  SMAZ2 (Byte-aligned):                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│  │ + Simple byte-aligned encoding                                                  │ │
│  │ + Fast compression/decompression                                                │ │
│  │ + Word dictionary for common terms                                              │ │
│  │ - Minimum 1 byte per character                                                  │ │
│  │ - Bigrams only save 1 byte (2 chars → 1 byte)                                   │ │
│  └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                       │
│  UNISHOX2 (Bit-aligned):                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│  │ + Variable-length bit codes (2-8 bits per char)                                 │ │
│  │ + Frequent chars ('e','t','a') get 2-3 bits                                     │ │
│  │ + Pre-encoded sequences ("://", "https", ".com")                                │ │
│  │ + Better theoretical compression                                                │ │
│  │ - Slower (bit manipulation)                                                     │ │
│  │ - More complex implementation                                                   │ │
│  └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                       │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                         WHEN TO USE WHICH                                            │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                       │
│  USE SMAZ2 + Extended Words when:                                                    │
│  • CPU cycles matter (fast encode/decode)                                            │
│  • Data has known vocabulary (login, URLs, emails)                                   │
│  • Simpler implementation needed                                                     │
│                                                                                       │
│  USE UNISHOX2 when:                                                                  │
│  • Maximum compression needed                                                        │
│  • Data is mostly natural English text                                               │
│  • CPU has cycles to spare                                                           │
│                                                                                       │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                         HYBRID APPROACH                                              │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                       │
│  Best of both worlds:                                                                │
│  1. Try phrase dictionary first (if match: 1 byte)                                  │
│  2. Try word dictionary (if match: 2 bytes)                                         │
│  3. Fall back to Unishox2 for remaining text                                        │
│                                                                                       │
│  Expected compression: 30-40% for cold start, 60-75% with repetition                │
│                                                                                       │
└──────────────────────────────────────────────────────────────────────────────────────┘
""")


if __name__ == "__main__":
    run_benchmark()
    run_type_breakdown()
    print_analysis()
