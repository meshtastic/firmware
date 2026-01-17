#!/usr/bin/env python3
"""
REALISTIC KEYSTROKE COMPRESSION BENCHMARK

Simulates actual keystroke capture patterns:
- Usernames and passwords
- Email addresses
- URLs
- Form data
- Chat messages

RAM Budget: 100KB (19% of RP2350's 520KB SRAM)
"""

import struct
import random
import string
from dataclasses import dataclass
from typing import List, Tuple, Dict
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
    "@": 0x99, "/": 0x9A, ":": 0x9B, "-": 0x9C, "_": 0x9D,
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
    "www": 0xEB, "http": 0xEC, "https": 0xED, ".com": 0xEE, ".org": 0xEF,
    "gmail": 0xF0, "yahoo": 0xF1, "mail": 0xF2, "user": 0xF3, "pass": 0xF4,
    "login": 0xF5, "admin": 0xF6, "test": 0xF7, "1234": 0xF8, "password": 0xF9,
}


def smaz2_compress(text: str) -> bytes:
    """SMAZ2 compression with extended dictionary."""
    result = bytearray()
    i = 0
    text_lower = text.lower()

    while i < len(text):
        best_code = None
        best_len = 0

        # Try matches of decreasing length
        for length in [8, 5, 4, 3, 2, 1]:
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
# High-Capacity Phrase Dictionary (100KB RAM budget)
# =============================================================================

class HighCapacityDictionary:
    """
    High-capacity phrase dictionary for 100KB RAM budget.

    Structure:
    - 1024 phrase entries (64 bytes each) = 64KB
    - 1024 word entries (32 bytes each) = 32KB
    - Hash table and metadata = 4KB
    Total: ~100KB
    """

    def __init__(self, max_phrases: int = 1024, max_words: int = 1024):
        self.max_phrases = max_phrases
        self.max_words = max_words
        self.max_phrase_len = 64
        self.max_word_len = 32

        # Phrase dictionary
        self.phrases: Dict[str, int] = {}
        self.phrase_freq: Dict[str, int] = {}
        self.phrase_reverse: Dict[int, str] = {}
        self.next_phrase = 0

        # Word dictionary
        self.words: Dict[str, int] = {}
        self.word_freq: Dict[str, int] = {}
        self.word_reverse: Dict[int, str] = {}
        self.next_word = 0

        # Pre-seed with common patterns
        self._preseed()

    def _preseed(self):
        """Pre-seed with common keystroke patterns."""
        # Common URL patterns
        url_patterns = [
            "https://", "http://", "www.", ".com", ".org", ".net", ".io",
            "google.com", "facebook.com", "twitter.com", "github.com",
            "amazon.com", "youtube.com", "linkedin.com", "instagram.com",
        ]

        # Common email patterns
        email_patterns = [
            "@gmail.com", "@yahoo.com", "@hotmail.com", "@outlook.com",
            "@icloud.com", "@proton.me", "@mail.com",
        ]

        # Common form fields
        form_patterns = [
            "username", "password", "email", "login", "signin", "signup",
            "submit", "cancel", "confirm", "forgot", "reset", "remember",
        ]

        # Common words in passwords/usernames
        word_patterns = [
            "admin", "user", "test", "demo", "guest", "root", "master",
            "1234", "12345", "123456", "qwerty", "abc123", "letmein",
        ]

        # Add as phrases
        for p in url_patterns + email_patterns:
            self._add_phrase(p)

        # Add as words
        for w in form_patterns + word_patterns:
            self._add_word(w)

    def _add_phrase(self, phrase: str) -> int:
        if len(phrase) > self.max_phrase_len:
            phrase = phrase[:self.max_phrase_len]

        if phrase in self.phrases:
            self.phrase_freq[phrase] += 1
            return self.phrases[phrase]

        if self.next_phrase >= self.max_phrases:
            # Evict lowest frequency
            if self.phrase_freq:
                min_phrase = min(self.phrase_freq.items(), key=lambda x: x[1])
                if min_phrase[1] < 3:
                    token = self.phrases[min_phrase[0]]
                    del self.phrases[min_phrase[0]]
                    del self.phrase_freq[min_phrase[0]]
                    del self.phrase_reverse[token]
                    self.phrases[phrase] = token
                    self.phrase_freq[phrase] = 1
                    self.phrase_reverse[token] = phrase
                    return token
            return -1

        token = self.next_phrase
        self.next_phrase += 1
        self.phrases[phrase] = token
        self.phrase_freq[phrase] = 1
        self.phrase_reverse[token] = phrase
        return token

    def _add_word(self, word: str) -> int:
        if len(word) > self.max_word_len:
            word = word[:self.max_word_len]

        if word in self.words:
            self.word_freq[word] += 1
            return self.words[word]

        if self.next_word >= self.max_words:
            if self.word_freq:
                min_word = min(self.word_freq.items(), key=lambda x: x[1])
                if min_word[1] < 3:
                    token = self.words[min_word[0]]
                    del self.words[min_word[0]]
                    del self.word_freq[min_word[0]]
                    del self.word_reverse[token]
                    self.words[word] = token
                    self.word_freq[word] = 1
                    self.word_reverse[token] = word
                    return token
            return -1

        token = self.next_word
        self.next_word += 1
        self.words[word] = token
        self.word_freq[word] = 1
        self.word_reverse[token] = word
        return token

    def compress(self, text: str) -> bytes:
        """
        Compress using multi-level dictionary.

        Wire format:
        - [0x00-0x7F] = phrase token (1 byte)
        - [0x80][token_hi][token_lo] = phrase token >= 128 (3 bytes)
        - [0x90][token_hi][token_lo] = word token (3 bytes)
        - [0xFE][len][smaz_data] = SMAZ2 segment
        - [0xFF][len][raw_data] = raw literal
        """
        # Try exact phrase match first (best case)
        if text in self.phrases:
            token = self.phrases[text]
            self.phrase_freq[text] += 1
            if token < 128:
                return bytes([token])
            else:
                return bytes([0x80, (token >> 8) & 0xFF, token & 0xFF])

        # Learn phrase for next time
        self._add_phrase(text)

        # Try word-level compression
        result = bytearray()
        words = self._tokenize(text)

        for word in words:
            if word in self.words:
                token = self.words[word]
                self.word_freq[word] += 1
                result.append(0x90)
                result.append((token >> 8) & 0xFF)
                result.append(token & 0xFF)
            elif len(word) == 1:
                # Single character
                if 32 <= ord(word) < 128:
                    result.append(ord(word))
                else:
                    result.append(0xFF)
                    result.append(1)
                    result.append(ord(word) & 0xFF)
            else:
                # Try SMAZ2
                smaz = smaz2_compress(word)
                if len(smaz) < len(word):
                    result.append(0xFE)
                    result.append(len(smaz))
                    result.extend(smaz)
                else:
                    # Raw literal
                    word_bytes = word.encode('utf-8')
                    result.append(0xFF)
                    result.append(len(word_bytes))
                    result.extend(word_bytes)

                # Learn word
                if len(word) >= 3:
                    self._add_word(word)

        return bytes(result)

    def _tokenize(self, text: str) -> List[str]:
        """Tokenize preserving special characters."""
        tokens = []
        current = []

        for ch in text:
            if ch.isalnum() or ch in '_-':
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
        # Phrase entries: 64 bytes each (phrase + metadata)
        # Word entries: 32 bytes each
        # Hash tables and overhead: ~4KB
        return (self.max_phrases * 64) + (self.max_words * 32) + 4096


# =============================================================================
# Realistic Test Data Generation
# =============================================================================

# Common usernames
USERNAMES = [
    "john.doe", "jane_smith", "admin", "user123", "testuser",
    "mike.wilson", "sarah.jones", "dev_team", "support", "guest",
    "jsmith", "mwilson", "sjones", "administrator", "webmaster",
]

# Password patterns (simulated - NOT real passwords)
PASSWORD_PATTERNS = [
    "Password123!", "Qwerty2024", "Welcome1!", "Admin@123",
    "Summer2024!", "Winter2023#", "MyPass123", "Secret99!",
    "Letmein123", "Changeme1!", "Test1234!", "Demo@2024",
]

# Email domains
EMAIL_DOMAINS = [
    "@gmail.com", "@yahoo.com", "@hotmail.com", "@outlook.com",
    "@company.com", "@work.org", "@mail.net", "@proton.me",
]

# URL patterns
URL_BASES = [
    "https://www.google.com/search?q=",
    "https://github.com/",
    "https://stackoverflow.com/questions/",
    "https://www.amazon.com/dp/",
    "https://mail.google.com/mail/",
    "https://www.linkedin.com/in/",
    "https://twitter.com/",
    "https://docs.google.com/document/d/",
]

# Form field values
FORM_FIELDS = [
    "First Name", "Last Name", "Phone Number", "Address",
    "City", "State", "ZIP Code", "Country",
    "Card Number", "Expiry Date", "CVV", "Billing Address",
]

# Chat messages
CHAT_MESSAGES = [
    "Hey, how are you?", "Good morning!", "Thanks for your help",
    "Can we meet at 3pm?", "See you tomorrow", "Sounds good!",
    "Let me check and get back to you", "Sure, no problem",
    "What time works for you?", "I'll send it over",
    "Got it, thanks!", "Perfect, talk soon",
]

# Search queries
SEARCH_QUERIES = [
    "python tutorial", "how to fix", "best practices",
    "error message", "stack overflow", "github repository",
    "api documentation", "install guide", "troubleshooting",
]


def generate_realistic_keystrokes(count: int, scenario_mix: dict = None) -> List[Tuple[str, int]]:
    """
    Generate realistic keystroke data.

    scenario_mix: dict with weights for each scenario type
    - 'login': username/password entry
    - 'email': email composition
    - 'url': URL entry
    - 'form': form filling
    - 'chat': chat messages
    - 'search': search queries
    """
    if scenario_mix is None:
        scenario_mix = {
            'login': 0.15,
            'email': 0.20,
            'url': 0.15,
            'form': 0.15,
            'chat': 0.25,
            'search': 0.10,
        }

    data = []
    recent_entries = []

    for _ in range(count):
        # Pick scenario
        r = random.random()
        cumulative = 0

        for scenario, weight in scenario_mix.items():
            cumulative += weight
            if r < cumulative:
                break

        # 30% chance to repeat recent entry
        if recent_entries and random.random() < 0.30:
            text = random.choice(recent_entries)
        else:
            if scenario == 'login':
                if random.random() < 0.5:
                    # Username
                    text = random.choice(USERNAMES)
                else:
                    # Password
                    text = random.choice(PASSWORD_PATTERNS)

            elif scenario == 'email':
                if random.random() < 0.3:
                    # Email address
                    username = random.choice(USERNAMES).replace('.', '').replace('_', '')
                    domain = random.choice(EMAIL_DOMAINS)
                    text = f"{username}{domain}"
                else:
                    # Email content
                    text = random.choice(CHAT_MESSAGES)

            elif scenario == 'url':
                base = random.choice(URL_BASES)
                if random.random() < 0.5:
                    text = base
                else:
                    text = base + random.choice(SEARCH_QUERIES).replace(' ', '+')

            elif scenario == 'form':
                text = random.choice(FORM_FIELDS)
                if random.random() < 0.3:
                    # Add a value
                    if 'Name' in text:
                        text = random.choice(['John', 'Jane', 'Mike', 'Sarah', 'David'])
                    elif 'Number' in text or 'ZIP' in text:
                        text = str(random.randint(10000, 99999))
                    elif 'City' in text:
                        text = random.choice(['New York', 'Los Angeles', 'Chicago', 'Houston'])

            elif scenario == 'chat':
                text = random.choice(CHAT_MESSAGES)

            else:  # search
                text = random.choice(SEARCH_QUERIES)

            # Track recent for repetition
            recent_entries.append(text)
            if len(recent_entries) > 20:
                recent_entries.pop(0)

        # Delta time (mostly regular intervals)
        if random.random() < 0.7:
            delta = random.randint(100, 500)  # Quick typing
        else:
            delta = random.randint(1000, 5000)  # Pause between fields

        data.append((text, delta))

    return data


# =============================================================================
# Compression Strategies
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


class HighCapacityStrategy(CompressionStrategy):
    def __init__(self, max_phrases: int = 1024, max_words: int = 1024):
        self.dict = HighCapacityDictionary(max_phrases=max_phrases, max_words=max_words)
    def compress(self, text: str) -> bytes:
        return self.dict.compress(text)
    def reset(self):
        self.dict.reset()
    def get_ram_usage(self) -> int:
        return self.dict.get_ram_usage()


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


def run_realistic_benchmark():
    """Run benchmark with realistic keystroke data."""
    print("\n" + "=" * 85)
    print(" REALISTIC KEYSTROKE COMPRESSION BENCHMARK")
    print(" Data: Usernames, Passwords, URLs, Emails, Chat, Forms")
    print(" RAM Budget: 100KB (19% of RP2350's 520KB SRAM)")
    print("=" * 85)

    random.seed(42)
    data = generate_realistic_keystrokes(1000)

    # Show sample data
    print("\n SAMPLE DATA (first 10 entries):")
    print("-" * 60)
    for i, (text, delta) in enumerate(data[:10]):
        print(f"  {i+1}. [{delta}ms] {text[:50]}{'...' if len(text) > 50 else ''}")

    strategies = [
        ("baseline", BaselineStrategy()),
        ("smaz2_2kb", SMAZ2Strategy()),
        ("highcap_25kb", HighCapacityStrategy(max_phrases=256, max_words=256)),
        ("highcap_50kb", HighCapacityStrategy(max_phrases=512, max_words=512)),
        ("highcap_100kb", HighCapacityStrategy(max_phrases=1024, max_words=1024)),
        ("highcap_150kb", HighCapacityStrategy(max_phrases=1536, max_words=1536)),
    ]

    results = []
    for name, strategy in strategies:
        result = benchmark_strategy(name, strategy, data)
        results.append(result)

    results.sort(key=lambda x: x['packets'])

    baseline_packets = next(r['packets'] for r in results if r['name'] == 'baseline')

    print(f"\n{'Strategy':<16} {'RAM(KB)':<10} {'Packets':<10} {'Rec/Pkt':<10} {'Ratio':<8} {'Reduction'}")
    print("-" * 85)

    for r in results:
        reduction = ((baseline_packets - r['packets']) / baseline_packets) * 100
        print(f"{r['name']:<16} {r['ram_kb']:<10.1f} {r['packets']:<10} "
              f"{r['avg_records_per_packet']:<10.1f} {r['ratio']:<8.2f} {reduction:>+.1f}%")

    best = results[0]
    print("\n" + "=" * 85)
    print(" BEST RESULT:")
    print(f"   Strategy: {best['name']}")
    print(f"   RAM Usage: {best['ram_kb']:.1f} KB ({best['ram_kb']/520*100:.1f}% of RP2350)")
    print(f"   Packets: {best['packets']} (vs {baseline_packets} baseline)")
    print(f"   Reduction: {((baseline_packets - best['packets']) / baseline_packets) * 100:.1f}%")
    print("=" * 85)

    return results


def run_scenario_breakdown():
    """Test each scenario type separately."""
    print("\n" + "=" * 85)
    print(" SCENARIO BREAKDOWN TEST")
    print("=" * 85)

    scenarios = {
        'login_only': {'login': 1.0, 'email': 0, 'url': 0, 'form': 0, 'chat': 0, 'search': 0},
        'email_only': {'login': 0, 'email': 1.0, 'url': 0, 'form': 0, 'chat': 0, 'search': 0},
        'url_only': {'login': 0, 'email': 0, 'url': 1.0, 'form': 0, 'chat': 0, 'search': 0},
        'form_only': {'login': 0, 'email': 0, 'url': 0, 'form': 1.0, 'chat': 0, 'search': 0},
        'chat_only': {'login': 0, 'email': 0, 'url': 0, 'form': 0, 'chat': 1.0, 'search': 0},
        'mixed': {'login': 0.15, 'email': 0.20, 'url': 0.15, 'form': 0.15, 'chat': 0.25, 'search': 0.10},
    }

    strategy = HighCapacityStrategy(max_phrases=1024, max_words=1024)

    print(f"\n{'Scenario':<14} {'Baseline':<12} {'100KB Dict':<12} {'Rec/Pkt':<10} {'Reduction'}")
    print("-" * 60)

    for scenario_name, mix in scenarios.items():
        random.seed(42)
        data = generate_realistic_keystrokes(500, scenario_mix=mix)

        baseline = benchmark_strategy("baseline", BaselineStrategy(), data)
        optimized = benchmark_strategy("highcap", strategy, data)

        reduction = ((baseline['packets'] - optimized['packets']) / baseline['packets']) * 100
        print(f"{scenario_name:<14} {baseline['packets']:<12} {optimized['packets']:<12} "
              f"{optimized['avg_records_per_packet']:<10.1f} {reduction:>+.1f}%")


def run_scaling_test():
    """Test with different data sizes."""
    print("\n" + "=" * 85)
    print(" SCALING TEST (100KB Dictionary)")
    print("=" * 85)

    strategy = HighCapacityStrategy(max_phrases=1024, max_words=1024)

    print(f"\n{'Count':<10} {'Baseline':<12} {'100KB Dict':<12} {'Rec/Pkt':<10} {'Reduction'}")
    print("-" * 60)

    for count in [100, 500, 1000, 2000, 5000, 10000]:
        random.seed(42)
        data = generate_realistic_keystrokes(count)

        baseline = benchmark_strategy("baseline", BaselineStrategy(), data)
        optimized = benchmark_strategy("highcap", strategy, data)

        reduction = ((baseline['packets'] - optimized['packets']) / baseline['packets']) * 100
        print(f"{count:<10} {baseline['packets']:<12} {optimized['packets']:<12} "
              f"{optimized['avg_records_per_packet']:<10.1f} {reduction:>+.1f}%")


def print_implementation():
    """Print C implementation guide."""
    print("\n" + "=" * 85)
    print(" C IMPLEMENTATION FOR XIAO RP2350 (100KB RAM)")
    print("=" * 85)
    print("""
┌──────────────────────────────────────────────────────────────────────────────────┐
│                         MEMORY LAYOUT (100KB)                                     │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                   │
│  // Phrase dictionary: 1024 entries x 64 bytes = 64KB                            │
│  typedef struct {                                                                 │
│      char     text[56];      // Phrase text                                       │
│      uint16_t token;         // Compression token                                 │
│      uint16_t freq;          // Usage frequency                                   │
│      uint16_t hash_next;     // Hash chain pointer                                │
│      uint16_t reserved;      // Alignment padding                                 │
│  } PhraseEntry;              // 64 bytes                                          │
│                                                                                   │
│  // Word dictionary: 1024 entries x 32 bytes = 32KB                              │
│  typedef struct {                                                                 │
│      char     text[24];      // Word text                                         │
│      uint16_t token;         // Compression token                                 │
│      uint16_t freq;          // Usage frequency                                   │
│      uint16_t hash_next;     // Hash chain pointer                                │
│      uint16_t reserved;      // Alignment padding                                 │
│  } WordEntry;                // 32 bytes                                          │
│                                                                                   │
│  // Hash tables: 2 x 1024 x 2 bytes = 4KB                                        │
│  static uint16_t phrase_hash[1024];                                              │
│  static uint16_t word_hash[1024];                                                │
│                                                                                   │
│  // Total: 64KB + 32KB + 4KB = 100KB                                             │
│                                                                                   │
├──────────────────────────────────────────────────────────────────────────────────┤
│                         WIRE FORMAT                                               │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                   │
│  [0x00-0x7F]           = Phrase token (1 byte for full phrase!)                  │
│  [0x80][hi][lo]        = Phrase token >= 128 (3 bytes)                           │
│  [0x90][hi][lo]        = Word token (3 bytes)                                    │
│  [0xFE][len][smaz...]  = SMAZ2 compressed segment                                │
│  [0xFF][len][raw...]   = Raw literal bytes                                       │
│  [0x20-0x7E]           = ASCII literal (1 byte)                                  │
│                                                                                   │
├──────────────────────────────────────────────────────────────────────────────────┤
│                         COMPRESSION FUNCTION                                      │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                   │
│  size_t compress(const char* text, uint8_t* out) {                               │
│      // 1. Try full phrase match (best case: 1 byte)                             │
│      int token = phrase_lookup(text);                                            │
│      if (token >= 0) {                                                           │
│          if (token < 128) {                                                      │
│              out[0] = token;                                                     │
│              return 1;                                                           │
│          }                                                                       │
│          out[0] = 0x80;                                                          │
│          out[1] = token >> 8;                                                    │
│          out[2] = token & 0xFF;                                                  │
│          return 3;                                                               │
│      }                                                                           │
│                                                                                   │
│      // 2. Learn phrase for next time                                            │
│      phrase_learn(text);                                                         │
│                                                                                   │
│      // 3. Tokenize and compress word-by-word                                    │
│      size_t out_len = 0;                                                         │
│      for (word in tokenize(text)) {                                              │
│          if (word in word_dict) {                                                │
│              out[out_len++] = 0x90;                                              │
│              out[out_len++] = word_token >> 8;                                   │
│              out[out_len++] = word_token & 0xFF;                                 │
│          } else if (is_single_ascii(word)) {                                     │
│              out[out_len++] = word[0];                                           │
│          } else {                                                                │
│              // SMAZ2 fallback                                                   │
│              out[out_len++] = 0xFE;                                              │
│              size_t smaz_len = smaz2_compress(word, out + out_len + 1);          │
│              out[out_len++] = smaz_len;                                          │
│              out_len += smaz_len;                                                │
│              word_learn(word);                                                   │
│          }                                                                       │
│      }                                                                           │
│      return out_len;                                                             │
│  }                                                                               │
│                                                                                   │
└──────────────────────────────────────────────────────────────────────────────────┘
""")


if __name__ == "__main__":
    run_realistic_benchmark()
    run_scenario_breakdown()
    run_scaling_test()
    print_implementation()
