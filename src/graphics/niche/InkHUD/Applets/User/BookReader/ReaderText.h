#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NicheGraphics::InkHUD
{
constexpr const char *BULLET_MARKER = "[[_BR_BULLET_]]";
constexpr const char *FIRST_LINE_MARKER = "[[_BR_FIRST_]]";
constexpr const char *TABLE_START = "[[_BR_TABLE_]]";
constexpr const char *TABLE_END = "[[_BR_TABLE_END_]]";
constexpr const char *TABLE_ROW = "[[_BR_ROW_]]";
constexpr const char *TABLE_CELL = "[[_BR_CELL_]]";
constexpr const char *TABLE_LINE = "[[_BR_GRID_]]";

inline size_t readerTagEnd(std::string_view text, size_t pos)
{
    if (pos >= text.size())
        return std::string_view::npos;
    const size_t start = pos && text[pos - 1] == '<' ? pos - 1 : pos;
    for (const auto &block : {std::pair{"<!--", "-->"}, std::pair{"<![CDATA[", "]]>"}}) {
        if (text.compare(start, std::strlen(block.first), block.first) == 0) {
            const size_t end = text.find(block.second, start + std::strlen(block.first));
            return end == std::string_view::npos ? end : end + 2;
        }
    }
    char quote = 0;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (quote) {
            if (c == quote)
                quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '>') {
            return pos;
        }
    }
    return std::string_view::npos;
}

inline std::string readerXmlAttribute(std::string_view tag, std::string_view name)
{
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    size_t pos = 0;
    while (pos < tag.size()) {
        if (space(tag[pos]) || tag[pos] == '<' || tag[pos] == '/' || tag[pos] == '?') {
            ++pos;
            continue;
        }
        if (tag[pos] == '>')
            break;
        const size_t start = pos;
        while (pos < tag.size() && !space(tag[pos]) && tag[pos] != '=' && tag[pos] != '/' && tag[pos] != '>' &&
               tag[pos] != '\'' && tag[pos] != '"')
            ++pos;
        const std::string_view key = tag.substr(start, pos - start);
        while (pos < tag.size() && space(tag[pos]))
            ++pos;
        if (pos == tag.size())
            break;
        if (tag[pos] != '=') {
            if (tag[pos] == '\'' || tag[pos] == '"') {
                const size_t end = tag.find(tag[pos], pos + 1);
                pos = end == std::string_view::npos ? tag.size() : end + 1;
            }
            continue;
        }
        ++pos;
        while (pos < tag.size() && space(tag[pos]))
            ++pos;
        if (pos == tag.size())
            break;
        const char quote = tag[pos] == '\'' || tag[pos] == '"' ? tag[pos++] : 0;
        const size_t valueStart = pos;
        while (pos < tag.size() && (quote ? tag[pos] != quote : !space(tag[pos]) && tag[pos] != '>'))
            ++pos;
        if (quote && pos == tag.size())
            return {};
        const std::string_view value = tag.substr(valueStart, pos - valueStart);
        if (quote)
            ++pos;
        bool matches = !name.empty() && key.size() == name.size();
        for (size_t i = 0; matches && i < key.size(); ++i)
            matches = lower(key[i]) == lower(name[i]);
        if (!matches)
            continue;

        std::string out;
        out.reserve(value.size());
        for (size_t i = 0; i < value.size();) {
            if (value[i] != '&') {
                out.push_back(value[i++]);
                continue;
            }
            const size_t end = value.find_first_of(";&", i + 1);
            if (end == std::string_view::npos) {
                out.append(value.substr(i));
                break;
            }
            if (value[end] == '&') {
                out.append(value.substr(i, end - i));
                i = end;
                continue;
            }
            const std::string_view ref = value.substr(i + 1, end - i - 1);
            uint32_t cp = ref == "amp"      ? '&'
                          : ref == "lt"     ? '<'
                          : ref == "gt"     ? '>'
                          : ref == "quot"   ? '"'
                          : ref == "apos"   ? '\''
                          : ref == "nbsp"   ? 0xA0
                          : ref == "hellip" ? 0x2026
                                            : 0;
            if (!ref.empty() && ref[0] == '#') {
                const bool hex = ref.size() > 1 && ref[1] == 'x';
                size_t digit = hex ? 2 : 1;
                bool valid = digit < ref.size();
                const uint32_t base = hex ? 16 : 10;
                for (; valid && digit < ref.size(); ++digit) {
                    const char c = ref[digit];
                    const uint32_t n = c >= '0' && c <= '9'   ? c - '0'
                                       : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                       : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                              : 16;
                    valid = n < base && cp <= (0x10FFFFU - n) / base;
                    if (valid)
                        cp = cp * base + n;
                }
                if (!valid)
                    cp = 0;
            }
            const bool valid = cp == 9 || cp == 10 || cp == 13 || (cp >= 0x20 && cp <= 0xD7FF) ||
                               (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
            if (!valid) {
                out.append(value.substr(i, end - i + 1));
            } else if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else {
                const unsigned count = cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
                out.push_back(static_cast<char>((count == 2 ? 0xC0 : count == 3 ? 0xE0 : 0xF0) | (cp >> (6 * (count - 1)))));
                for (unsigned shift = 6 * (count - 1); shift; shift -= 6)
                    out.push_back(static_cast<char>(0x80 | ((cp >> (shift - 6)) & 0x3F)));
            }
            i = end + 1;
        }
        return out;
    }
    return {};
}

inline std::vector<std::string> readerTableCells(const std::string &row)
{
    std::vector<std::string> cells;
    size_t start = row.find(TABLE_CELL);
    while (start != std::string::npos) {
        start += std::strlen(TABLE_CELL);
        const size_t end = row.find(TABLE_CELL, start);
        cells.push_back(row.substr(start, end == std::string::npos ? end : end - start));
        start = end;
    }
    return cells;
}

inline size_t readerBulletLengthAt(const std::string &text, size_t pos)
{
    if (pos >= text.size())
        return 0;
    if (text.size() - pos >= 3) {
        const auto a = static_cast<uint8_t>(text[pos]);
        const auto b = static_cast<uint8_t>(text[pos + 1]);
        const auto c = static_cast<uint8_t>(text[pos + 2]);
        if (a == 0xE2U && ((b == 0x80U && (c == 0xA2U || c == 0xA3U)) || (b == 0x97U && (c == 0x8FU || c == 0xA6U))))
            return 3;
    }
    if (text[pos] != '&')
        return 0;
    for (const char *entity : {"&bull;", "&bullet;", "&#8226;", "&#x2022;"}) {
        const size_t length = std::strlen(entity);
        if (text.compare(pos, length, entity) == 0)
            return length;
    }
    return 0;
}

inline bool readerParagraphStartsWithBullet(const std::string &text, size_t pos)
{
    while (pos < text.size()) {
        if (text[pos] == '<') {
            const size_t end = readerTagEnd(text, pos + 1);
            if (end == std::string::npos || text[pos + 1] == '/')
                return false;
            pos = end + 1;
        } else if (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n') {
            ++pos;
        } else {
            return readerBulletLengthAt(text, pos) != 0;
        }
    }
    return false;
}

inline size_t readerSpaceLengthAt(const std::string &text, size_t pos)
{
    if (pos >= text.size())
        return 0;
    const size_t remaining = text.size() - pos;
    if (remaining >= 2 && static_cast<uint8_t>(text[pos]) == 0xC2U && static_cast<uint8_t>(text[pos + 1]) == 0xA0U)
        return 2;
    if (remaining < 3)
        return 0;
    const auto a = static_cast<uint8_t>(text[pos]);
    const auto b = static_cast<uint8_t>(text[pos + 1]);
    const auto c = static_cast<uint8_t>(text[pos + 2]);
    if ((a == 0xE2U && b == 0x80U && ((c >= 0x80U && c <= 0x8AU) || c == 0xAFU)) || (a == 0xE2U && b == 0x81U && c == 0x9FU) ||
        (a == 0xE3U && b == 0x80U && c == 0x80U))
        return 3;
    return 0;
}

inline void appendHtmlBlockBreak(std::string &text, bool compact)
{
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    if (text.empty())
        return;
    const bool endsWithBreak = text.back() == '\n';
    if (compact && endsWithBreak)
        return;
    if (endsWithBreak && text.size() > 1 && text[text.size() - 2] == '\n')
        return;
    text.push_back('\n');
}
} // namespace NicheGraphics::InkHUD
