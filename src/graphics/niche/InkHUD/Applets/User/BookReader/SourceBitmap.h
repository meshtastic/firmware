#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if INKHUD_BOOKREADER_GRAYSCALE && defined(ARCH_ESP32) && defined(__GNUC__)
// Pixel conversion favors speed; restore the firmware's size optimization after these helpers.
#pragma GCC push_options
#pragma GCC optimize("O3")
#endif

namespace NicheGraphics::InkHUD
{
inline size_t readerBitmapStride(uint16_t width)
{
#if INKHUD_BOOKREADER_GRAYSCALE
    return (static_cast<size_t>(width) + 1U) / 2U;
#else
    return (static_cast<size_t>(width) + 7U) / 8U;
#endif
}

#if INKHUD_BOOKREADER_GRAYSCALE
// Reader caches store ink coverage: zero is white, fifteen is black.
inline constexpr uint8_t readerInkFromLuminance(unsigned luminance)
{
    // Blend linear and quadratic luminance to deepen midtones without clipping the endpoints.
    const unsigned adjusted = (luminance * (1275U + 3U * luminance) + 1020U) / 2040U;
    return 15U - (adjusted * 15U + 127U) / 255U;
}

inline constexpr auto readerInkLookup = [] {
    std::array<uint8_t, 256> ink{};
    for (unsigned luminance = 0; luminance < ink.size(); ++luminance)
        ink[luminance] = readerInkFromLuminance(luminance);
    return ink;
}();

// Common scaling footprints contain at most sixteen source pixels.
inline constexpr auto readerAverageInkLookup = [] {
    std::array<std::array<uint8_t, 241>, 16> averages{};
    for (unsigned samples = 1; samples <= averages.size(); ++samples)
        for (unsigned sum = 0; sum <= samples * 15U; ++sum)
            averages[samples - 1U][sum] = (sum + samples / 2U) / samples;
    return averages;
}();

inline uint8_t readerGrayAt(const uint8_t *data, size_t stride, uint16_t x, uint16_t y)
{
    return (data[y * stride + x / 2U] >> ((x & 1U) ? 0U : 4U)) & 15U;
}

inline unsigned readerShortGraySpanSum(const uint8_t *row, uint16_t left, uint16_t span)
{
    const uint8_t pair = row[left / 2U];
    unsigned ink = (pair >> ((left & 1U) ? 0U : 4U)) & 15U;
    if (span >= 2U)
        ink += (left & 1U) ? row[left / 2U + 1U] >> 4U : pair & 15U;
    if (span == 3U)
        ink += (row[left / 2U + 1U] >> ((left & 1U) ? 0U : 4U)) & 15U;
    return ink;
}

inline void setReaderGray(uint8_t *data, size_t stride, uint16_t x, uint16_t y, uint8_t ink)
{
    uint8_t &byte = data[y * stride + x / 2U];
    const unsigned shift = (x & 1U) ? 0U : 4U;
    byte = (byte & ~(15U << shift)) | ((ink & 15U) << shift);
}

inline void copyReaderGrayBitmap(const uint8_t *source, size_t sourceStride, uint16_t sourceWidth, uint16_t sourceHeight,
                                 uint8_t *destination, size_t destinationStride, uint16_t destinationWidth,
                                 uint16_t destinationHeight, int16_t left, int16_t top)
{
    const int32_t firstX = std::max<int32_t>(0, -static_cast<int32_t>(left));
    const int32_t firstY = std::max<int32_t>(0, -static_cast<int32_t>(top));
    const int32_t endX = std::min<int32_t>(sourceWidth, static_cast<int32_t>(destinationWidth) - left);
    const int32_t endY = std::min<int32_t>(sourceHeight, static_cast<int32_t>(destinationHeight) - top);
    if (firstX >= endX || firstY >= endY)
        return;
    for (int32_t y = firstY; y < endY; ++y) {
        const uint8_t *row = source + y * sourceStride;
        uint8_t *out = destination + (top + y) * destinationStride + (left + firstX) / 2U;
        int32_t x = firstX;
        if ((left + x) & 1U) {
            *out = (*out & 0xF0U) | readerGrayAt(row, sourceStride, x, 0);
            ++out;
            ++x;
        }
        const int32_t pairs = (endX - x) / 2;
        const uint8_t *in = row + x / 2U;
        if (!(x & 1U)) {
            std::memcpy(out, in, pairs);
            out += pairs;
        } else {
            // Shift adjacent bytes when source and destination nibble alignment differs.
            for (int32_t i = 0; i < pairs; ++i)
                *out++ = static_cast<uint8_t>((in[i] << 4U) | (in[i + 1] >> 4U));
        }
        x += pairs * 2;
        if (x < endX)
            *out = (*out & 0x0FU) | (readerGrayAt(row, sourceStride, x, 0) << 4U);
    }
}
#endif

inline bool rasterizeInlineRgb565Block(const uint16_t *pixels, int x, int y, int width, int height, int pixelStride, uint16_t sw,
                                       uint16_t sh, uint16_t dw, uint16_t dh, std::vector<uint8_t> &destination)
{
    const size_t stride = readerBitmapStride(dw);
    if (!pixels || x < 0 || y < 0 || width <= 0 || height <= 0 || pixelStride < width || !sw || !sh || !dw || !dh ||
        destination.size() < stride * dh)
        return false;
    if (x >= sw || y >= sh)
        return true;
    const uint32_t xEnd = static_cast<uint32_t>(x) + std::min<int>(width, sw - x);
    const uint32_t yEnd = static_cast<uint32_t>(y) + std::min<int>(height, sh - y);
    const uint32_t left = (static_cast<uint32_t>(x) * dw + sw - 1U) / sw;
    const uint32_t top = (static_cast<uint32_t>(y) * dh + sh - 1U) / sh;
    const uint32_t right = (xEnd * dw + sw - 1U) / sw;
    const uint32_t bottom = (yEnd * dh + sh - 1U) / sh;
#if !INKHUD_BOOKREADER_GRAYSCALE
    // Dither after scaling, so gray detail survives reduction.
    constexpr uint8_t bayer[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
#endif
    for (uint32_t dy = top; dy < bottom; ++dy) {
        const uint16_t *row = pixels + (dy * sh / dh - y) * pixelStride;
#if !INKHUD_BOOKREADER_GRAYSCALE
        uint8_t *out = destination.data() + dy * stride;
#endif
        for (uint32_t dx = left; dx < right; ++dx) {
            const uint16_t rgb = row[dx * sw / dw - x];
#if INKHUD_BOOKREADER_GRAYSCALE
            const unsigned r = (rgb >> 11) & 31U, g = (rgb >> 5) & 63U, b = rgb & 31U;
            const unsigned luminance =
                (((r << 3U) | (r >> 2U)) * 38U + ((g << 2U) | (g >> 4U)) * 75U + ((b << 3U) | (b >> 2U)) * 15U + 64U) >> 7U;
            setReaderGray(destination.data(), stride, dx, dy, readerInkLookup[luminance]);
#else
            const unsigned luminance = (((rgb >> 11) & 31U) * 38U + ((rgb >> 5) & 63U) * 75U + (rgb & 31U) * 15U) / 25U;
            const unsigned threshold = bayer[(dy & 3U) * 4U + (dx & 3U)] * 16U + 8U;
            const uint8_t mask = 0x80U >> (dx & 7U);
            if (luminance < threshold)
                out[dx / 8U] |= mask;
            else
                out[dx / 8U] &= static_cast<uint8_t>(~mask);
#endif
        }
    }
    return true;
}

inline bool packGraySourceBlock(const uint8_t *pixels, int x, int y, int width, int height, int pixelStride, uint16_t sourceWidth,
                                uint16_t sourceHeight, std::vector<uint8_t> &source)
{
    const size_t stride = readerBitmapStride(sourceWidth);
    if (!pixels || width <= 0 || height <= 0 || pixelStride < width || source.size() < stride * sourceHeight)
        return false;
#if INKHUD_BOOKREADER_GRAYSCALE
    const auto inkAt = [](uint8_t gray) { return readerInkLookup[gray]; };
    const int firstX = std::max(0, -x);
    const int endX = std::min(width, static_cast<int>(sourceWidth) - x);
    if (firstX >= endX)
        return true;
    for (int sy = std::max(0, -y); sy < height && y + sy < sourceHeight; ++sy) {
        uint8_t *out = source.data() + static_cast<size_t>(y + sy) * stride + (x + firstX) / 2U;
        const uint8_t *in = pixels + static_cast<size_t>(sy) * pixelStride + firstX;
        int remaining = endX - firstX;
        if ((x + firstX) & 1U) {
            *out = (*out & 0xF0U) | inkAt(*in++);
            ++out;
            --remaining;
        }
        for (; remaining >= 2; remaining -= 2, in += 2)
            *out++ = (inkAt(in[0]) << 4U) | inkAt(in[1]);
        if (remaining)
            *out = (*out & 0x0FU) | (inkAt(*in) << 4U);
    }
    return true;
#else
    if (x >= 0 && y >= 0 && (x & 7) == 0 && x + width <= sourceWidth && y + height <= sourceHeight) {
        const size_t firstByte = static_cast<size_t>(x) / 8U;
        for (int sy = 0; sy < height; ++sy) {
            uint8_t *out = source.data() + static_cast<size_t>(y + sy) * stride + firstByte;
            const uint8_t *in = pixels + static_cast<size_t>(sy) * pixelStride;
            int sx = 0;
            for (; sx + 8 <= width; sx += 8) {
                out[sx / 8] = static_cast<uint8_t>((in[sx] < 132 ? 0x80U : 0U) | (in[sx + 1] < 132 ? 0x40U : 0U) |
                                                   (in[sx + 2] < 132 ? 0x20U : 0U) | (in[sx + 3] < 132 ? 0x10U : 0U) |
                                                   (in[sx + 4] < 132 ? 0x08U : 0U) | (in[sx + 5] < 132 ? 0x04U : 0U) |
                                                   (in[sx + 6] < 132 ? 0x02U : 0U) | (in[sx + 7] < 132 ? 0x01U : 0U));
            }
            for (; sx < width; ++sx) {
                const uint8_t mask = 0x80U >> (sx & 7);
                if (in[sx] < 132)
                    out[sx / 8] |= mask;
                else
                    out[sx / 8] &= static_cast<uint8_t>(~mask);
            }
        }
        return true;
    }
    for (int sy = 0; sy < height && y + sy < sourceHeight; ++sy) {
        if (y + sy < 0)
            continue;
        uint8_t *row = source.data() + (y + sy) * stride;
        for (int sx = 0; sx < width && x + sx < sourceWidth; ++sx) {
            if (x + sx < 0)
                continue;
            const uint8_t mask = 0x80U >> ((x + sx) & 7);
            // Match the RGB565 threshold for grayscale samples.
            if (pixels[sy * pixelStride + sx] < 132)
                row[(x + sx) / 8] |= mask;
            else
                row[(x + sx) / 8] &= static_cast<uint8_t>(~mask);
        }
    }
    return true;
#endif
}

inline bool scaleSourceBitmap(const std::vector<uint8_t> &source, uint16_t sw, uint16_t sh, uint16_t dw, uint16_t dh,
                              std::vector<uint8_t> &destination)
{
    const size_t sourceStride = readerBitmapStride(sw);
    if (!sw || !sh || !dw || !dh || source.size() < sourceStride * sh || &source == &destination)
        return false;
    if (sw == dw && sh == dh) {
        destination.assign(source.begin(), source.begin() + sourceStride * sh);
        return true;
    }
    const size_t stride = readerBitmapStride(dw);
#if INKHUD_BOOKREADER_GRAYSCALE
    // Every output byte is assigned below, including an odd-width row's padding nibble.
    destination.resize(stride * dh);
    std::vector<uint16_t> columns(static_cast<size_t>(dw) + 1U);
    for (uint32_t x = 0; x <= dw; ++x)
        columns[x] = x * sw / dw;
    std::vector<uint16_t> spans(dw);
    for (uint32_t x = 0; x < dw; ++x)
        spans[x] = std::max<uint16_t>(1U, columns[x + 1U] - columns[x]);
    std::vector<uint32_t> sums(dw);
    uint32_t previousTop = UINT32_MAX, previousBottom = UINT32_MAX;
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t top = y * sh / dh;
        const uint32_t bottom = std::max(top + 1U, (y + 1U) * sh / dh);
        if (y && top == previousTop && bottom == previousBottom) {
            std::copy_n(destination.data() + (y - 1U) * stride, stride, destination.data() + y * stride);
            continue;
        }
        previousTop = top;
        previousBottom = bottom;
        if (sw <= 2U * dw && bottom == top + 1U) {
            uint8_t *out = destination.data() + y * stride;
            const auto ink = [&](uint32_t x) {
                const uint8_t first = readerGrayAt(source.data(), sourceStride, columns[x], top);
                if (spans[x] == 1U)
                    return first;
                const uint8_t second = readerGrayAt(source.data(), sourceStride, columns[x] + 1U, top);
                return static_cast<uint8_t>((first + second + 1U) >> 1U);
            };
            uint32_t x = 0;
            for (; x + 1U < dw; x += 2U)
                out[x / 2U] = (ink(x) << 4U) | ink(x + 1U);
            if (x < dw)
                out[x / 2U] = ink(x) << 4U;
            continue;
        }
        if (sw <= 2U * dw && bottom == top + 2U) {
            const uint8_t *firstRow = source.data() + top * sourceStride;
            const uint8_t *secondRow = firstRow + sourceStride;
            uint8_t *out = destination.data() + y * stride;
            const auto ink = [&](uint32_t x) {
                const uint32_t left = columns[x];
                const uint8_t first = firstRow[left / 2U], second = secondRow[left / 2U];
                const unsigned shift = (left & 1U) ? 0U : 4U;
                unsigned sum = ((first >> shift) & 15U) + ((second >> shift) & 15U);
                if (spans[x] == 1U)
                    return (sum + 1U) >> 1U;
                sum += (left & 1U) ? (firstRow[left / 2U + 1U] >> 4U) + (secondRow[left / 2U + 1U] >> 4U)
                                   : (first & 15U) + (second & 15U);
                return (sum + 2U) >> 2U;
            };
            uint32_t x = 0;
            for (; x + 1U < dw; x += 2U)
                out[x / 2U] = (ink(x) << 4U) | ink(x + 1U);
            if (x < dw)
                out[x / 2U] = ink(x) << 4U;
            continue;
        }
        if (sw <= 3U * dw && bottom <= top + 3U) {
            const uint8_t *firstRow = source.data() + top * sourceStride;
            const unsigned rows = bottom - top;
            uint8_t *out = destination.data() + y * stride;
            const auto ink = [&](uint32_t x) {
                const uint16_t left = columns[x], span = spans[x];
                unsigned sum = readerShortGraySpanSum(firstRow, left, span);
                if (rows >= 2U)
                    sum += readerShortGraySpanSum(firstRow + sourceStride, left, span);
                if (rows == 3U)
                    sum += readerShortGraySpanSum(firstRow + 2U * sourceStride, left, span);
                return readerAverageInkLookup[span * rows - 1U][sum];
            };
            uint32_t x = 0;
            for (; x + 1U < dw; x += 2U)
                out[x / 2U] = (ink(x) << 4U) | ink(x + 1U);
            if (x < dw)
                out[x / 2U] = ink(x) << 4U;
            continue;
        }
        std::fill(sums.begin(), sums.end(), 0U);
        // Read each source row sequentially instead of revisiting it for every destination pixel.
        for (uint32_t sy = top; sy < bottom; ++sy) {
            const uint8_t *row = source.data() + sy * sourceStride;
            for (uint32_t x = 0; x < dw; ++x) {
                const uint32_t left = columns[x];
                if (spans[x] <= 3U) {
                    sums[x] += readerShortGraySpanSum(row, left, spans[x]);
                    continue;
                }
                const uint32_t right = left + spans[x];
                uint32_t ink = 0;
                uint32_t sx = left;
                if (sx & 1U) {
                    ink += row[sx / 2U] & 15U;
                    ++sx;
                }
                for (; sx + 1U < right; sx += 2U) {
                    const uint8_t pair = row[sx / 2U];
                    ink += (pair >> 4U) + (pair & 15U);
                }
                if (sx < right)
                    ink += row[sx / 2U] >> 4U;
                sums[x] += ink;
            }
        }
        uint8_t *out = destination.data() + y * stride;
        const uint32_t rows = bottom - top;
        const auto average = [&](uint32_t x) {
            const uint32_t samples = spans[x] * rows;
            return samples <= readerAverageInkLookup.size() ? static_cast<uint32_t>(readerAverageInkLookup[samples - 1U][sums[x]])
                                                            : (sums[x] + samples / 2U) / samples;
        };
        uint32_t x = 0;
        for (; x + 1U < dw; x += 2U)
            out[x / 2U] = (average(x) << 4U) | average(x + 1U);
        if (x < dw)
            out[x / 2U] = average(x) << 4U;
    }
    return true;
#else
    destination.assign(stride * dh, 0);
    if (dw < sw || dh < sh) {
        // Preserve source-pixel coverage when reducing fine lines and screentones.
        constexpr uint8_t bayer[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        for (uint32_t y = 0; y < dh; ++y) {
            const uint32_t top = y * sh / dh;
            const uint32_t bottom = std::max(top + 1U, (y + 1U) * sh / dh);
            uint8_t *out = destination.data() + y * stride;
            for (uint32_t x = 0; x < dw; ++x) {
                const uint32_t left = x * sw / dw;
                const uint32_t right = std::max(left + 1U, (x + 1U) * sw / dw);
                uint32_t black = 0;
                for (uint32_t sy = top; sy < bottom; ++sy) {
                    const uint8_t *row = source.data() + sy * sourceStride;
                    for (uint32_t sx = left; sx < right; ++sx)
                        black += (row[sx / 8U] >> (7U - (sx & 7U))) & 1U;
                }
                const uint32_t samples = (right - left) * (bottom - top);
                const uint32_t threshold = bayer[(y & 3U) * 4U + (x & 3U)] * 2U + 1U;
                if (static_cast<uint64_t>(black) * 32U > static_cast<uint64_t>(samples) * threshold)
                    out[x / 8U] |= 0x80U >> (x & 7U);
            }
        }
        return true;
    }
    std::vector<uint16_t> columns(dw);
    for (uint32_t x = 0; x < dw; ++x)
        columns[x] = x * sw / dw;
    for (uint32_t y = 0; y < dh; ++y) {
        const uint8_t *row = source.data() + (y * sh / dh) * sourceStride;
        uint8_t *out = destination.data() + y * stride;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint16_t sx = columns[x];
            if (row[sx / 8] & (0x80U >> (sx & 7)))
                out[x / 8] |= 0x80U >> (x & 7);
        }
    }
    return true;
#endif
}
} // namespace NicheGraphics::InkHUD

#if INKHUD_BOOKREADER_GRAYSCALE && defined(ARCH_ESP32) && defined(__GNUC__)
#pragma GCC pop_options
#endif
