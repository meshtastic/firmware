#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace NicheGraphics::InkHUD
{
inline int32_t readerPageCount(int32_t lines, int32_t step)
{
    return 1 + (std::max<int32_t>(1, lines) - 1) / std::max<int32_t>(1, step);
}
inline int32_t readerLastPageStart(int32_t lines, int32_t step)
{
    return (readerPageCount(lines, step) - 1) * std::max<int32_t>(1, step);
}
inline uint32_t readerPagePrefix(const std::vector<int32_t> &counts, size_t end)
{
    uint32_t pages = 0;
    if (end > counts.size())
        return 0;
    for (size_t i = 0; i < end; ++i) {
        if (counts[i] <= 0)
            return 0;
        pages += counts[i];
    }
    return pages;
}
} // namespace NicheGraphics::InkHUD
