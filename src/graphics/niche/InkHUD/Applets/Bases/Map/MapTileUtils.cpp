#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "MapTileUtils.h"

#include <string.h>

namespace NicheGraphics::InkHUD::MapTileUtils
{

int decompress(const uint8_t *source, size_t sourceLength, uint8_t *destination, size_t destinationCapacity)
{
    if (!source || !destination)
        return -1;

    const uint8_t *sourceEnd = source + sourceLength;
    uint8_t *destinationEnd = destination + destinationCapacity;
    uint8_t *output = destination;

    while (source < sourceEnd) {
        const uint8_t token = *source++;
        size_t literalLength = (token >> 4) & 0x0F;
        if (literalLength == 15) {
            uint8_t extension;
            do {
                if (source >= sourceEnd)
                    return -1;
                extension = *source++;
                literalLength += extension;
            } while (extension == 255);
        }

        if (literalLength > sourceEnd - source || literalLength > destinationEnd - output)
            return -1;
        memcpy(output, source, literalLength);
        output += literalLength;
        source += literalLength;
        if (source >= sourceEnd)
            break;
        if (sourceEnd - source < 2)
            return -1;

        const size_t offset = (size_t)source[0] | ((size_t)source[1] << 8);
        source += 2;
        if (offset == 0 || offset > static_cast<size_t>(output - destination))
            return -1;

        size_t matchLength = (token & 0x0F) + 4;
        if (matchLength == 19) {
            uint8_t extension;
            do {
                if (source >= sourceEnd)
                    return -1;
                extension = *source++;
                matchLength += extension;
            } while (extension == 255);
        }
        if (matchLength > destinationEnd - output)
            return -1;

        const uint8_t *match = output - offset;
        for (size_t i = 0; i < matchLength; ++i)
            *output++ = match[i];
    }

    return static_cast<int>(output - destination);
}

} // namespace NicheGraphics::InkHUD::MapTileUtils

#endif
