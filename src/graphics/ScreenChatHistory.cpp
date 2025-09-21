#include "graphics/ScreenChatHistory.h"
#include "modules/ChatHistoryStore.h"  // <-- REQUIRED
#include <cstdio>
#include <algorithm>
#include <string>

using chat::ChatHistoryStore;

namespace graphics {
namespace chatui {

int ScreenChatHistory::visibleLines() {
  int lh = DisplayIface::lineHeight();
  if (lh <= 0) lh = 10;
  // leave 1 line for header
  int vis = (DisplayIface::height() - lh) / lh;
  if (vis < 1) vis = 1;
  return vis;
}

void ScreenChatHistory::enterPicker(Mode m) {
  picker_.mode = m;
  picker_.cursor = picker_.first = 0;
  picker_.peers.clear();
  picker_.chans.clear();
  if (m == Mode::ByNode)
    picker_.peers = ChatHistoryStore::instance().listDMPeers();
  else
    picker_.chans = ChatHistoryStore::instance().listChannels();
}

void ScreenChatHistory::clampList(int total, int &cursor, int &first, int vis) {
  if (total <= 0) { cursor = first = 0; return; }
  if (cursor < 0) cursor = 0;
  if (cursor >= total) cursor = total - 1;
  if (first > cursor) first = cursor;
  if (cursor >= first + vis) first = cursor - vis + 1;
  if (first < 0) first = 0;
}

std::string ScreenChatHistory::peerName(uint32_t nodeId) {
  // Format node ID as a string for display
  char buf[32];
  std::snprintf(buf, sizeof(buf), "Node %08X", (unsigned)nodeId);
  return std::string(buf);
}

std::string ScreenChatHistory::chanName(uint8_t ch) {
  // Format channel index as a string for display
  char buf[24];
  std::snprintf(buf, sizeof(buf), "Channel %u", (unsigned)ch);
  return std::string(buf);
}

void ScreenChatHistory::renderPicker() {
  // Clear the display before rendering the picker
  DisplayIface::clear();
  // Calculate how many lines can be shown on the display
  const int vis = visibleLines();

  // Draw the header for the picker (nodes or channels)
  DisplayIface::drawText(0, 0,
    picker_.mode == Mode::ByNode ? "Chat history: Nodes" : "Chat history: Channels",
    true);

  // Start drawing below the header
  int y = DisplayIface::lineHeight();
  // Render the list of nodes or channels depending on picker mode
  if (picker_.mode == Mode::ByNode) {
    int total = (int)picker_.peers.size();
  // Clamp the cursor and first index to valid range
  clampList(total, picker_.cursor, picker_.first, vis);
  // Draw each visible node entry
  for (int i=0; i<vis && (picker_.first+i)<total; ++i) {
  auto id = picker_.peers[picker_.first + i];
  auto line = peerName(id); // Get display name for node
  DisplayIface::drawText(0, y + i*DisplayIface::lineHeight(), line.c_str(),
             (picker_.first+i)==picker_.cursor); // Highlight if selected
    }
  } else {
    int total = (int)picker_.chans.size();
  // Clamp the cursor and first index to valid range
  clampList(total, picker_.cursor, picker_.first, vis);
  // Draw each visible channel entry
  for (int i=0; i<vis && (picker_.first+i)<total; ++i) {
  auto ch = picker_.chans[picker_.first + i];
  auto line = chanName(ch); // Get display name for channel
  DisplayIface::drawText(0, y + i*DisplayIface::lineHeight(), line.c_str(),
             (picker_.first+i)==picker_.cursor); // Highlight if selected
    }
  }
}

void ScreenChatHistory::handlePickerUp()   { picker_.cursor--; }
void ScreenChatHistory::handlePickerDown() { picker_.cursor++; }

bool ScreenChatHistory::handlePickerSelect() {
  if (picker_.mode == Mode::ByNode) {
    if (picker_.peers.empty()) return false;
    detail_.isChannel = false;
    detail_.node = picker_.peers[picker_.cursor];
  } else {
    if (picker_.chans.empty()) return false;
    detail_.isChannel = true;
    detail_.channel = picker_.chans[picker_.cursor];
  }
  detail_.cursor = detail_.first = 0;
  return true;
}

void ScreenChatHistory::renderDetail() {
  // Clear the display before rendering chat details
  DisplayIface::clear();
  // Get the chat history for the selected node or channel
  auto& store = ChatHistoryStore::instance();
  const auto& q = detail_.isChannel ? store.getCHAN(detail_.channel) : store.getDM(detail_.node);

  // Calculate how many lines can be shown on the display
  const int vis = visibleLines();
  // Draw the header for the chat detail (node or channel)
  auto title = detail_.isChannel ? ("Chan " + chanName(detail_.channel)) : peerName(detail_.node);
  DisplayIface::drawText(0, 0, title.c_str(), true);

  // Clamp the cursor and first index to valid range
  int total = (int)q.size();
  clampList(total, detail_.cursor, detail_.first, vis);
  // Start drawing below the header
  int y = DisplayIface::lineHeight();

  // Draw each visible chat entry with direction prefix
  for (int i=0; i<vis && (detail_.first+i)<total; ++i) {
    const auto& e = q[detail_.first + i];
    char prefix[4];
    std::snprintf(prefix, sizeof(prefix), "%s ", e.outgoing ? ">" : "<"); // '>' for sent, '<' for received
    std::string line = std::string(prefix) + e.text;
    DisplayIface::drawText(0, y + i*DisplayIface::lineHeight(), line.c_str(),
                           (detail_.first+i)==detail_.cursor); // Highlight if selected
  }
}

void ScreenChatHistory::handleDetailUp()   { detail_.cursor--; }
void ScreenChatHistory::handleDetailDown() { detail_.cursor++; }

} // namespace chatui
} // namespace graphics
