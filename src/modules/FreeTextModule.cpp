#include "configuration.h"
#if HAS_SCREEN
#include "Channels.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/emotes.h"
#include "input/SerialKeyboard.h"
#include "modules/CannedMessageModule.h"
#include "modules/FreeTextModule.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
#include "graphics/EInkDynamicDisplay.h"
#endif

static constexpr int32_t kInactivateAfterMs = 20000;

// Split a message into plain-text and emote-label tokens for mixed rendering.
std::vector<std::pair<bool, String>> freeTextModule::tokenizeMessageWithEmotes(const char *msg)
{
    std::vector<std::pair<bool, String>> tokens;
    int msgLen = strlen(msg);
    int pos = 0;
    while (pos < msgLen) {
        const graphics::Emote *foundEmote = nullptr;
        int foundLen = 0;
        for (int j = 0; j < graphics::numEmotes; j++) {
            const char *label = graphics::emotes[j].label;
            int labelLen = strlen(label);
            if (labelLen == 0) {
                continue;
            }
            if (strncmp(msg + pos, label, labelLen) == 0) {
                if (!foundEmote || labelLen > foundLen) {
                    foundEmote = &graphics::emotes[j];
                    foundLen = labelLen;
                }
            }
        }
        if (foundEmote) {
            tokens.emplace_back(true, String(foundEmote->label));
            pos += foundLen;
        } else {
            int nextEmote = msgLen;
            for (int j = 0; j < graphics::numEmotes; j++) {
                const char *label = graphics::emotes[j].label;
                if (!label || !*label) {
                    continue;
                }
                const char *found = strstr(msg + pos, label);
                if (found && (found - msg) < nextEmote) {
                    nextEmote = found - msg;
                }
            }
            int textLen = (nextEmote > pos) ? (nextEmote - pos) : (msgLen - pos);
            if (textLen > 0) {
                tokens.emplace_back(false, String(msg + pos).substring(0, textLen));
                pos += textLen;
            } else {
                break;
            }
        }
    }
    return tokens;
}

// Render one emote token inline and advance the drawing cursor.
void freeTextModule::renderEmote(OLEDDisplay *display, int &nextX, int lineY, int rowHeight, const String &label)
{
    const graphics::Emote *emote = nullptr;
    for (int j = 0; j < graphics::numEmotes; j++) {
        if (label == graphics::emotes[j].label) {
            emote = &graphics::emotes[j];
            break;
        }
    }
    if (emote) {
        int emoteYOffset = (rowHeight - emote->height) / 2;
        display->drawXbm(nextX, lineY + emoteYOffset, emote->width, emote->height, emote->bitmap);
        nextX += emote->width + 2;
    }
}

namespace
{
// ASCII-only lowercase helper to avoid locale-dependent behavior.
char toLowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c + ('a' - 'A'));
    }
    return c;
}

// Small built-in dictionary for lightweight freetext word completion.
// Keep this list compact to limit flash/RAM usage on constrained targets.
static const char *const kFreeTextCompletionWords[] = {
    "about",   "above",       "across",     "ack",      "after",    "again",    "ahead",     "all",         "alive",
    "already", "answer",      "anyone",     "arrived",  "asap",     "at",       "available", "back",        "base",
    "battery", "because",     "before",     "behind",   "below",    "between",  "busy",      "bye",         "call",
    "can",     "cancel",      "cannot",     "careful",  "channel",  "check",    "checkin",   "clear",       "close",
    "come",    "coming",      "confirmed",  "contact",  "continue", "copy",     "current",   "danger",      "data",
    "delayed", "destination", "direct",     "done",     "down",     "east",     "emergency", "enroute",     "ETA",
    "evening", "everyone",    "failed",     "feedback", "fine",     "for",      "from",      "friendly",    "going",
    "good",    "got",         "GPS",        "grid",     "group",    "have",     "he",        "hear",        "hello",
    "help",    "here",        "hey",        "high",     "hold",     "home",     "how",       "inside",      "later",
    "left",    "listen",      "location",   "lost",     "low",      "maintain", "meet",      "mesh",        "Meshtastic",
    "message", "morning",     "moving",     "near",     "nearby",   "need",     "negative",  "net",         "network",
    "night",   "node",        "none",       "north",    "nothing",  "now",      "offgrid",   "offline",     "okay",
    "online",  "open",        "out",        "outside",  "over",     "perfect",  "ping",      "pickup",      "please",
    "point",   "positive",    "position",   "power",    "priority", "proceed",  "quick",     "quiet",       "radio",
    "ready",   "reading",     "receive",    "received", "repeat",   "reply",    "request",   "resend",      "respond",
    "return",  "returning",   "right",      "roger",    "route",    "running",  "safe",      "safety",      "search",
    "secure",  "see",         "seen",       "send",     "signal",   "soon",     "south",     "standby",     "station",
    "status",  "still",       "stop",       "success",  "support",  "target",   "team",      "temperature", "test",
    "thank",   "thanks",      "that",       "the",      "there",    "these",    "this",      "towards",     "track",
    "traffic", "unable",      "understood", "update",   "urgent",   "vehicle",  "visual",    "wait",        "warning",
    "watch",   "weather",     "welcome",    "west",     "when",     "where",    "who",       "why",         "will",
    "with",    "work",        "yes",        "you",      "your"};

enum CompletionWordForm : uint8_t {
    COMPLETION_FORM_NONE = 0,
    COMPLETION_FORM_S = 1 << 0,
    // Past tense/participle form (regular "-ed" or irregular override).
    COMPLETION_FORM_ED = 1 << 1,
    COMPLETION_FORM_ING = 1 << 2,
};

struct CompletionInflectionStem {
    const char *stem;
    uint8_t forms;
};

// Explicit stems where we want generated inflected forms without duplicating dictionary entries.
static const CompletionInflectionStem kFreeTextInflectionStems[] = {
    // Regular inflections.
    {"ack", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"answer", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"arrive", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"call", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"cancel", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"check", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"clear", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"close", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"confirm", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"contact", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"continue", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"copy", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"delay", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"direct", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"do", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"help", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"listen", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"maintain", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"message", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"move", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"need", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"open", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"pickup", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"ping", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"point", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"position", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"power", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"proceed", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"receive", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"repeat", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"reply", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"request", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"respond", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"return", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"route", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"search", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"secure", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"signal", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"support", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"target", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"thank", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"test", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"track", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"update", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"wait", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"watch", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"welcome", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"work", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},

    // Irregular stems still using generated present/continuous and custom past forms.
    {"come", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"go", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"have", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"hear", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"hold", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"meet", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"read", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"resend", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"run", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"see", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"send", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
    {"stop", COMPLETION_FORM_S | COMPLETION_FORM_ED | COMPLETION_FORM_ING},
};

bool isAsciiVowel(char c)
{
    const char lower = toLowerAscii(c);
    return lower == 'a' || lower == 'e' || lower == 'i' || lower == 'o' || lower == 'u';
}

bool isAsciiConsonant(char c)
{
    const char lower = toLowerAscii(c);
    return (lower >= 'a' && lower <= 'z') && !isAsciiVowel(lower);
}

bool endsWithAscii(const char *word, const char *suffix)
{
    const size_t wordLen = strlen(word);
    const size_t suffixLen = strlen(suffix);
    if (suffixLen > wordLen) {
        return false;
    }
    return strncmp(word + (wordLen - suffixLen), suffix, suffixLen) == 0;
}

bool shouldDoubleFinalConsonant(const char *stem)
{
    const size_t len = strlen(stem);
    if (len < 3 || len > 4) {
        return false;
    }

    const char last = toLowerAscii(stem[len - 1]);
    if (last == 'w' || last == 'x' || last == 'y') {
        return false;
    }

    if (len == 3) {
        return isAsciiConsonant(stem[0]) && isAsciiVowel(stem[1]) && isAsciiConsonant(stem[2]);
    }

    // len == 4: approximate "CCVC" words like stop/chat/plan, but avoid "open".
    return isAsciiConsonant(stem[0]) && isAsciiConsonant(stem[1]) && isAsciiVowel(stem[2]) && isAsciiConsonant(stem[3]);
}

String buildSForm(const char *stem)
{
    if (strcmp(stem, "do") == 0) {
        return "does";
    }
    if (strcmp(stem, "have") == 0) {
        return "has";
    }

    const size_t len = strlen(stem);
    if (len == 0) {
        return "";
    }

    if (len >= 2) {
        const char last = toLowerAscii(stem[len - 1]);
        const char prev = toLowerAscii(stem[len - 2]);
        if (last == 'y' && !isAsciiVowel(prev)) {
            return String(stem).substring(0, len - 1) + "ies";
        }
        if (last == 's' || last == 'x' || last == 'z' || last == 'o') {
            return String(stem) + "es";
        }
    }

    if (endsWithAscii(stem, "ch") || endsWithAscii(stem, "sh")) {
        return String(stem) + "es";
    }

    return String(stem) + "s";
}

String buildEdForm(const char *stem)
{
    // Small irregular map for common command/messaging verbs.
    if (strcmp(stem, "come") == 0) {
        return "came";
    }
    if (strcmp(stem, "do") == 0) {
        return "did";
    }
    if (strcmp(stem, "go") == 0) {
        return "went";
    }
    if (strcmp(stem, "have") == 0) {
        return "had";
    }
    if (strcmp(stem, "hear") == 0) {
        return "heard";
    }
    if (strcmp(stem, "hold") == 0) {
        return "held";
    }
    if (strcmp(stem, "meet") == 0) {
        return "met";
    }
    if (strcmp(stem, "read") == 0) {
        return "read";
    }
    if (strcmp(stem, "resend") == 0) {
        return "resent";
    }
    if (strcmp(stem, "run") == 0) {
        return "ran";
    }
    if (strcmp(stem, "see") == 0) {
        return "saw";
    }
    if (strcmp(stem, "send") == 0) {
        return "sent";
    }

    const size_t len = strlen(stem);
    if (len == 0) {
        return "";
    }
    if (toLowerAscii(stem[len - 1]) == 'e') {
        return String(stem) + "d";
    }
    if (len >= 2 && toLowerAscii(stem[len - 1]) == 'y' && !isAsciiVowel(stem[len - 2])) {
        return String(stem).substring(0, len - 1) + "ied";
    }
    if (shouldDoubleFinalConsonant(stem)) {
        String out = stem;
        out += stem[len - 1];
        out += "ed";
        return out;
    }
    return String(stem) + "ed";
}

String buildIngForm(const char *stem)
{
    const size_t len = strlen(stem);
    if (len == 0) {
        return "";
    }
    if (len >= 2 && toLowerAscii(stem[len - 2]) == 'i' && toLowerAscii(stem[len - 1]) == 'e') {
        return String(stem).substring(0, len - 2) + "ying";
    }
    if (toLowerAscii(stem[len - 1]) == 'e' && !(len >= 2 && toLowerAscii(stem[len - 2]) == 'e')) {
        return String(stem).substring(0, len - 1) + "ing";
    }
    if (shouldDoubleFinalConsonant(stem)) {
        String out = stem;
        out += stem[len - 1];
        out += "ing";
        return out;
    }
    return String(stem) + "ing";
}

// True when `word` starts with the already-lowercased prefix.
bool startsWithAscii(const char *word, const String &prefixLower)
{
    size_t prefixLen = prefixLower.length();
    if (prefixLen == 0) {
        return false;
    }

    for (size_t i = 0; i < prefixLen; ++i) {
        char wc = word[i];
        if (wc == '\0') {
            return false;
        }
        if (toLowerAscii(wc) != prefixLower[i]) {
            return false;
        }
    }
    return true;
}

// Stable case-insensitive compare used to keep completion ordering deterministic.
int compareCaseInsensitiveAscii(const char *lhs, const char *rhs)
{
    size_t i = 0;
    while (lhs[i] != '\0' && rhs[i] != '\0') {
        char l = toLowerAscii(lhs[i]);
        char r = toLowerAscii(rhs[i]);
        if (l < r) {
            return -1;
        }
        if (l > r) {
            return 1;
        }
        ++i;
    }

    if (lhs[i] == '\0' && rhs[i] == '\0') {
        return strcmp(lhs, rhs);
    }
    return (lhs[i] == '\0') ? -1 : 1;
}

// Match helper for already materialized String completions.
bool startsWithLowercasePrefix(const String &word, const String &prefixLower)
{
    if (prefixLower.length() == 0 || word.length() < prefixLower.length()) {
        return false;
    }

    for (size_t i = 0; i < prefixLower.length(); ++i) {
        if (toLowerAscii(word[i]) != prefixLower[i]) {
            return false;
        }
    }
    return true;
}
} // namespace

// Return the current trailing word at cursor (lowercased) for completion lookup.
String CannedMessageModule::getFreeTextPrefix() const
{
    if (this->cursor == 0 || this->cursor > this->freetext.length() || this->cursor != this->freetext.length()) {
        return "";
    }

    unsigned int start = this->cursor;
    while (start > 0) {
        char c = this->freetext[start - 1];
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z')) {
            break;
        }
        --start;
    }

    if (start == this->cursor) {
        return "";
    }

    String prefix = this->freetext.substring(start, this->cursor);
    prefix.toLowerCase();
    return prefix;
}

// Refresh completion candidates based on current freetext cursor position.
void CannedMessageModule::updateFreeTextCompletion()
{
    String previousSelection = this->freeTextCompletion;
    this->freeTextCompletion = "";
    this->freeTextCompletionCount = 0;
    this->freeTextCompletionIndex = 0;
    if (this->runState != CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        return;
    }
    if (this->freeTextCompletionSuppressed) {
        return;
    }

    String prefixLower = getFreeTextPrefix();
    if (prefixLower.length() < 2) {
        return;
    }

    auto insertCandidate = [this, &prefixLower](const String &candidate) {
        if (!startsWithLowercasePrefix(candidate, prefixLower)) {
            return;
        }

        const size_t candidateLen = candidate.length();
        if (candidateLen <= static_cast<size_t>(prefixLower.length())) {
            return;
        }

        for (uint8_t i = 0; i < this->freeTextCompletionCount; ++i) {
            if (this->freeTextCompletions[i] == candidate) {
                return;
            }
        }

        uint8_t insertAt = this->freeTextCompletionCount;
        while (insertAt > 0) {
            const String &existing = this->freeTextCompletions[insertAt - 1];
            const size_t existingLen = existing.length();
            const int lexicalCompare = compareCaseInsensitiveAscii(candidate.c_str(), existing.c_str());
            if (candidateLen > existingLen || (candidateLen == existingLen && lexicalCompare >= 0)) {
                break;
            }
            --insertAt;
        }

        if (insertAt >= maxFreeTextCompletions) {
            return;
        }

        if (this->freeTextCompletionCount < maxFreeTextCompletions) {
            this->freeTextCompletionCount++;
        }
        for (int j = static_cast<int>(this->freeTextCompletionCount) - 1; j > static_cast<int>(insertAt); --j) {
            this->freeTextCompletions[j] = this->freeTextCompletions[j - 1];
        }
        this->freeTextCompletions[insertAt] = candidate;
    };

    for (size_t i = 0; i < (sizeof(kFreeTextCompletionWords) / sizeof(kFreeTextCompletionWords[0])); ++i) {
        const char *candidate = kFreeTextCompletionWords[i];
        if (!startsWithAscii(candidate, prefixLower)) {
            continue;
        }
        insertCandidate(candidate);
    }

    for (size_t i = 0; i < (sizeof(kFreeTextInflectionStems) / sizeof(kFreeTextInflectionStems[0])); ++i) {
        const CompletionInflectionStem &entry = kFreeTextInflectionStems[i];
        insertCandidate(entry.stem);
        if (entry.forms & COMPLETION_FORM_S) {
            insertCandidate(buildSForm(entry.stem));
        }
        if (entry.forms & COMPLETION_FORM_ED) {
            insertCandidate(buildEdForm(entry.stem));
        }
        if (entry.forms & COMPLETION_FORM_ING) {
            insertCandidate(buildIngForm(entry.stem));
        }
    }

    if (this->freeTextCompletionCount > 0) {
        uint8_t selectedIndex = 0;
        for (uint8_t i = 0; i < this->freeTextCompletionCount; ++i) {
            if (this->freeTextCompletions[i] == previousSelection) {
                selectedIndex = i;
                break;
            }
        }
        this->freeTextCompletionIndex = selectedIndex;
        this->freeTextCompletion = this->freeTextCompletions[this->freeTextCompletionIndex];
    }
}

// Cycle through available completions
bool CannedMessageModule::cycleFreeTextCompletion(int8_t step)
{
    if (this->freeTextCompletionCount < 2) {
        return false;
    }

    int next = static_cast<int>(this->freeTextCompletionIndex) + static_cast<int>(step);
    while (next < 0) {
        next += this->freeTextCompletionCount;
    }
    while (next >= this->freeTextCompletionCount) {
        next -= this->freeTextCompletionCount;
    }
    this->freeTextCompletionIndex = static_cast<uint8_t>(next);
    this->freeTextCompletion = this->freeTextCompletions[this->freeTextCompletionIndex];
    return true;
}

// Replace current word with selected completion and optionally append a space.
bool CannedMessageModule::acceptFreeTextCompletion(bool appendSpace)
{
    if (this->freeTextCompletion.length() == 0) {
        return false;
    }

    String prefixLower = getFreeTextPrefix();
    if (prefixLower.length() < 2 || this->freeTextCompletion.length() <= prefixLower.length()) {
        return false;
    }

    unsigned int start = this->cursor - prefixLower.length();
    String acceptedWord = this->freeTextCompletion;
    if (start < this->cursor && acceptedWord.length() > 0) {
        char typedFirstChar = this->freetext[start];
        if (typedFirstChar >= 'A' && typedFirstChar <= 'Z') {
            char acceptedFirstChar = acceptedWord[0];
            if (acceptedFirstChar >= 'a' && acceptedFirstChar <= 'z') {
                acceptedWord[0] = static_cast<char>(acceptedFirstChar - ('a' - 'A'));
            }
        }
    }

    this->freetext = this->freetext.substring(0, start) + acceptedWord + this->freetext.substring(this->cursor);
    this->cursor = start + acceptedWord.length();

    const uint16_t maxChars = 200 - (moduleConfig.canned_message.send_bell ? 1 : 0);
    if (appendSpace && this->cursor < maxChars) {
        this->freetext = this->freetext.substring(0, this->cursor) + " " + this->freetext.substring(this->cursor);
        this->cursor++;
    }
    if (this->freetext.length() > maxChars) {
        this->freetext = this->freetext.substring(0, maxChars);
        this->cursor = maxChars;
    }

    // Treat a just-selected completion as final until the user edits again.
    this->freeTextCompletionSuppressed = true;
    this->freeTextCompletion = "";
    this->freeTextCompletionCount = 0;
    this->freeTextCompletionIndex = 0;
    return true;
}

// Draw horizontally scrollable completion chips under the freetext input area.
void CannedMessageModule::drawFreeTextCompletionRow(OLEDDisplay *display, int16_t x, int16_t rowY, const String &completionPrefix)
{
    const int completionRowY = rowY;
    if (completionRowY < 0 || completionRowY >= display->getHeight()) {
        return;
    }

    const int spaceWidth = display->getStringWidth(" ");
    const int separatorWidth = spaceWidth;
    const int viewportWidth = display->getWidth();
    const int chipPaddingX = 3;
    const int chipRadius = 2;
    const int chipHeight = FONT_HEIGHT_SMALL;

    // Draw a filled rounded rectangle using rects + circles (works across OLED/EInk backends).
    auto drawRoundedFill = [display](int x0, int y0, int w0, int h0, int radius) {
        if (w0 <= 0 || h0 <= 0) {
            return;
        }

        int r = std::max(0, radius);
        r = std::min(r, std::min(w0 / 2, h0 / 2));
        if (r == 0) {
            display->fillRect(x0, y0, w0, h0);
            return;
        }

        const int centerW = w0 - (r * 2);
        const int sideH = h0 - (r * 2);
        if (centerW > 0) {
            display->fillRect(x0 + r, y0, centerW, h0);
        }
        if (sideH > 0) {
            display->fillRect(x0, y0 + r, r, sideH);
            display->fillRect(x0 + w0 - r, y0 + r, r, sideH);
        }
        display->fillCircle(x0 + r, y0 + r, r);
        display->fillCircle(x0 + w0 - r - 1, y0 + r, r);
        display->fillCircle(x0 + r, y0 + h0 - r - 1, r);
        display->fillCircle(x0 + w0 - r - 1, y0 + h0 - r - 1, r);
    };

    struct ChoiceLayout {
        uint8_t idx;
        String word;
        int startX;
        int width;
    };

    std::vector<ChoiceLayout> choices;
    int runningX = 0;
    bool hasChoices = false;
    int selectedStart = 0;
    int selectedEnd = 0;

    for (uint8_t i = 0; i < this->freeTextCompletionCount; ++i) {
        const String &candidate = this->freeTextCompletions[i];
        if (!startsWithLowercasePrefix(candidate, completionPrefix) || candidate.length() <= completionPrefix.length()) {
            continue;
        }

        if (hasChoices) {
            runningX += separatorWidth;
        }

        int tokenWidth = display->getStringWidth(candidate);
        int chipWidth = tokenWidth + (chipPaddingX * 2);
        choices.push_back({i, candidate, runningX, chipWidth});
        if (i == this->freeTextCompletionIndex) {
            selectedStart = runningX;
            selectedEnd = runningX + chipWidth;
        }

        runningX += chipWidth;
        hasChoices = true;
    }

    int choiceScrollX = 0;
    if (runningX > viewportWidth) {
        choiceScrollX = std::max(0, selectedEnd - viewportWidth);
        if (selectedStart < choiceScrollX) {
            choiceScrollX = selectedStart;
        }
        const int maxScrollX = std::max(0, runningX - viewportWidth);
        if (choiceScrollX > maxScrollX) {
            choiceScrollX = maxScrollX;
        }
    }

    // Center the whole row when all chips fit on screen; otherwise keep scroll behavior.
    const int centeredOffsetX = (runningX < viewportWidth) ? ((viewportWidth - runningX) / 2) : 0;
    const int drawBaseX = x + centeredOffsetX;

    for (const auto &choice : choices) {
        int boxX = drawBaseX + (choice.startX - choiceScrollX);
        if ((boxX + choice.width) < x || boxX > (x + viewportWidth)) {
            continue;
        }

        const int textX = boxX + chipPaddingX;
        if (choice.idx == this->freeTextCompletionIndex) {
            // Selected completion: filled rounded chip.
            display->setColor(WHITE);
            drawRoundedFill(boxX, completionRowY, choice.width, chipHeight, chipRadius);
            display->setColor(BLACK);
            display->drawString(textX, completionRowY, choice.word);
        } else {
            // Unselected completion: hollow rounded chip.
            display->setColor(WHITE);
            drawRoundedFill(boxX, completionRowY, choice.width, chipHeight, chipRadius);
            if (choice.width > 2 && chipHeight > 2) {
                display->setColor(BLACK);
                drawRoundedFill(boxX + 1, completionRowY + 1, choice.width - 2, chipHeight - 2, chipRadius - 1);
            }
            display->setColor(WHITE);
            display->drawString(textX, completionRowY, choice.word);
        }
    }
    display->setColor(WHITE);
}

// Insert visual cursor (and optional completion suffix hint) into rendered text.
String CannedMessageModule::drawWithCursor(String text, int cursor)
{
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > static_cast<int>(text.length())) {
        cursor = text.length();
    }

    String completionSuffix = "";
    if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT && this->cursor == this->freetext.length() &&
        this->freeTextCompletionCount > 0 && this->freeTextCompletionIndex < this->freeTextCompletionCount) {
        String prefixLower = this->getFreeTextPrefix();
        const String &candidate = this->freeTextCompletions[this->freeTextCompletionIndex];
        if (prefixLower.length() >= 2 && startsWithAscii(candidate.c_str(), prefixLower) &&
            candidate.length() > prefixLower.length()) {
            completionSuffix = candidate.substring(prefixLower.length());
        }
    }

    return text.substring(0, cursor) + "|" + completionSuffix + text.substring(cursor);
}

// If idle screen receives printable key, jump directly into freetext mode.
bool CannedMessageModule::tryStartFreeTextFromInactive(const InputEvent *event)
{
    if (event->kbchar < 32 || event->kbchar > 126) {
        return false;
    }

    runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    // Process the initiating key as the first freetext character.
    return handleFreeTextInput(event);
}

// Apply pending freetext payload action (typing/edit/move) and refresh UI state.
int32_t CannedMessageModule::runFreeTextState(UIFrameEvent &e)
{
    switch (this->payload) {
    case INPUT_BROKER_LEFT:
        if (this->cursor == this->freetext.length() && this->freeTextCompletionCount > 1) {
            this->cycleFreeTextCompletion(-1);
        } else if (this->cursor > 0) {
            this->cursor--;
        }
        break;
    case INPUT_BROKER_RIGHT:
        if (this->cursor < this->freetext.length()) {
            this->cursor++;
        } else if (this->freeTextCompletionCount > 1) {
            this->cycleFreeTextCompletion(1);
        } else if (this->freeTextCompletionCount > 0) {
            this->acceptFreeTextCompletion(true);
        }
        break;
    default:
        break;
    }

    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    switch (this->payload) {
    case 0x08: // backspace
        this->freeTextCompletionSuppressed = false;
        if (this->freetext.length() > 0 && this->cursor > 0) {
            if (this->cursor == this->freetext.length()) {
                this->freetext = this->freetext.substring(0, this->freetext.length() - 1);
            } else {
                this->freetext = this->freetext.substring(0, this->cursor - 1) +
                                 this->freetext.substring(this->cursor, this->freetext.length());
            }
            this->cursor--;
        }
        break;
    case INPUT_BROKER_MSG_TAB:
        return 0;
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_RIGHT:
        break;
    default:
        if (this->payload >= 32 && this->payload <= 126) {
            this->freeTextCompletionSuppressed = false;
            requestFocus();
            if (this->cursor == this->freetext.length()) {
                this->freetext += static_cast<char>(this->payload);
                this->cursor++;
            } else {
                this->freetext = this->freetext.substring(0, this->cursor) + static_cast<char>(this->payload) +
                                 this->freetext.substring(this->cursor);
                this->cursor++;
            }
            const uint16_t maxChars = 200 - (moduleConfig.canned_message.send_bell ? 1 : 0);
            if (this->freetext.length() > maxChars) {
                this->cursor = maxChars;
                this->freetext = this->freetext.substring(0, maxChars);
            }
        }
        break;
    }

    this->updateFreeTextCompletion();
    this->lastTouchMillis = millis();
    this->notifyObservers(&e);
    return kInactivateAfterMs;
}

// Route physical/touch input while in freetext mode.
bool CannedMessageModule::handleFreeTextInput(const InputEvent *event)
{
    if (runState != CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        return false;
    }

#if defined(USE_VIRTUAL_KEYBOARD)
    if (event->inputEvent == INPUT_BROKER_LEFT) {
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        freetext = "";
        cursor = 0;
        payload = 0;
        currentMessageIndex = -1;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
        return true;
    }

    if (event->touchX != 0 || event->touchY != 0) {
        String keyTapped = keyForCoordinates(event->touchX, event->touchY);
        bool valid = false;

        if (keyTapped == "⇧") {
            highlight = -1;
            payload = 0x00;
            shift = !shift;
            valid = true;
        } else if (keyTapped == "⌫") {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = 0x08;
            shift = false;
            valid = true;
        } else if (keyTapped == "123" || keyTapped == "ABC") {
            highlight = -1;
            payload = 0x00;
            charSet = (charSet == 0 ? 1 : 0);
            valid = true;
        } else if (keyTapped == " ") {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = keyTapped[0];
            shift = false;
            valid = true;
        } else if (keyTapped == "↵") {
            runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
            payload = CANNED_MESSAGE_RUN_STATE_FREETEXT;
            currentMessageIndex = -1;
            shift = false;
            valid = true;
        } else if (!(keyTapped == "")) {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = shift ? keyTapped[0] : std::tolower(keyTapped[0]);
            shift = false;
            valid = true;
        }

        if (valid) {
            lastTouchMillis = millis();
            runOnce();
            payload = 0;
            return true;
        }
    }
#endif

    if (event->kbchar == INPUT_BROKER_MSG_EMOTE_LIST) {
        runState = CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER;
        requestFocus();
        screen->forceDisplay();
        return true;
    }

    bool isSelect = isSelectEvent(event);
    if (isSelect) {
        if (this->cursor == this->freetext.length() && this->freeTextCompletionCount > 0 &&
            this->acceptFreeTextCompletion(true)) {
            this->payload = 0;
            this->lastTouchMillis = millis();
            requestFocus();
            runOnce();
            return true;
        }

        LOG_DEBUG("[SELECT] handleFreeTextInput: runState=%d, dest=%u, channel=%d, freetext='%s'", (int)runState, dest, channel,
                  freetext.c_str());
        if (dest == 0) {
            dest = NODENUM_BROADCAST;
        }
        if (channel >= channels.getNumChannels()) {
            channel = 0;
        }

        payload = CANNED_MESSAGE_RUN_STATE_FREETEXT;
        currentMessageIndex = -1;
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    if (event->inputEvent == INPUT_BROKER_BACK && this->freetext.length() > 0) {
        payload = 0x08;
        lastTouchMillis = millis();
        requestFocus();
        runOnce();
        return true;
    }

    if (event->inputEvent == INPUT_BROKER_LEFT) {
        payload = INPUT_BROKER_LEFT;
        lastTouchMillis = millis();
        requestFocus();
        runOnce();
        return true;
    }
    if (event->inputEvent == INPUT_BROKER_RIGHT) {
        payload = INPUT_BROKER_RIGHT;
        lastTouchMillis = millis();
        requestFocus();
        runOnce();
        return true;
    }

    if (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_ALT_LONG ||
        (event->inputEvent == INPUT_BROKER_BACK && this->freetext.length() == 0)) {
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        freetext = "";
        cursor = 0;
        payload = 0;
        currentMessageIndex = -1;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
        return true;
    }

    if (event->kbchar == INPUT_BROKER_MSG_TAB) {
        return handleTabSwitch(event);
    }

    if (event->kbchar >= 32 && event->kbchar <= 126) {
        payload = event->kbchar;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    return false;
}

// Navigate/select emotes, then insert selected emote into freetext.
int CannedMessageModule::handleEmotePickerInput(const InputEvent *event)
{
    int numEmotes = graphics::numEmotes;

    bool isUp = isUpEvent(event);
    bool isDown = isDownEvent(event);
    bool isSelect = isSelectEvent(event);
    if (runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER) {
        if (event->inputEvent == INPUT_BROKER_USER_PRESS) {
            isDown = true;
        } else if (event->inputEvent == INPUT_BROKER_SELECT) {
            isSelect = true;
        }
    }

    if (isUp && emotePickerIndex > 0) {
        emotePickerIndex--;
        screen->forceDisplay();
        return 1;
    }
    if (isDown && emotePickerIndex < numEmotes - 1) {
        emotePickerIndex++;
        screen->forceDisplay();
        return 1;
    }

    if (isSelect) {
        String label = graphics::emotes[emotePickerIndex].label;
        String emoteInsert = label;
        if (cursor == freetext.length()) {
            freetext += emoteInsert;
        } else {
            freetext = freetext.substring(0, cursor) + emoteInsert + freetext.substring(cursor);
        }
        cursor += emoteInsert.length();
        runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
        screen->forceDisplay();
        return 1;
    }

    if (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_ALT_LONG) {
        runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
        screen->forceDisplay();
        return 1;
    }

    return 0;
}

// Draw freetext composer UI (header, live text, and completion row/keyboard).
void CannedMessageModule::drawFreeTextScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y, char *buffer)
{
    requestFocus();
#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
    EInkDynamicDisplay *einkDisplay = static_cast<EInkDynamicDisplay *>(display);
    einkDisplay->enableUnlimitedFastMode();
#endif

#if defined(USE_VIRTUAL_KEYBOARD)
    drawKeyboard(display, state, 0, 0);
#else
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Draw node/channel header at the top.
    drawHeader(display, x, y, buffer);

    // Char count right-aligned.
    uint16_t charsLeft =
        meshtastic_Constants_DATA_PAYLOAD_LEN - this->freetext.length() - (moduleConfig.canned_message.send_bell ? 1 : 0);
    snprintf(buffer, 50, "%d left", charsLeft);
    display->drawString(x + display->getWidth() - display->getStringWidth(buffer), y + 0, buffer);

#if INPUTBROKER_SERIAL_TYPE == 1
    // Chatter Modifier key mode label (right side).
    {
        uint8_t mode = globalSerialKeyboard ? globalSerialKeyboard->getShift() : 0;
        const char *label = (mode == 0) ? "a" : (mode == 1) ? "A" : "#";

        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_LEFT);

        const int16_t th = FONT_HEIGHT_SMALL;
        const int16_t tw = display->getStringWidth(label);
        const int16_t padX = 3;
        const int16_t padY = 2;
        const int16_t r = 3;

        const int16_t bw = tw + padX * 2;
        const int16_t bh = th + padY * 2;

        const int16_t bx = x + display->getWidth() - bw - 2;
        const int16_t by = y + display->getHeight() - bh - 2;

        display->setColor(WHITE);
        display->fillRect(bx + r, by, bw - r * 2, bh);
        display->fillRect(bx, by + r, r, bh - r * 2);
        display->fillRect(bx + bw - r, by + r, r, bh - r * 2);
        display->fillCircle(bx + r, by + r, r);
        display->fillCircle(bx + bw - r - 1, by + r, r);
        display->fillCircle(bx + r, by + bh - r - 1, r);
        display->fillCircle(bx + bw - r - 1, by + bh - r - 1, r);

        display->setColor(BLACK);
        display->drawString(bx + padX, by + padY, label);
    }

    // Left-side destination hint box ("Dest: Shift + <").
    {
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_LEFT);

        const char *label = "Dest: Shift + ";
        int16_t labelW = display->getStringWidth(label);

        // Triangle size visually matches glyph height, not full line height.
        const int triH = FONT_HEIGHT_SMALL - 3;
        const int triW = triH * 0.7;

        const int16_t padX = 3;
        const int16_t padY = 2;
        const int16_t r = 3;

        const int16_t bw = labelW + triW + padX * 2 + 2;
        const int16_t bh = FONT_HEIGHT_SMALL + padY * 2;

        const int16_t bx = x + 2;
        const int16_t by = y + display->getHeight() - bh - 2;

        // Rounded white box.
        display->setColor(WHITE);
        display->fillRect(bx + r, by, bw - (r * 2), bh);
        display->fillRect(bx, by + r, r, bh - (r * 2));
        display->fillRect(bx + bw - r, by + r, r, bh - (r * 2));
        display->fillCircle(bx + r, by + r, r);
        display->fillCircle(bx + bw - r - 1, by + r, r);
        display->fillCircle(bx + r, by + bh - r - 1, r);
        display->fillCircle(bx + bw - r - 1, by + bh - r - 1, r);

        // Draw text.
        display->setColor(BLACK);
        display->drawString(bx + padX, by + padY, label);

        // Center triangle on text baseline.
        int16_t tx = bx + padX + labelW;
        int16_t ty = by + padY + (FONT_HEIGHT_SMALL / 2) - (triH / 2) - 1;

        display->fillTriangle(tx + triW, ty, tx, ty + triH / 2, tx + triW, ty + triH);
    }
#endif

    // Draw freetext input with multi-emote support and proper line wrapping.
    display->setColor(WHITE);
    {
        const int inputTopOffset = -3;
        int inputY = y + FONT_HEIGHT_SMALL + inputTopOffset;
        String msgWithCursor = this->drawWithCursor(this->freetext, this->cursor);

        // Tokenize input into (isEmote, token) pairs.
        const char *msg = msgWithCursor.c_str();
        std::vector<std::pair<bool, String>> tokens = freeTextModule::tokenizeMessageWithEmotes(msg);

        // Advanced word-wrapping (emotes + text, split by word, wrap inside word if needed).
        std::vector<std::vector<std::pair<bool, String>>> lines;
        std::vector<std::pair<bool, String>> currentLine;
        int lineWidth = 0;
        int maxWidth = display->getWidth();
        for (auto &token : tokens) {
            if (token.first) {
                int tokenWidth = 0;
                for (int j = 0; j < graphics::numEmotes; j++) {
                    if (token.second == graphics::emotes[j].label) {
                        tokenWidth = graphics::emotes[j].width + 2;
                        break;
                    }
                }
                if (lineWidth + tokenWidth > maxWidth && !currentLine.empty()) {
                    lines.push_back(currentLine);
                    currentLine.clear();
                    lineWidth = 0;
                }
                currentLine.push_back(token);
                lineWidth += tokenWidth;
            } else {
                // Text: split by words and wrap inside word if needed.
                String text = token.second;
                int pos = 0;
                while (pos < static_cast<int>(text.length())) {
                    int spacePos = text.indexOf(' ', pos);
                    int endPos = (spacePos == -1) ? text.length() : spacePos + 1; // Include space.
                    String word = text.substring(pos, endPos);
                    int wordWidth = display->getStringWidth(word);

                    if (lineWidth + wordWidth > maxWidth && lineWidth > 0) {
                        lines.push_back(currentLine);
                        currentLine.clear();
                        lineWidth = 0;
                    }
                    if (wordWidth > maxWidth) {
                        uint16_t charPos = 0;
                        while (charPos < word.length()) {
                            String oneChar = word.substring(charPos, charPos + 1);
                            int charWidth = display->getStringWidth(oneChar);
                            if (lineWidth + charWidth > maxWidth && lineWidth > 0) {
                                lines.push_back(currentLine);
                                currentLine.clear();
                                lineWidth = 0;
                            }
                            currentLine.push_back({false, oneChar});
                            lineWidth += charWidth;
                            charPos++;
                        }
                    } else {
                        currentLine.push_back({false, word});
                        lineWidth += wordWidth;
                    }
                    pos = endPos;
                }
            }
        }
        if (!currentLine.empty())
            lines.push_back(currentLine);

        const int rowHeight = std::max(8, FONT_HEIGHT_SMALL - 3);
        const int viewportTop = inputY;
        const int viewportBottom = y + display->getHeight();
        String completionPrefix = this->getFreeTextPrefix();
        const bool showCompletionRow =
            (this->cursor == this->freetext.length() && this->freeTextCompletionCount > 1 && completionPrefix.length() >= 2);

        // Reserve enough space for the completion row so chip/text rendering is not clipped.
        const int completionRowHeight = FONT_HEIGHT_SMALL + 1;
        const int textViewportBottom = showCompletionRow ? std::max(viewportTop, viewportBottom - completionRowHeight) : viewportBottom;
        const int viewportHeight = std::max(1, textViewportBottom - viewportTop);
        const int viewportRows = std::max(1, viewportHeight / rowHeight);

        int cursorRow = static_cast<int>(lines.size()) - 1;
        if (cursorRow < 0) {
            cursorRow = 0;
        }
        if (this->cursor < this->freetext.length()) {
            for (int lineIdx = 0; lineIdx < static_cast<int>(lines.size()); ++lineIdx) {
                bool hasCursorMarker = false;
                for (const auto &token : lines[lineIdx]) {
                    if (!token.first && token.second.indexOf('_') >= 0) {
                        hasCursorMarker = true;
                        break;
                    }
                }
                if (hasCursorMarker) {
                    cursorRow = lineIdx;
                    break;
                }
            }
        }

        const int totalRows = static_cast<int>(lines.size());
        int scrollRows = std::max(0, totalRows - viewportRows);
        int targetRow = cursorRow;
        if (targetRow < scrollRows) {
            scrollRows = targetRow;
        }
        if (targetRow >= (scrollRows + viewportRows)) {
            scrollRows = targetRow - viewportRows + 1;
        }
        if (scrollRows < 0) {
            scrollRows = 0;
        }

        // Draw wrapped text rows with vertical scrolling.
        for (int rowIdx = 0; rowIdx < static_cast<int>(lines.size()); ++rowIdx) {
            int yLine = viewportTop + ((rowIdx - scrollRows) * rowHeight);
            if (yLine < viewportTop || yLine >= textViewportBottom) {
                continue;
            }

            int nextX = x;
            for (const auto &token : lines[rowIdx]) {
                if (token.first) {
                    freeTextModule::renderEmote(display, nextX, yLine, rowHeight, token.second);
                } else {
                    display->drawString(nextX, yLine, token.second);
                    nextX += display->getStringWidth(token.second);
                }
            }
        }

        if (showCompletionRow) {
            this->drawFreeTextCompletionRow(display, x, viewportBottom - completionRowHeight, completionPrefix);
        }
    }
#endif
}

// Draw scrollable emote picker list.
void CannedMessageModule::drawEmotePickerScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const int headerFontHeight = FONT_HEIGHT_SMALL;
    const int headerMargin = 2;
    const int labelGap = 6;
    const int bitmapGapX = 4;

    int maxEmoteHeight = 0;
    for (int i = 0; i < graphics::numEmotes; ++i) {
        if (graphics::emotes[i].height > maxEmoteHeight) {
            maxEmoteHeight = graphics::emotes[i].height;
        }
    }

    const int rowHeight = maxEmoteHeight + 2;
    int headerY = y;
    int listTop = headerY + headerFontHeight + headerMargin;
    int _visibleRows = (display->getHeight() - listTop - 2) / rowHeight;
    int numEmotes = graphics::numEmotes;

    this->visibleRows = _visibleRows;

    if (emotePickerIndex < 0) {
        emotePickerIndex = 0;
    }
    if (emotePickerIndex >= numEmotes) {
        emotePickerIndex = numEmotes - 1;
    }

    int topIndex = emotePickerIndex - _visibleRows / 2;
    if (topIndex < 0) {
        topIndex = 0;
    }
    if (topIndex > numEmotes - _visibleRows) {
        topIndex = std::max(0, numEmotes - _visibleRows);
    }

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2, headerY, "Select Emote");
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    for (int vis = 0; vis < _visibleRows; ++vis) {
        int emoteIdx = topIndex + vis;
        if (emoteIdx >= numEmotes) {
            break;
        }
        const graphics::Emote &emote = graphics::emotes[emoteIdx];
        int rowY = listTop + vis * rowHeight;

        if (emoteIdx == emotePickerIndex) {
            display->fillRect(x, rowY, display->getWidth() - 8, emote.height + 2);
            display->setColor(BLACK);
        }

        int emoteY = rowY + 1;
        display->drawXbm(x + bitmapGapX, emoteY, emote.width, emote.height, emote.bitmap);

        display->setFont(FONT_MEDIUM);
        int labelY = rowY + ((rowHeight - FONT_HEIGHT_MEDIUM) / 2);
        display->drawString(x + bitmapGapX + emote.width + labelGap, labelY, emote.label);

        if (emoteIdx == emotePickerIndex) {
            display->setColor(WHITE);
        }
    }

    if (numEmotes > _visibleRows) {
        int scrollbarHeight = _visibleRows * rowHeight;
        int scrollTrackX = display->getWidth() - 6;
        display->drawRect(scrollTrackX, listTop, 4, scrollbarHeight);
        int scrollBarLen = std::max(6, (scrollbarHeight * _visibleRows) / numEmotes);
        int scrollBarPos = listTop + (scrollbarHeight * topIndex) / numEmotes;
        display->fillRect(scrollTrackX, scrollBarPos, 4, scrollBarLen);
    }
}

#endif
