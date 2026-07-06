#pragma once

#include "MessageStore.h"

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
namespace graphics
{
namespace MessageStatusText
{

const char *inlineTextFor(const StoredMessage &message);
const char *bannerTextFor(const StoredMessage &message);

} // namespace MessageStatusText
} // namespace graphics
#endif
