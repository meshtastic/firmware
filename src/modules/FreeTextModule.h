#pragma once
#if HAS_SCREEN

#include "configuration.h"
#include <utility>
#include <vector>

class OLEDDisplay;

namespace freeTextModule
{
std::vector<std::pair<bool, String>> tokenizeMessageWithEmotes(const char *msg);
void renderEmote(OLEDDisplay *display, int &nextX, int lineY, int rowHeight, const String &label);
} // namespace freeTextModule

#endif
