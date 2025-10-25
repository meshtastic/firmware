#pragma once
#include "MessageStore.h" // for StoredMessage
#if HAS_SCREEN
#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"
#include "graphics/emotes.h"
#include "mesh/generated/meshtastic/mesh.pb.h" // for meshtastic_MeshPacket
#include <string>
#include <vector>

namespace graphics
{
namespace MessageRenderer
{

// Thread filter modes
enum class ThreadMode { ALL, CHANNEL, DIRECT };

// Setter for switching thread mode
void setThreadMode(ThreadMode mode, int channel = -1, uint32_t peer = 0);

// Getter for current mode
ThreadMode getThreadMode();

// Getter for current channel (valid if mode == CHANNEL)
int getThreadChannel();

// Getter for current peer (valid if mode == DIRECT)
uint32_t getThreadPeer();

// Registry accessors for menuHandler
const std::vector<int> &getSeenChannels();
const std::vector<uint32_t> &getSeenPeers();

void clearThreadRegistries();

// Text and emote rendering
void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount);

/// Draws the text message frame for displaying received messages
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Function to generate lines with word wrapping
std::vector<std::string> generateLines(OLEDDisplay *display, const char *headerStr, const char *messageBuf, int textWidth);

// Function to calculate heights for each line
std::vector<int> calculateLineHeights(const std::vector<std::string> &lines, const Emote *emotes,
                                      const std::vector<bool> &isHeaderVec);

// Reset scroll state when new messages arrive
void resetScrollState();

// Helper to auto-select the correct thread mode from a message
void setThreadFor(const StoredMessage &sm, const meshtastic_MeshPacket &packet);

// Handles a new incoming/outgoing message: banner, wake, thread select, scroll reset
void handleNewMessage(OLEDDisplay *display, const StoredMessage &sm, const meshtastic_MeshPacket &packet);

// Clear Message Line Cache from Message Renderer
void clearMessageCache();

} // namespace MessageRenderer
} // namespace graphics
#endif