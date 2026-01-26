#include "modules/ChineseIme.h"
#include "modules/PinyinData.h"

#if defined(T_DECK_PRO) || defined(T_DECK)
#include <pgmspace.h>
#endif

void ChineseIme::setEnabled(bool enabled)
{
#if !defined(T_DECK_PRO) && !defined(T_DECK)
    (void)enabled;
    if (this->enabled) {
        this->enabled = false;
        reset();
    }
    return;
#endif
    if (this->enabled == enabled)
        return;

    this->enabled = enabled;
    if (!this->enabled) {
        reset();
    }
}

bool ChineseIme::isEnabled() const
{
    return enabled;
}

void ChineseIme::reset()
{
    imeBuffer = "";
    imeCandidates.clear();
    imeCandidateIndex = 0;
}

bool ChineseIme::hasBuffer() const
{
    return imeBuffer.length() > 0;
}

const String &ChineseIme::buffer() const
{
    return imeBuffer;
}

const std::vector<String> &ChineseIme::candidates() const
{
    return imeCandidates;
}

int ChineseIme::candidateIndex() const
{
    return imeCandidateIndex;
}

void ChineseIme::appendLetter(char c)
{
    if (!enabled)
        return;
    if (imeBuffer.length() >= kMaxBufferLen)
        return;

    imeBuffer += c;
    updateCandidates();
}

void ChineseIme::backspace()
{
    if (!enabled)
        return;
    if (imeBuffer.length() == 0)
        return;

    imeBuffer.remove(imeBuffer.length() - 1);
    updateCandidates();
}

void ChineseIme::moveCandidate(int delta)
{
    if (!enabled)
        return;
    if (imeCandidates.empty())
        return;

    int size = static_cast<int>(imeCandidates.size());
    imeCandidateIndex = (imeCandidateIndex + delta) % size;
    if (imeCandidateIndex < 0)
        imeCandidateIndex += size;
}

bool ChineseIme::commitCandidate(int index, String &out)
{
    if (!enabled)
        return false;
    if (imeBuffer.length() == 0)
        return false;

    if (index >= 0 && index < static_cast<int>(imeCandidates.size()))
        out = imeCandidates[index];
    else if (!imeCandidates.empty())
        out = imeCandidates[0];
    else
        out = imeBuffer;

    reset();
    return true;
}

bool ChineseIme::commitActive(String &out)
{
    if (!enabled)
        return false;
    return commitCandidate(imeCandidateIndex, out);
}

void ChineseIme::updateCandidates()
{
    imeCandidates.clear();
    imeCandidateIndex = 0;

    if (!enabled)
        return;
    if (imeBuffer.length() == 0)
        return;

    updateCandidatesFromBuiltin();
}

void ChineseIme::updateCandidatesFromBuiltin()
{
#if !defined(T_DECK_PRO) && !defined(T_DECK)
    return;
#else
    const size_t kMaxCandidates = 50;
    std::vector<String> exactCandidates;
    std::vector<String> prefixCandidates;
    auto appendCandidate = [](std::vector<String> &list, const String &candidate) {
        for (const auto &existing : list) {
            if (existing == candidate)
                return;
        }
        list.emplace_back(candidate);
    };
    auto appendCandidates = [&](std::vector<String> &list, const char *candidates) {
        const char *p = candidates;
        while (*p && list.size() < kMaxCandidates) {
            while (*p == ' ')
                ++p;
            const char *start = p;
            while (*p && *p != ' ')
                ++p;
            if (p > start)
                appendCandidate(list, String(start).substring(0, p - start));
        }
    };
    auto processLine = [&](const String &line) {
        String trimmed = line;
        trimmed.trim();
        if (trimmed.length() == 0 || trimmed.startsWith("#"))
            return;

        int tabPos = trimmed.indexOf('\t');
        int splitPos = (tabPos >= 0) ? tabPos : trimmed.indexOf(' ');
        if (splitPos <= 0)
            return;

        String pinyin = trimmed.substring(0, splitPos);
        String candidates = trimmed.substring(splitPos + 1);
        candidates.trim();
        if (candidates.length() == 0)
            return;

        if (pinyin == imeBuffer) {
            appendCandidates(exactCandidates, candidates.c_str());
        } else if (pinyin.startsWith(imeBuffer)) {
            appendCandidates(prefixCandidates, candidates.c_str());
        }
    };

    String line;
    const char *ptr = kPinyinDict;
    while (true) {
        char c = static_cast<char>(pgm_read_byte(ptr++));
        if (c == '\0') {
            if (line.length() > 0)
                processLine(line);
            break;
        }
        if (c == '\r')
            continue;
        if (c == '\n') {
            processLine(line);
            line = "";
            continue;
        }
        line += c;
    }

    for (const auto &candidate : exactCandidates) {
        if (imeCandidates.size() >= kMaxCandidates)
            break;
        imeCandidates.emplace_back(candidate);
    }
    for (const auto &candidate : prefixCandidates) {
        if (imeCandidates.size() >= kMaxCandidates)
            break;
        bool exists = false;
        for (const auto &existing : imeCandidates) {
            if (existing == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists)
            imeCandidates.emplace_back(candidate);
    }
#endif
}
