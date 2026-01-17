#!/usr/bin/env python3
"""
SMAZ2 IMPROVED - Enhanced compression for keystroke capture

Based on antirez/smaz2 (https://github.com/antirez/smaz2)
Extended with:
- 256 bigrams (vs original 128)
- 512 words (vs original 256)
- Keystroke-specific patterns (URLs, emails, passwords)
- Trigram support for common sequences

RAM usage estimates for RP2350:
- Original SMAZ2: ~4KB
- SMAZ2 Improved: ~12KB
- SMAZ2 Max: ~24KB
"""

import struct
import random
from typing import List, Tuple, Dict, Optional
from dataclasses import dataclass


# =============================================================================
# ORIGINAL SMAZ2 CODEBOOK (from antirez/smaz2)
# =============================================================================

# Original 128 bigrams (256 bytes)
SMAZ2_ORIGINAL_BIGRAMS = (
    "intherreheanonesorteattistenntartondalitseediseangoulecomenerirode"
    "raioicliofasetvetasihamaecomceelllcaurlachhidihofonsotacnarssoprrts"
    "assusnoiltsemctgeloeebetrnipeiepancpooldaadviunamutwimoshyoaiewowos"
    "fiepttmiopiaweagsuiddoooirspplscaywaigeirylytuulivimabty"
)

# Original 256 words
SMAZ2_ORIGINAL_WORDS = [
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
    "read", "group", "need", "many", "user", "said", "does", "under",
    "general", "research", "university", "january", "mail", "full", "review", "program",
    "life", "know", "days", "management", "part", "could", "great", "united",
    "real", "international", "center", "ebay", "must", "store", "travel", "comment",
    "made", "development", "report", "detail", "line", "term", "before", "hotel",
    "send", "type", "because", "local", "those", "using", "result", "office",
    "education", "national", "design", "take", "posted", "internet", "address", "community",
    "within", "state", "area", "want", "phone", "shipping", "reserved", "subject",
    "between", "forum", "family", "long", "based", "code", "show", "even",
    "black", "check", "special", "price", "website", "index", "being", "women",
    "much", "sign", "file", "link", "open", "today", "technology", "south",
    "case", "project", "same", "version", "section", "found", "sport", "house",
    "related", "security", "both", "county", "american", "game", "member", "power",
    "while", "care", "network", "down", "computer", "system", "three", "total",
    "place", "following", "download", "without", "access", "think", "north", "resource",
    "current", "media", "control", "water", "history", "picture", "size", "personal",
    "since", "including", "guide", "shop", "directory", "board", "location", "change",
    "white", "text", "small", "rating", "rate", "government", "child", "during",
    "return", "student", "shopping", "account", "site", "level", "digital", "profile",
    "previous", "form", "event", "love", "main", "another", "class", "still",
]


# =============================================================================
# EXTENDED BIGRAMS (256 total - 128 new)
# =============================================================================

# Additional bigrams for keystroke patterns
SMAZ2_EXTENDED_BIGRAMS = (
    # URL patterns
    "htpsww.c.o.n.i//:@##$$%%&&**()[]{}|\\<>?!-_+=~`"
    # Email patterns
    "gmyahomaiolookuprobliveiclonetflixyoutubefacebtwit"
    # Common password patterns
    "12233445566778899001qwertasdfzxcvbnm"
    # Additional common bigrams
    "myweusitgoifnosoambyupowdoaaborwamede"
)

# Combined bigrams (256 total = 512 bytes)
SMAZ2_BIGRAMS_256 = SMAZ2_ORIGINAL_BIGRAMS + SMAZ2_EXTENDED_BIGRAMS[:256 - len(SMAZ2_ORIGINAL_BIGRAMS)//2*2]


# =============================================================================
# EXTENDED WORDS (512 total - 256 new for keystrokes)
# =============================================================================

SMAZ2_KEYSTROKE_WORDS = [
    # Login/auth words
    "username", "password", "login", "logout", "signin", "signup", "register",
    "forgot", "reset", "remember", "captcha", "verify", "confirm", "submit",
    "admin", "root", "guest", "test", "demo", "user", "account",

    # URL components
    "https", "http", "www", "com", "org", "net", "edu", "gov", "io", "co",
    "html", "php", "asp", "jsp", "cgi", "api", "json", "xml", "css",

    # Domains
    "google", "facebook", "twitter", "instagram", "linkedin", "youtube",
    "amazon", "ebay", "netflix", "spotify", "github", "stackoverflow",
    "reddit", "wikipedia", "microsoft", "apple", "yahoo", "bing",

    # Email
    "gmail", "hotmail", "outlook", "icloud", "proton", "mail",
    "inbox", "sent", "draft", "spam", "trash", "compose", "reply", "forward",

    # Form fields
    "firstname", "lastname", "middlename", "fullname", "nickname",
    "phone", "mobile", "cell", "fax", "telephone",
    "address", "street", "city", "state", "country", "zip", "postal",
    "birthday", "birth", "age", "gender", "male", "female",

    # Payment
    "card", "credit", "debit", "visa", "mastercard", "amex", "paypal",
    "cvv", "expiry", "billing", "shipping", "checkout", "cart", "order",

    # Common typed phrases
    "hello", "world", "thanks", "thank", "please", "sorry", "okay",
    "yes", "no", "maybe", "sure", "great", "good", "bad", "nice",

    # Status words
    "error", "success", "failed", "complete", "pending", "loading",
    "status", "active", "inactive", "enabled", "disabled", "online", "offline",

    # Time words
    "today", "tomorrow", "yesterday", "monday", "tuesday", "wednesday",
    "thursday", "friday", "saturday", "sunday", "january", "february",
    "march", "april", "may", "june", "july", "august", "september",
    "october", "november", "december", "morning", "afternoon", "evening", "night",

    # Numbers as words
    "zero", "one", "two", "three", "four", "five", "six", "seven",
    "eight", "nine", "ten", "hundred", "thousand", "million",

    # Tech words
    "server", "client", "database", "query", "table", "field", "column",
    "file", "folder", "directory", "path", "upload", "download",
    "save", "load", "delete", "create", "update", "insert", "select",

    # Social
    "friend", "follow", "like", "share", "comment", "post", "feed",
    "profile", "avatar", "photo", "image", "video", "audio", "media",

    # Common verbs
    "click", "press", "type", "enter", "submit", "cancel", "close",
    "open", "start", "stop", "pause", "play", "next", "previous", "back",
]

# Combined words (512 total)
SMAZ2_WORDS_512 = SMAZ2_ORIGINAL_WORDS + SMAZ2_KEYSTROKE_WORDS[:256]


# =============================================================================
# TRIGRAMS (common 3-char sequences)
# =============================================================================

SMAZ2_TRIGRAMS = [
    # Common English trigrams
    "the", "and", "ing", "ion", "tio", "ent", "ati", "for", "her", "ter",
    "hat", "tha", "ere", "ate", "his", "con", "res", "ver", "all", "ons",
    "nce", "men", "ith", "ted", "ers", "pro", "thi", "wit", "are", "ess",

    # URL trigrams
    "www", "http", "com", "org", "net", "htm", "php", "asp", "api",

    # Password trigrams
    "123", "234", "345", "456", "567", "678", "789", "890", "abc", "xyz",
    "qwe", "wer", "ert", "asd", "sdf", "dfg", "zxc", "xcv", "cvb",

    # Common word endings
    "ing", "tion", "ment", "ness", "able", "ible", "ful", "less", "ous",
]


# =============================================================================
# SMAZ2 COMPRESSOR CLASSES
# =============================================================================

class SMAZ2Original:
    """Original SMAZ2 implementation (128 bigrams, 256 words)."""

    def __init__(self):
        self.bigrams = SMAZ2_ORIGINAL_BIGRAMS
        self.words = SMAZ2_ORIGINAL_WORDS
        self._build_lookups()

    def _build_lookups(self):
        # Build bigram lookup
        self.bigram_map = {}
        for i in range(0, len(self.bigrams), 2):
            bg = self.bigrams[i:i+2]
            self.bigram_map[bg] = i // 2

        # Build word lookup
        self.word_map = {w: i for i, w in enumerate(self.words)}

    def compress(self, text: str) -> bytes:
        """Compress using SMAZ2 algorithm."""
        result = bytearray()
        verbatim = bytearray()
        i = 0
        text_lower = text.lower()

        def flush_verbatim():
            nonlocal verbatim
            if not verbatim:
                return
            # Encode verbatim: length marker (1-5) followed by bytes
            while len(verbatim) > 0:
                chunk = verbatim[:5]
                verbatim = verbatim[5:]
                result.append(len(chunk))  # 1-5
                result.extend(chunk)

        while i < len(text):
            matched = False

            # Try word match (4+ chars)
            for word_len in range(min(16, len(text) - i), 3, -1):
                word = text_lower[i:i+word_len]
                if word in self.word_map:
                    flush_verbatim()
                    idx = self.word_map[word]
                    # Check for space before/after
                    space_before = i > 0 and text[i-1] == ' '
                    space_after = i + word_len < len(text) and text[i+word_len] == ' '

                    if space_before and not space_after:
                        result.append(8)  # space+word
                    elif space_after and not space_before:
                        result.append(7)  # word+space
                        i += 1  # consume trailing space
                    else:
                        result.append(6)  # plain word
                    result.append(idx)
                    i += word_len
                    matched = True
                    break

            if matched:
                continue

            # Try bigram match
            if i + 2 <= len(text):
                bg = text_lower[i:i+2]
                if bg in self.bigram_map:
                    flush_verbatim()
                    result.append(128 + self.bigram_map[bg])
                    i += 2
                    continue

            # Literal byte
            verbatim.append(ord(text[i]))
            i += 1

        flush_verbatim()
        return bytes(result)

    def get_ram_usage(self) -> int:
        return len(self.bigrams) + len(self.words) * 16 + 4096


class SMAZ2Extended:
    """Extended SMAZ2 with 256 bigrams and 512 words."""

    def __init__(self):
        # Extended bigrams (256)
        self.bigrams = self._build_extended_bigrams()
        # Extended words (512)
        self.words = SMAZ2_WORDS_512
        self._build_lookups()

    def _build_extended_bigrams(self) -> str:
        """Build extended bigram table."""
        # Start with original
        bigrams = list(SMAZ2_ORIGINAL_BIGRAMS[i:i+2]
                      for i in range(0, len(SMAZ2_ORIGINAL_BIGRAMS), 2))

        # Add URL/email patterns
        url_chars = "/:@.-_?=&#%+"
        for c1 in url_chars:
            for c2 in "aeioustrnlcdp":
                bg = c1 + c2
                if bg not in bigrams and len(bigrams) < 256:
                    bigrams.append(bg)

        # Add number patterns
        for i in range(10):
            for j in range(10):
                bg = str(i) + str(j)
                if bg not in bigrams and len(bigrams) < 256:
                    bigrams.append(bg)

        # Add common password bigrams
        password_bigrams = [
            "12", "23", "34", "45", "56", "67", "78", "89", "90", "01",
            "qw", "we", "er", "rt", "ty", "as", "sd", "df", "zx", "xc",
            "Aa", "Bb", "Cc", "Dd", "!@", "@#", "#$", "$%", "!1", "1!",
        ]
        for bg in password_bigrams:
            if bg.lower() not in [b.lower() for b in bigrams] and len(bigrams) < 256:
                bigrams.append(bg.lower())

        # Pad to 256 if needed
        while len(bigrams) < 256:
            bigrams.append("  ")

        return ''.join(bigrams[:256])

    def _build_lookups(self):
        self.bigram_map = {}
        for i in range(0, min(512, len(self.bigrams)), 2):
            bg = self.bigrams[i:i+2]
            if bg not in self.bigram_map:
                self.bigram_map[bg] = i // 2

        self.word_map = {w.lower(): i for i, w in enumerate(self.words)}

    def compress(self, text: str) -> bytes:
        """Compress using extended SMAZ2."""
        result = bytearray()
        verbatim = bytearray()
        i = 0
        text_lower = text.lower()

        def flush_verbatim():
            nonlocal verbatim
            while len(verbatim) > 0:
                chunk = verbatim[:5]
                verbatim = verbatim[5:]
                result.append(len(chunk))
                result.extend(chunk)

        while i < len(text):
            matched = False

            # Try word match (longer words = better compression)
            for word_len in range(min(20, len(text) - i), 3, -1):
                word = text_lower[i:i+word_len]
                if word in self.word_map:
                    flush_verbatim()
                    idx = self.word_map[word]

                    if idx < 256:
                        # Original encoding
                        result.append(6)
                        result.append(idx)
                    else:
                        # Extended word (2 bytes for index)
                        result.append(9)  # Extended word marker
                        result.append((idx >> 8) & 0xFF)
                        result.append(idx & 0xFF)

                    i += word_len
                    matched = True
                    break

            if matched:
                continue

            # Try bigram match
            if i + 2 <= len(text):
                bg = text_lower[i:i+2]
                if bg in self.bigram_map:
                    idx = self.bigram_map[bg]
                    if idx < 128:
                        flush_verbatim()
                        result.append(128 + idx)
                        i += 2
                        continue

            # Literal
            verbatim.append(ord(text[i]))
            i += 1

        flush_verbatim()
        return bytes(result)

    def get_ram_usage(self) -> int:
        return 512 + len(self.words) * 16 + 8192


class SMAZ2WithPhrases:
    """
    SMAZ2 extended with phrase learning.
    Combines static SMAZ2 tables with dynamic phrase dictionary.
    """

    def __init__(self, max_phrases: int = 256):
        self.smaz2 = SMAZ2Extended()
        self.max_phrases = max_phrases
        self.phrases: Dict[str, int] = {}
        self.phrase_reverse: Dict[int, str] = {}
        self.next_phrase = 0
        self._preseed_phrases()

    def _preseed_phrases(self):
        """Pre-seed with common keystroke phrases."""
        common_phrases = [
            # Greetings
            "Hello World", "Good morning", "Good afternoon", "Good evening",
            "How are you", "Thank you", "Thanks", "Please", "Sorry",

            # URLs
            "https://", "http://", "www.", ".com", ".org", ".net",
            "https://www.", "http://www.",
            "@gmail.com", "@yahoo.com", "@hotmail.com", "@outlook.com",

            # Login
            "username", "password", "email", "login", "sign in", "sign up",

            # Common messages
            "On my way", "Be there soon", "See you later", "Call me",
            "Let me know", "Sounds good", "No problem", "Got it",

            # Status
            "Status OK", "Error", "Success", "Failed", "Complete", "Loading",
        ]

        for phrase in common_phrases:
            if self.next_phrase < self.max_phrases:
                self.phrases[phrase.lower()] = self.next_phrase
                self.phrase_reverse[self.next_phrase] = phrase
                self.next_phrase += 1

    def _learn_phrase(self, phrase: str):
        """Learn a new phrase."""
        phrase_lower = phrase.lower()
        if phrase_lower in self.phrases:
            return

        if len(phrase) < 4:  # Don't learn short phrases
            return

        if self.next_phrase >= self.max_phrases:
            # Could implement LRU eviction here
            return

        self.phrases[phrase_lower] = self.next_phrase
        self.phrase_reverse[self.next_phrase] = phrase
        self.next_phrase += 1

    def compress(self, text: str) -> bytes:
        """Compress with phrase lookup + SMAZ2 fallback."""
        text_lower = text.lower()

        # Try exact phrase match first
        if text_lower in self.phrases:
            idx = self.phrases[text_lower]
            if idx < 64:
                return bytes([0xC0 | idx])  # 1 byte for phrase
            else:
                return bytes([0xFD, (idx >> 8) & 0xFF, idx & 0xFF])

        # Learn this phrase for next time
        self._learn_phrase(text)

        # Fall back to SMAZ2
        smaz_result = self.smaz2.compress(text)
        return bytes([0xFE]) + smaz_result  # Marker + SMAZ2 data

    def reset(self):
        self.phrases.clear()
        self.phrase_reverse.clear()
        self.next_phrase = 0
        self._preseed_phrases()

    def get_ram_usage(self) -> int:
        return self.smaz2.get_ram_usage() + self.max_phrases * 64


# =============================================================================
# TEST DATA GENERATION
# =============================================================================

USERNAMES = [
    "john.doe", "jane_smith", "admin", "user123", "testuser",
    "mike.wilson", "sarah.jones", "dev_team", "support", "guest",
]

PASSWORDS = [
    "Password123!", "Qwerty2024", "Welcome1!", "Admin@123",
    "Summer2024!", "Winter2023#", "MyPass123", "Secret99!",
]

URLS = [
    "https://www.google.com/search?q=test",
    "https://github.com/antirez/smaz2",
    "https://stackoverflow.com/questions/12345",
    "https://mail.google.com/mail/inbox",
    "https://www.amazon.com/dp/B08N5WRWNW",
]

EMAILS = [
    "john.doe@gmail.com", "jane@yahoo.com", "admin@company.com",
    "support@example.org", "test@hotmail.com",
]

MESSAGES = [
    "Hello World", "How are you?", "Good morning!", "Thanks for your help",
    "See you tomorrow", "On my way", "Be there soon", "Sounds good",
    "Let me check", "I'll send it", "Got it, thanks!", "No problem",
]


def generate_keystroke_data(count: int, repetition: float = 0.3) -> List[Tuple[str, int]]:
    """Generate realistic keystroke data."""
    data = []
    recent = []

    categories = [
        (USERNAMES, 0.15),
        (PASSWORDS, 0.15),
        (URLS, 0.15),
        (EMAILS, 0.15),
        (MESSAGES, 0.40),
    ]

    for _ in range(count):
        if recent and random.random() < repetition:
            text = random.choice(recent)
        else:
            # Pick category
            r = random.random()
            cumulative = 0
            for items, weight in categories:
                cumulative += weight
                if r < cumulative:
                    text = random.choice(items)
                    break

            recent.append(text)
            if len(recent) > 15:
                recent.pop(0)

        delta = random.randint(50, 500) if random.random() < 0.7 else random.randint(500, 3000)
        data.append((text, delta))

    return data


# =============================================================================
# PACKET BUFFER
# =============================================================================

MAX_PACKET_PAYLOAD = 190
HEADER_SIZE = 8
MAX_DATA_SIZE = MAX_PACKET_PAYLOAD - HEADER_SIZE


def encode_varint(value: int) -> bytes:
    result = bytearray()
    while value > 127:
        result.append((value & 0x7F) | 0x80)
        value >>= 7
    result.append(value)
    return bytes(result)


@dataclass
class Record:
    delta: int
    text: str
    compressed_size: int


class PacketBuffer:
    def __init__(self, compressor, max_data: int = MAX_DATA_SIZE):
        self.compressor = compressor
        self.max_data = max_data
        self.buffer = bytearray()
        self.records: List[Record] = []
        self.raw_bytes = 0

    def try_add(self, text: str, delta: int) -> bool:
        compressed = self.compressor.compress(text)
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


# =============================================================================
# BENCHMARK
# =============================================================================

def benchmark_compressor(name: str, compressor, data: List[Tuple[str, int]]) -> dict:
    """Benchmark a compressor."""
    if hasattr(compressor, 'reset'):
        compressor.reset()

    packets = []
    current = PacketBuffer(compressor)

    for text, delta in data:
        if not current.try_add(text, delta):
            packets.append(current)
            current = PacketBuffer(compressor)
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
        'ram_kb': compressor.get_ram_usage() / 1024 if hasattr(compressor, 'get_ram_usage') else 0,
    }


class BaselineCompressor:
    def compress(self, text: str) -> bytes:
        return text.encode('utf-8')
    def get_ram_usage(self) -> int:
        return 0


def run_smaz2_benchmark():
    """Run SMAZ2 comparison benchmark."""
    print("\n" + "=" * 85)
    print(" SMAZ2 IMPROVED BENCHMARK")
    print(" Comparing: Original vs Extended vs With Phrases")
    print("=" * 85)

    random.seed(42)
    data = generate_keystroke_data(1000, repetition=0.3)

    # Show sample
    print("\n SAMPLE DATA:")
    for i, (text, delta) in enumerate(data[:8]):
        print(f"  {i+1}. [{delta}ms] {text[:50]}")

    compressors = [
        ("baseline", BaselineCompressor()),
        ("smaz2_original", SMAZ2Original()),
        ("smaz2_extended", SMAZ2Extended()),
        ("smaz2+phrase_128", SMAZ2WithPhrases(max_phrases=128)),
        ("smaz2+phrase_256", SMAZ2WithPhrases(max_phrases=256)),
        ("smaz2+phrase_512", SMAZ2WithPhrases(max_phrases=512)),
    ]

    results = []
    for name, comp in compressors:
        result = benchmark_compressor(name, comp, data)
        results.append(result)

    results.sort(key=lambda x: x['packets'])
    baseline_packets = next(r['packets'] for r in results if r['name'] == 'baseline')

    print(f"\n{'Compressor':<20} {'RAM(KB)':<10} {'Packets':<10} {'Rec/Pkt':<10} {'Ratio':<8} {'Reduction'}")
    print("-" * 85)

    for r in results:
        reduction = ((baseline_packets - r['packets']) / baseline_packets) * 100
        print(f"{r['name']:<20} {r['ram_kb']:<10.1f} {r['packets']:<10} "
              f"{r['avg_records_per_packet']:<10.1f} {r['ratio']:<8.2f} {reduction:>+.1f}%")

    best = results[0]
    print("\n" + "=" * 85)
    print(f" BEST: {best['name']}")
    print(f" Packets: {best['packets']} (vs {baseline_packets} baseline)")
    print(f" Reduction: {((baseline_packets - best['packets']) / baseline_packets) * 100:.1f}%")
    print("=" * 85)

    return results


def run_individual_tests():
    """Test compression on individual strings."""
    print("\n" + "=" * 85)
    print(" INDIVIDUAL STRING COMPRESSION TEST")
    print("=" * 85)

    test_strings = [
        "Hello World",
        "Password123!",
        "https://www.google.com",
        "john.doe@gmail.com",
        "How are you?",
        "admin",
        "12345",
        "Good morning!",
    ]

    compressors = [
        ("baseline", BaselineCompressor()),
        ("smaz2_orig", SMAZ2Original()),
        ("smaz2_ext", SMAZ2Extended()),
        ("smaz2+phrase", SMAZ2WithPhrases(max_phrases=256)),
    ]

    print(f"\n{'String':<30} ", end="")
    for name, _ in compressors:
        print(f"{name:<14}", end="")
    print()
    print("-" * 90)

    for s in test_strings:
        print(f"{s:<30} ", end="")
        for name, comp in compressors:
            compressed = comp.compress(s)
            ratio = len(compressed) / len(s.encode('utf-8'))
            print(f"{len(compressed):>3}b ({ratio:.0%}){'':<4}", end="")
        print()


def run_repetition_test():
    """Test impact of repetition."""
    print("\n" + "=" * 85)
    print(" REPETITION IMPACT TEST")
    print("=" * 85)

    compressors = [
        ("baseline", BaselineCompressor()),
        ("smaz2_original", SMAZ2Original()),
        ("smaz2+phrase_256", SMAZ2WithPhrases(max_phrases=256)),
    ]

    print(f"\n{'Rep%':<8}", end="")
    for name, _ in compressors:
        print(f"{name:<18}", end="")
    print()
    print("-" * 60)

    for rep in [0.0, 0.2, 0.4, 0.6, 0.8]:
        random.seed(42)
        data = generate_keystroke_data(500, repetition=rep)

        print(f"{rep*100:.0f}%{'':<6}", end="")
        for name, comp in compressors:
            if hasattr(comp, 'reset'):
                comp.reset()
            result = benchmark_compressor(name, comp, data)
            print(f"{result['packets']:<18}", end="")
        print()


def print_implementation_guide():
    """Print C implementation guide."""
    print("\n" + "=" * 85)
    print(" C IMPLEMENTATION GUIDE")
    print("=" * 85)
    print("""
┌──────────────────────────────────────────────────────────────────────────────────┐
│                    SMAZ2 IMPROVED - C IMPLEMENTATION                             │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                   │
│  MEMORY LAYOUT:                                                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Component              │ Size      │ Description                            │ │
│  ├─────────────────────────────────────────────────────────────────────────────┤ │
│  │ Bigrams (256)          │ 512 B     │ 2-char sequences                       │ │
│  │ Words (512)            │ 8 KB      │ Common words (avg 16 bytes)            │ │
│  │ Phrases (256)          │ 16 KB     │ Learned phrases (64 bytes each)        │ │
│  │ Hash tables            │ 2 KB      │ Fast lookups                           │ │
│  │ Total                  │ ~27 KB    │ 5.2% of RP2350 SRAM                    │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                   │
│  ENCODING SCHEME:                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────────┐ │
│  │ Byte Value    │ Meaning                                                     │ │
│  ├─────────────────────────────────────────────────────────────────────────────┤ │
│  │ 0x01-0x05     │ Verbatim sequence (1-5 bytes follow)                        │ │
│  │ 0x06          │ Word token (1 byte index follows)                           │ │
│  │ 0x07          │ Word + trailing space                                       │ │
│  │ 0x08          │ Space + word                                                │ │
│  │ 0x09          │ Extended word (2 byte index follows)                        │ │
│  │ 0x20-0x7F     │ ASCII literal                                               │ │
│  │ 0x80-0xFF     │ Bigram index (subtract 0x80)                                │ │
│  │ 0xC0-0xFF     │ Phrase token (if phrase dictionary enabled)                 │ │
│  │ 0xFD          │ Extended phrase (2 byte index follows)                      │ │
│  │ 0xFE          │ SMAZ2 fallback marker                                       │ │
│  └─────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                   │
│  COMPRESSION FUNCTION:                                                            │
│                                                                                   │
│  size_t smaz2_improved_compress(const char* in, size_t inlen,                    │
│                                  uint8_t* out, size_t outlen) {                  │
│      size_t i = 0, o = 0;                                                        │
│                                                                                   │
│      // 1. Try phrase match first (best compression)                             │
│      int phrase_idx = phrase_lookup(in, inlen);                                  │
│      if (phrase_idx >= 0) {                                                      │
│          if (phrase_idx < 64) {                                                  │
│              out[o++] = 0xC0 | phrase_idx;  // 1 byte!                           │
│              return o;                                                           │
│          }                                                                       │
│      }                                                                           │
│                                                                                   │
│      // 2. Standard SMAZ2 compression                                            │
│      while (i < inlen) {                                                         │
│          // Try word match                                                       │
│          int word_idx = word_lookup(in + i, inlen - i);                          │
│          if (word_idx >= 0) {                                                    │
│              out[o++] = 0x06;                                                    │
│              out[o++] = word_idx;                                                │
│              i += word_len[word_idx];                                            │
│              continue;                                                           │
│          }                                                                       │
│                                                                                   │
│          // Try bigram match                                                     │
│          if (i + 2 <= inlen) {                                                   │
│              int bg_idx = bigram_lookup(in[i], in[i+1]);                         │
│              if (bg_idx >= 0) {                                                  │
│                  out[o++] = 0x80 + bg_idx;                                       │
│                  i += 2;                                                         │
│                  continue;                                                       │
│              }                                                                   │
│          }                                                                       │
│                                                                                   │
│          // Literal byte                                                         │
│          out[o++] = in[i++];                                                     │
│      }                                                                           │
│                                                                                   │
│      // 3. Learn phrase for next time                                            │
│      phrase_learn(in, inlen);                                                    │
│                                                                                   │
│      return o;                                                                   │
│  }                                                                               │
│                                                                                   │
└──────────────────────────────────────────────────────────────────────────────────┘
""")


if __name__ == "__main__":
    run_smaz2_benchmark()
    run_individual_tests()
    run_repetition_test()
    print_implementation_guide()
