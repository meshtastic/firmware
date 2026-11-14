#pragma once

#include <cstdint>
#include <string>

namespace WaypointUtils
{

inline std::string utf8FromCodepoint(uint32_t codepoint)
{
    if (codepoint == 0 || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
        return "";

    char buf[4];
    if (codepoint <= 0x7F) {
        buf[0] = static_cast<char>(codepoint);
        return std::string(buf, 1);
    }
    if (codepoint <= 0x7FF) {
        buf[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        buf[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 2);
    }
    if (codepoint <= 0xFFFF) {
        buf[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return std::string(buf, 3);
    }

    buf[0] = static_cast<char>(0xF0 | (codepoint >> 18));
    buf[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
    return std::string(buf, 4);
}

} // namespace WaypointUtils
