#!/usr/bin/env python3
"""
COLD START COMPRESSION BENCHMARK

Reality check: We don't know what the user will type.
Phrase learning only helps on REPEAT occurrences.

This benchmark tests:
1. 0% repetition (worst case - every entry is unique)
2. Static compression only (bigrams + words)
3. Trigrams for better static compression
4. Expanded word dictionaries

Goal: Find the best compression WITHOUT relying on learned phrases.
"""

import random
import string
from typing import List, Tuple, Dict, Set
from dataclasses import dataclass


# =============================================================================
# Constants
# =============================================================================

MAX_PACKET_PAYLOAD = 190
HEADER_SIZE = 8
MAX_DATA_SIZE = MAX_PACKET_PAYLOAD - HEADER_SIZE


# =============================================================================
# SMAZ2 ORIGINAL CODEBOOK
# =============================================================================

SMAZ2_BIGRAMS = (
    "intherreheanonesorteattistenntartondalitseediseangoulecomenerirode"
    "raioicliofasetvetasihamaecomceelllcaurlachhidihofonsotacnarssoprrts"
    "assusnoiltsemctgeloeebetrnipeiepancpooldaadviunamutwimoshyoaiewowos"
    "fiepttmiopiaweagsuiddoooirspplscaywaigeirylytuulivimabty"
)

SMAZ2_WORDS = [
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
# KEYSTROKE-SPECIFIC VOCABULARY
# =============================================================================

KEYSTROKE_WORDS = [
    # Login/Auth
    "username", "password", "login", "logout", "signin", "signup", "register",
    "forgot", "reset", "remember", "verify", "confirm", "submit", "captcha",
    "admin", "root", "guest", "test", "demo", "administrator",

    # URLs
    "https", "http", "www", "html", "php", "asp", "api", "json",
    "google", "facebook", "twitter", "instagram", "linkedin", "youtube",
    "amazon", "github", "stackoverflow", "reddit", "wikipedia", "netflix",

    # Email
    "gmail", "yahoo", "hotmail", "outlook", "icloud", "protonmail",
    "inbox", "sent", "draft", "spam", "compose", "reply", "forward",

    # Form fields
    "firstname", "lastname", "fullname", "nickname", "phone", "mobile",
    "address", "street", "city", "state", "country", "zipcode", "postal",

    # Payment
    "credit", "debit", "visa", "mastercard", "paypal", "checkout", "cart",
    "cvv", "expiry", "billing", "shipping",

    # Common words in typing
    "hello", "thanks", "thank", "please", "sorry", "okay", "yes", "no",
    "maybe", "sure", "great", "good", "nice", "cool", "awesome",

    # Status
    "error", "success", "failed", "loading", "pending", "complete",
    "active", "inactive", "enabled", "disabled", "online", "offline",

    # Time
    "today", "tomorrow", "yesterday", "morning", "afternoon", "evening",
    "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday",
    "january", "february", "march", "april", "june", "july", "august",
    "september", "october", "november", "december",
]

# Domains and TLDs
DOMAINS_TLDS = [
    ".com", ".org", ".net", ".edu", ".gov", ".io", ".co", ".me", ".app",
    "www.", "mail.", "api.", "dev.", "app.", "m.",
    "@gmail.com", "@yahoo.com", "@hotmail.com", "@outlook.com",
]

# Common URL patterns
URL_PATTERNS = [
    "https://", "http://", "https://www.", "http://www.",
    "/login", "/signin", "/signup", "/register", "/forgot",
    "/search", "/account", "/profile", "/settings", "/dashboard",
]

# Password patterns (static parts)
PASSWORD_PARTS = [
    "pass", "word", "123", "1234", "12345", "abc", "qwerty",
    "admin", "user", "test", "welcome", "hello", "secret",
]


# =============================================================================
# TRIGRAMS (3-char sequences that save 2 bytes each)
# =============================================================================

TRIGRAMS = [
    # Common English
    "the", "and", "ing", "ion", "tio", "ent", "ati", "for", "her", "ter",
    "hat", "tha", "ere", "ate", "his", "con", "res", "ver", "all", "ons",
    "nce", "men", "ith", "ted", "ers", "pro", "thi", "wit", "are", "ess",

    # URL trigrams
    "www", "com", "org", "net", "edu", "gov", "htm", "php", "asp", "api",
    "http", "mail", "user", "pass", "log",

    # Number sequences
    "123", "234", "345", "456", "567", "678", "789", "012", "000", "111",

    # Keyboard patterns
    "qwe", "wer", "ert", "rty", "asd", "sdf", "dfg", "zxc", "xcv", "cvb",
]


# =============================================================================
# COMPRESSOR IMPLEMENTATIONS
# =============================================================================

class BaselineCompressor:
    """No compression."""
    def compress(self, text: str) -> bytes:
        return text.encode('utf-8')
    def get_ram_usage(self) -> int:
        return 0
    def get_name(self) -> str:
        return "baseline"


class SMAZ2Original:
    """Original SMAZ2 with 128 bigrams, 256 words."""

    def __init__(self):
        self.bigram_map = {}
        for i in range(0, len(SMAZ2_BIGRAMS), 2):
            bg = SMAZ2_BIGRAMS[i:i+2]
            self.bigram_map[bg] = i // 2
        self.word_map = {w: i for i, w in enumerate(SMAZ2_WORDS)}

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
            # Try word
            matched = False
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
        return len(SMAZ2_BIGRAMS) + len(SMAZ2_WORDS) * 16

    def get_name(self) -> str:
        return "smaz2_original"


class SMAZ2Extended:
    """SMAZ2 with extended bigrams and keystroke words."""

    def __init__(self, extra_bigrams: str = "", extra_words: List[str] = None):
        # Build bigram map
        self.bigram_map = {}
        all_bigrams = SMAZ2_BIGRAMS + extra_bigrams
        for i in range(0, min(256, len(all_bigrams)), 2):
            bg = all_bigrams[i:i+2].lower()
            if bg not in self.bigram_map:
                self.bigram_map[bg] = len(self.bigram_map)

        # Build word map
        all_words = SMAZ2_WORDS + (extra_words or [])
        self.word_map = {w.lower(): i for i, w in enumerate(all_words[:512])}
        self.words = all_words[:512]

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

            # Try word (longest match first)
            for wlen in range(min(20, len(text) - i), 3, -1):
                word = text_lower[i:i+wlen]
                if word in self.word_map:
                    flush()
                    idx = self.word_map[word]
                    if idx < 256:
                        result.append(6)
                        result.append(idx)
                    else:
                        result.append(9)  # Extended word marker
                        result.append(idx - 256)
                    i += wlen
                    matched = True
                    break

            if matched:
                continue

            # Try bigram
            if i + 2 <= len(text):
                bg = text_lower[i:i+2]
                if bg in self.bigram_map and self.bigram_map[bg] < 128:
                    flush()
                    result.append(128 + self.bigram_map[bg])
                    i += 2
                    continue

            verbatim.append(ord(text[i]))
            i += 1

        flush()
        return bytes(result)

    def get_ram_usage(self) -> int:
        return len(self.bigram_map) * 2 + len(self.words) * 16

    def get_name(self) -> str:
        return "smaz2_extended"


class TrigramCompressor:
    """Compressor with trigram support (saves 2 bytes per match)."""

    def __init__(self, trigrams: List[str], bigrams: str, words: List[str]):
        self.trigram_map = {t.lower(): i for i, t in enumerate(trigrams[:64])}

        self.bigram_map = {}
        for i in range(0, min(256, len(bigrams)), 2):
            bg = bigrams[i:i+2].lower()
            if bg not in self.bigram_map:
                self.bigram_map[bg] = len(self.bigram_map)

        self.word_map = {w.lower(): i for i, w in enumerate(words[:512])}
        self.words = words[:512]
        self.trigrams = trigrams[:64]

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

            # Try word (longest first)
            for wlen in range(min(20, len(text) - i), 3, -1):
                word = text_lower[i:i+wlen]
                if word in self.word_map:
                    flush()
                    idx = self.word_map[word]
                    if idx < 256:
                        result.append(6)
                        result.append(idx)
                    else:
                        result.append(9)
                        result.append(idx - 256)
                    i += wlen
                    matched = True
                    break

            if matched:
                continue

            # Try trigram (saves 2 bytes: 3 chars -> 1 byte)
            if i + 3 <= len(text):
                tri = text_lower[i:i+3]
                if tri in self.trigram_map:
                    flush()
                    result.append(0xC0 + self.trigram_map[tri])  # Trigram marker
                    i += 3
                    continue

            # Try bigram
            if i + 2 <= len(text):
                bg = text_lower[i:i+2]
                if bg in self.bigram_map and self.bigram_map[bg] < 128:
                    flush()
                    result.append(128 + self.bigram_map[bg])
                    i += 2
                    continue

            verbatim.append(ord(text[i]))
            i += 1

        flush()
        return bytes(result)

    def get_ram_usage(self) -> int:
        return len(self.trigrams) * 3 + len(self.bigram_map) * 2 + len(self.words) * 16

    def get_name(self) -> str:
        return "trigram_compressor"


# =============================================================================
# TEST DATA - UNIQUE ENTRIES (NO REPETITION)
# =============================================================================

def generate_unique_usernames(count: int) -> List[str]:
    """Generate unique usernames."""
    prefixes = ["john", "jane", "mike", "sarah", "david", "emma", "chris", "lisa"]
    suffixes = ["_dev", "_admin", "_test", "123", "2024", "_user", "_work", ""]
    separators = [".", "_", ""]

    usernames = set()
    while len(usernames) < count:
        p = random.choice(prefixes)
        s = random.choice(suffixes)
        sep = random.choice(separators)
        num = random.randint(1, 999) if random.random() < 0.5 else ""
        username = f"{p}{sep}{s}{num}"
        usernames.add(username)

    return list(usernames)[:count]


def generate_unique_passwords(count: int) -> List[str]:
    """Generate unique passwords."""
    words = ["Password", "Welcome", "Summer", "Winter", "Hello", "Secret", "Admin", "User"]
    numbers = ["123", "1234", "2024", "99", "01", "007", "42"]
    symbols = ["!", "@", "#", "$", "!!", ""]

    passwords = set()
    while len(passwords) < count:
        w = random.choice(words)
        n = random.choice(numbers)
        s = random.choice(symbols)
        cap = random.choice([str.upper, str.lower, str.capitalize])
        password = cap(w) + n + s
        passwords.add(password)

    return list(passwords)[:count]


def generate_unique_urls(count: int) -> List[str]:
    """Generate unique URLs."""
    protocols = ["https://", "http://"]
    domains = ["google.com", "github.com", "stackoverflow.com", "amazon.com",
               "facebook.com", "twitter.com", "linkedin.com", "reddit.com"]
    paths = ["/search", "/login", "/user/", "/api/", "/products/", "/account/", ""]

    urls = set()
    while len(urls) < count:
        proto = random.choice(protocols)
        www = "www." if random.random() < 0.5 else ""
        domain = random.choice(domains)
        path = random.choice(paths)
        param = f"?id={random.randint(1000, 9999)}" if random.random() < 0.3 else ""
        url = f"{proto}{www}{domain}{path}{param}"
        urls.add(url)

    return list(urls)[:count]


def generate_unique_emails(count: int) -> List[str]:
    """Generate unique email addresses."""
    names = ["john", "jane", "mike", "sarah", "david", "emma", "chris", "admin", "info"]
    domains = ["gmail.com", "yahoo.com", "hotmail.com", "outlook.com", "company.com"]

    emails = set()
    while len(emails) < count:
        name = random.choice(names)
        num = random.randint(1, 999) if random.random() < 0.5 else ""
        domain = random.choice(domains)
        email = f"{name}{num}@{domain}"
        emails.add(email)

    return list(emails)[:count]


def generate_unique_messages(count: int) -> List[str]:
    """Generate unique chat messages."""
    templates = [
        "Meeting at {}pm tomorrow",
        "Can you send me the {} file?",
        "I'll be there in {} minutes",
        "Please review the {} document",
        "Thanks for the {} update",
        "Let me check with {} first",
        "The {} is ready for review",
        "Don't forget about {}",
    ]

    fillers = ["report", "project", "budget", "schedule", "team", "client", "data"]

    messages = set()
    while len(messages) < count:
        template = random.choice(templates)
        filler = random.choice(fillers) if "{}" in template else str(random.randint(1, 12))
        messages.add(template.format(filler))

    return list(messages)[:count]


def generate_cold_start_data(count: int) -> List[Tuple[str, int]]:
    """Generate data with 0% repetition - every entry is unique."""
    # Generate pools of unique entries
    usernames = generate_unique_usernames(count // 5)
    passwords = generate_unique_passwords(count // 5)
    urls = generate_unique_urls(count // 5)
    emails = generate_unique_emails(count // 5)
    messages = generate_unique_messages(count // 5)

    # Combine and shuffle
    all_entries = usernames + passwords + urls + emails + messages
    random.shuffle(all_entries)

    data = []
    for text in all_entries[:count]:
        delta = random.randint(100, 1000)
        data.append((text, delta))

    return data


# =============================================================================
# BENCHMARK
# =============================================================================

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
        self.records = []
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


def benchmark(compressor, data: List[Tuple[str, int]]) -> dict:
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

    return {
        'name': compressor.get_name(),
        'packets': len(packets),
        'records': total_records,
        'raw_bytes': total_raw,
        'compressed_bytes': total_compressed,
        'ratio': total_compressed / total_raw if total_raw else 1,
        'avg_records': total_records / len(packets) if packets else 0,
        'ram_kb': compressor.get_ram_usage() / 1024,
    }


def run_cold_start_benchmark():
    """Benchmark with 0% repetition."""
    print("\n" + "=" * 85)
    print(" COLD START BENCHMARK (0% Repetition)")
    print(" Every entry is UNIQUE - no phrase learning benefit")
    print("=" * 85)

    random.seed(42)
    data = generate_cold_start_data(500)

    print("\n SAMPLE DATA (all unique):")
    for i, (text, delta) in enumerate(data[:8]):
        print(f"  {i+1}. {text}")

    # Build extended bigrams
    extra_bigrams = ""
    for i in range(10):
        for j in range(10):
            extra_bigrams += f"{i}{j}"
    # Add URL patterns
    for c in "/:@.-_?=&#":
        for l in "abcdefghijklmnop":
            extra_bigrams += c + l

    compressors = [
        BaselineCompressor(),
        SMAZ2Original(),
        SMAZ2Extended(extra_bigrams, KEYSTROKE_WORDS),
        TrigramCompressor(TRIGRAMS, SMAZ2_BIGRAMS + extra_bigrams,
                         SMAZ2_WORDS + KEYSTROKE_WORDS),
    ]

    results = []
    for comp in compressors:
        result = benchmark(comp, data)
        results.append(result)

    baseline_packets = results[0]['packets']

    print(f"\n{'Compressor':<20} {'RAM(KB)':<10} {'Packets':<10} {'Ratio':<10} {'Reduction'}")
    print("-" * 70)

    for r in results:
        reduction = ((baseline_packets - r['packets']) / baseline_packets) * 100
        print(f"{r['name']:<20} {r['ram_kb']:<10.1f} {r['packets']:<10} "
              f"{r['ratio']:<10.2f} {reduction:>+.1f}%")

    return results


def run_individual_compression_test():
    """Test individual strings."""
    print("\n" + "=" * 85)
    print(" INDIVIDUAL STRING COMPRESSION (Static dictionaries only)")
    print("=" * 85)

    test_strings = [
        ("Username", "john_doe_2024"),
        ("Password", "Welcome123!"),
        ("URL", "https://www.google.com/search"),
        ("Email", "john.doe@gmail.com"),
        ("Message", "Meeting at 3pm tomorrow"),
        ("Numbers", "1234567890"),
    ]

    extra_bigrams = ""
    for i in range(10):
        for j in range(10):
            extra_bigrams += f"{i}{j}"

    compressors = [
        ("baseline", BaselineCompressor()),
        ("smaz2_orig", SMAZ2Original()),
        ("smaz2_ext", SMAZ2Extended(extra_bigrams, KEYSTROKE_WORDS)),
        ("trigram", TrigramCompressor(TRIGRAMS, SMAZ2_BIGRAMS + extra_bigrams,
                                      SMAZ2_WORDS + KEYSTROKE_WORDS)),
    ]

    print(f"\n{'Type':<12} {'String':<30}", end="")
    for name, _ in compressors:
        print(f"{name:<14}", end="")
    print()
    print("-" * 90)

    for type_name, s in test_strings:
        raw_len = len(s.encode('utf-8'))
        print(f"{type_name:<12} {s:<30}", end="")
        for name, comp in compressors:
            compressed = comp.compress(s)
            savings = (1 - len(compressed) / raw_len) * 100
            print(f"{len(compressed):>2}b ({savings:>+3.0f}%)   ", end="")
        print()


def run_data_type_breakdown():
    """Test each data type separately."""
    print("\n" + "=" * 85)
    print(" DATA TYPE BREAKDOWN (Cold Start)")
    print("=" * 85)

    extra_bigrams = ""
    for i in range(10):
        for j in range(10):
            extra_bigrams += f"{i}{j}"

    compressor = TrigramCompressor(TRIGRAMS, SMAZ2_BIGRAMS + extra_bigrams,
                                   SMAZ2_WORDS + KEYSTROKE_WORDS)
    baseline = BaselineCompressor()

    data_types = [
        ("Usernames", generate_unique_usernames(100)),
        ("Passwords", generate_unique_passwords(100)),
        ("URLs", generate_unique_urls(100)),
        ("Emails", generate_unique_emails(100)),
        ("Messages", generate_unique_messages(100)),
    ]

    print(f"\n{'Data Type':<15} {'Avg Raw':<12} {'Avg Comp':<12} {'Savings':<12} {'Best For'}")
    print("-" * 70)

    for type_name, items in data_types:
        total_raw = sum(len(s.encode('utf-8')) for s in items)
        total_comp = sum(len(compressor.compress(s)) for s in items)
        avg_raw = total_raw / len(items)
        avg_comp = total_comp / len(items)
        savings = (1 - total_comp / total_raw) * 100

        best = "words" if type_name == "Messages" else "bigrams" if savings > 10 else "limited"
        print(f"{type_name:<15} {avg_raw:<12.1f} {avg_comp:<12.1f} {savings:>+5.1f}%{'':<5} {best}")


def print_recommendations():
    """Print recommendations for cold start scenario."""
    print("\n" + "=" * 85)
    print(" RECOMMENDATIONS FOR COLD START (Unknown User Input)")
    print("=" * 85)
    print("""
┌──────────────────────────────────────────────────────────────────────────────────────┐
│                    STATIC COMPRESSION STRATEGIES                                      │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                       │
│  PROBLEM: Phrase learning only helps on REPEAT occurrences.                          │
│           First occurrence = 0% compression from phrases.                            │
│                                                                                       │
│  SOLUTION: Maximize STATIC compression that works immediately.                        │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐ │
│  │ Component          │ Matches           │ Savings       │ Priority              │ │
│  ├─────────────────────────────────────────────────────────────────────────────────┤ │
│  │ Word dictionary    │ "password" → 2B   │ 6 bytes saved │ HIGH (best ROI)       │ │
│  │ Trigrams           │ "the" → 1B        │ 2 bytes saved │ MEDIUM                │ │
│  │ Bigrams            │ "th" → 1B         │ 1 byte saved  │ LOW (diminishing)     │ │
│  └─────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                       │
│  RECOMMENDED WORD DICTIONARY (512 entries):                                           │
│  - Original SMAZ2 words (256) - common English                                        │
│  - Keystroke words (128) - login, password, username, email, etc.                    │
│  - URL components (64) - https, www, com, org, gmail, google, etc.                   │
│  - Domain names (64) - google, facebook, amazon, github, etc.                        │
│                                                                                       │
│  RECOMMENDED TRIGRAMS (64 entries):                                                   │
│  - English: the, and, ing, ion, for, her, ter, hat, tha, ere                         │
│  - URL: www, com, org, net, http                                                      │
│  - Numbers: 123, 234, 000, 111                                                        │
│  - Keyboard: qwe, asd, zxc                                                            │
│                                                                                       │
│  MEMORY BUDGET:                                                                       │
│  - Words (512 × 16 bytes avg) = 8 KB                                                 │
│  - Trigrams (64 × 3 bytes) = 192 bytes                                               │
│  - Bigrams (128 × 2 bytes) = 256 bytes                                               │
│  - Hash tables = 4 KB                                                                │
│  - Total: ~13 KB (2.5% of RP2350 SRAM)                                               │
│                                                                                       │
│  EXPECTED COMPRESSION (Cold Start):                                                   │
│  - Messages: 20-30% reduction (word matches)                                          │
│  - URLs: 10-15% reduction (component matches)                                         │
│  - Emails: 15-20% reduction (domain matches)                                          │
│  - Passwords: 5-10% reduction (limited matches)                                       │
│  - Overall: 15-20% packet reduction                                                   │
│                                                                                       │
│  PHRASE LEARNING STILL HELPS:                                                         │
│  - Keep phrase dictionary for repeated entries                                        │
│  - 2nd occurrence onwards benefits from learning                                      │
│  - Real users DO repeat phrases (greetings, responses)                               │
│                                                                                       │
└──────────────────────────────────────────────────────────────────────────────────────┘
""")


if __name__ == "__main__":
    run_cold_start_benchmark()
    run_individual_compression_test()
    run_data_type_breakdown()
    print_recommendations()
