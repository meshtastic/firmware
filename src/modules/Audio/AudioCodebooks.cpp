#include "configuration.h"

#if defined(HAS_AUDIO_CODEC2) && !MESHTASTIC_EXCLUDE_AUDIO

#include "AudioCodebooks.h"

#if !defined(CODEC2_EXTERNAL_CODEBOOKS)

// Tables are linked into the image; nothing to bind.
const char *audioCodebooksBind()
{
    return nullptr;
}

#elif !defined(ARCH_NRF52)

const char *audioCodebooksBind()
{
    return "external codebooks unsupported on this architecture";
}

#endif

#endif
