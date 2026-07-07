#pragma once

#include "MessageStore.h"

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) || defined(MESHTASTIC_INCLUDE_BASE_UI_MESSAGE_STATUS)
namespace graphics
{
namespace MessageStatusText
{

const char *inlineTextFor(const StoredMessage &message);
const char *bannerTextFor(const StoredMessage &message);
bool isFailureStatus(AckStatus status);

} // namespace MessageStatusText
} // namespace graphics
#endif
