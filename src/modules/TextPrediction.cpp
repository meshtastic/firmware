#include "configuration.h"
#if HAS_SCREEN
#include "graphics/ScreenFonts.h"
#include "modules/CannedMessageModule.h"
#include <cstring>

namespace
{
char toLowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c + ('a' - 'A'));
    }
    return c;
}

// Small built-in dictionary for lightweight freetext word prediction.
// Keep this list compact to limit flash/RAM usage on constrained targets.
static const char *const kFreeTextPredictWords[] = {
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

void CannedMessageModule::updateFreeTextPrediction()
{
    String previousSelection = this->freeTextPrediction;
    this->freeTextPrediction = "";
    this->freeTextPredictionCount = 0;
    this->freeTextPredictionIndex = 0;
    if (this->runState != CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        return;
    }
    if (this->freeTextPredictionSuppressed) {
        return;
    }

    String prefixLower = getFreeTextPrefix();
    if (prefixLower.length() < 2) {
        return;
    }

    for (size_t i = 0; i < (sizeof(kFreeTextPredictWords) / sizeof(kFreeTextPredictWords[0])); ++i) {
        const char *candidate = kFreeTextPredictWords[i];
        if (!startsWithAscii(candidate, prefixLower)) {
            continue;
        }

        const size_t candidateLen = strlen(candidate);
        if (candidateLen <= static_cast<size_t>(prefixLower.length())) {
            continue;
        }

        uint8_t insertAt = this->freeTextPredictionCount;
        while (insertAt > 0) {
            const String &existing = this->freeTextPredictions[insertAt - 1];
            const size_t existingLen = existing.length();
            const int lexicalCompare = compareCaseInsensitiveAscii(candidate, existing.c_str());
            if (candidateLen > existingLen || (candidateLen == existingLen && lexicalCompare >= 0)) {
                break;
            }
            --insertAt;
        }

        if (insertAt >= maxFreeTextPredictions) {
            continue;
        }

        if (this->freeTextPredictionCount < maxFreeTextPredictions) {
            this->freeTextPredictionCount++;
        }
        for (int j = static_cast<int>(this->freeTextPredictionCount) - 1; j > static_cast<int>(insertAt); --j) {
            this->freeTextPredictions[j] = this->freeTextPredictions[j - 1];
        }
        this->freeTextPredictions[insertAt] = candidate;
    }

    if (this->freeTextPredictionCount > 0) {
        uint8_t selectedIndex = 0;
        for (uint8_t i = 0; i < this->freeTextPredictionCount; ++i) {
            if (this->freeTextPredictions[i] == previousSelection) {
                selectedIndex = i;
                break;
            }
        }
        this->freeTextPredictionIndex = selectedIndex;
        this->freeTextPrediction = this->freeTextPredictions[this->freeTextPredictionIndex];
    }
}

bool CannedMessageModule::cycleFreeTextPrediction(int8_t step)
{
    if (this->freeTextPredictionCount < 2) {
        return false;
    }

    int next = static_cast<int>(this->freeTextPredictionIndex) + static_cast<int>(step);
    while (next < 0) {
        next += this->freeTextPredictionCount;
    }
    while (next >= this->freeTextPredictionCount) {
        next -= this->freeTextPredictionCount;
    }
    this->freeTextPredictionIndex = static_cast<uint8_t>(next);
    this->freeTextPrediction = this->freeTextPredictions[this->freeTextPredictionIndex];
    return true;
}

bool CannedMessageModule::acceptFreeTextPrediction(bool appendSpace)
{
    if (this->freeTextPrediction.length() == 0) {
        return false;
    }

    String prefixLower = getFreeTextPrefix();
    if (prefixLower.length() < 2 || this->freeTextPrediction.length() <= prefixLower.length()) {
        return false;
    }

    unsigned int start = this->cursor - prefixLower.length();
    String acceptedWord = this->freeTextPrediction;
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

    // Treat a just-selected prediction as final until the user edits again.
    this->freeTextPredictionSuppressed = true;
    this->freeTextPrediction = "";
    this->freeTextPredictionCount = 0;
    this->freeTextPredictionIndex = 0;
    return true;
}

void CannedMessageModule::drawFreeTextPredictionRow(OLEDDisplay *display, int16_t x, int16_t viewportTop, int16_t viewportBottom,
                                                    int rowHeight, int linesCount, int scrollRows, const String &predictionPrefix)
{
    const int predictionRowY = viewportTop + ((linesCount - scrollRows) * rowHeight);
    if (predictionRowY < (viewportTop - rowHeight) || predictionRowY > viewportBottom) {
        return;
    }

    const int spaceWidth = display->getStringWidth(" ");
    const int separatorWidth = spaceWidth * 2;
    const int viewportWidth = display->getWidth();

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

    for (uint8_t i = 0; i < this->freeTextPredictionCount; ++i) {
        const String &candidate = this->freeTextPredictions[i];
        if (!startsWithLowercasePrefix(candidate, predictionPrefix) || candidate.length() <= predictionPrefix.length()) {
            continue;
        }

        if (hasChoices) {
            runningX += separatorWidth;
        }

        int tokenWidth = display->getStringWidth(candidate);
        choices.push_back({i, candidate, runningX, tokenWidth});
        if (i == this->freeTextPredictionIndex) {
            selectedStart = runningX;
            selectedEnd = runningX + tokenWidth;
        }

        runningX += tokenWidth;
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

    for (const auto &choice : choices) {
        int drawX = x + (choice.startX - choiceScrollX);
        if ((drawX + choice.width) < x || drawX > (x + viewportWidth)) {
            continue;
        }

        if (choice.idx == this->freeTextPredictionIndex) {
#ifdef USE_EINK
            display->drawRect(drawX - 1, predictionRowY, choice.width + 2, FONT_HEIGHT_SMALL - 1);
            display->drawString(drawX, predictionRowY, choice.word);
#else
            display->fillRect(drawX - 1, predictionRowY, choice.width + 2, FONT_HEIGHT_SMALL);
            display->setColor(BLACK);
            display->drawString(drawX, predictionRowY, choice.word);
            display->setColor(WHITE);
#endif
        } else {
            display->drawString(drawX, predictionRowY, choice.word);
        }
    }
}

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
        this->freeTextPredictionCount > 0 && this->freeTextPredictionIndex < this->freeTextPredictionCount) {
        String prefixLower = this->getFreeTextPrefix();
        const String &candidate = this->freeTextPredictions[this->freeTextPredictionIndex];
        if (prefixLower.length() >= 2 && startsWithAscii(candidate.c_str(), prefixLower) &&
            candidate.length() > prefixLower.length()) {
            completionSuffix = candidate.substring(prefixLower.length());
        }
    }

    return text.substring(0, cursor) + "|" + completionSuffix + text.substring(cursor);
}

#endif
