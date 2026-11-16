#include "configuration.h"
#if HAS_SCREEN
#include "OEMBootImage.h"

#include "DebugConfiguration.h"
#include "FSCommon.h"
#include "SPILock.h"
#include <stdlib.h>
#include <string.h>

namespace graphics
{
namespace OEMBootImage
{
namespace
{
const char *fileName = "/prefs/oemboot";
constexpr uint32_t OEMBOOT_MAGIC = 0x424D454F; // "OEMB" little-endian
constexpr uint8_t OEMBOOT_VERSION = 1;
constexpr uint16_t OEMBOOT_MAX_DIM = 320;
constexpr size_t OEMBOOT_MAX_BYTES = 8192;

struct __attribute__((packed)) Header {
    uint32_t magic;
    uint8_t version;
    uint16_t width;
    uint16_t height;
    uint8_t fontSize;
    char text[32];
};

Image image{};
bool loaded = false;
} // namespace

bool load()
{
    release();
#ifdef FSCom
    spiLock->lock();
    auto file = FSCom.open(fileName, FILE_O_READ);
    if (!file) {
        spiLock->unlock();
        return false;
    }

    Header header{};
    bool ok = file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) && header.magic == OEMBOOT_MAGIC &&
              header.version == OEMBOOT_VERSION && header.width > 0 && header.height > 0 && header.width <= OEMBOOT_MAX_DIM &&
              header.height <= OEMBOOT_MAX_DIM;
    size_t bytes = ok ? (size_t)((header.width + 7) / 8) * header.height : 0;
    if (ok && bytes > OEMBOOT_MAX_BYTES)
        ok = false;

    uint8_t *buf = ok ? (uint8_t *)malloc(bytes) : nullptr;
    if (buf && file.read(buf, bytes) != bytes) {
        free(buf);
        buf = nullptr;
    }
    file.close();
    spiLock->unlock();

    if (!buf) {
        LOG_WARN("OEM boot image %s invalid, ignored", fileName);
        return false;
    }

    image.width = header.width;
    image.height = header.height;
    image.fontSize = header.fontSize;
    memcpy(image.text, header.text, sizeof(header.text));
    image.text[sizeof(image.text) - 1] = '\0';
    image.xbm = buf;
    loaded = true;
    LOG_INFO("Loaded OEM boot image %ux%u", image.width, image.height);
    return true;
#else
    return false;
#endif
}

const Image *get()
{
    return loaded ? &image : nullptr;
}

void release()
{
    if (image.xbm)
        free(image.xbm);
    image = Image{};
    loaded = false;
}

} // namespace OEMBootImage
} // namespace graphics
#endif
