#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <stdint.h>

namespace chat {

/**
 * Chat history entry.
 * - For DM: isChannel=false, 'node' is peer and 'channel' is 0.
 * - For Channel: isChannel=true, 'channel' is channel index and 'node' is sender (0 if not available).
 */
struct ChatEntry {
  uint32_t ts{0};           // epoch seconds
  bool     outgoing{false}; // true if you sent it from this node
  bool     isChannel{false}; // true=channel, false=DM by node
  bool     unread{false};   // true if the message has not been read
  uint32_t node{0};         // DM: peer; Channel: nodeId of sender (0 if not available)
  uint8_t  channel{0};      // valid if isChannel==true
  std::string text;         // UTF-8 renderable on OLED

  // Simple CSV serialization
  static std::string serialize(const ChatEntry& e);
  static ChatEntry deserialize(const std::string& line);
};

class ChatHistoryStore {
public:
  static ChatHistoryStore& instance();

  // Add messages
  void addDM(uint32_t peer, bool outgoing, const std::string& text, uint32_t ts, bool unread = true);
  void addCHAN(uint8_t channel, uint32_t fromNode, bool outgoing, const std::string& text, uint32_t ts, bool unread = true);

  // Read-only access to history (returns stable deque; empty if doesn't exist)
  const std::deque<ChatEntry>& getDM(uint32_t peer) const;
  const std::deque<ChatEntry>& getCHAN(uint8_t channel) const;

  // Management
  void clearDM(uint32_t peer);
  void clearCHAN(uint8_t channel);
  void removeByNode(uint32_t peer);     // delete entire DM conversation with that peer
  void removeChannel(uint8_t channel);  // delete entire channel history

  // New methods to remove complete history (RAM + persistent) but maintain channel/frame
  void clearChatHistoryDM(uint32_t peer);        // Remove only DM history, maintain the peer
  void clearChatHistoryChannel(uint8_t channel); // Remove only channel history, maintain channel/frame

  // Unread message management
  int getUnreadCountDM(uint32_t peer) const;     // Count unread messages from a specific DM
  int getUnreadCountCHAN(uint8_t channel) const; // Count unread messages from a specific channel
  int getTotalUnreadCount() const;               // Total count of unread messages
  void markAsReadDM(uint32_t peer);              // Mark all DM messages as read
  void markAsReadCHAN(uint8_t channel);          // Mark all channel messages as read
  void markAllAsRead();                          // Mark all messages as read
  void markMessageAsRead(uint32_t peer, int messageIndex); // Mark specific DM message as read
  void markChannelMessageAsRead(uint8_t channel, int messageIndex); // Mark specific channel message as read
  
  // Functions to position marquee on first unread message
  int getFirstUnreadIndexDM(uint32_t peer) const;       // Returns index of first unread message in DM (-1 if all read)
  int getFirstUnreadIndexCHAN(uint8_t channel) const;   // Returns index of first unread message in channel (-1 if all read)
  int getLastReadIndexDM(uint32_t peer) const;          // Returns index of last read message in DM (-1 if none read)  
  int getLastReadIndexCHAN(uint8_t channel) const;      // Returns index of last read message in channel (-1 if none read)

  // Listados
  std::vector<uint32_t> listDMPeers() const;
  std::vector<uint8_t>  listChannels() const;

  // Limit per conversation/channel
  static constexpr size_t kMaxPerGroup = 30;

private:
  ChatHistoryStore();
  static void pushBounded(std::deque<ChatEntry>& q, ChatEntry e);

  void saveDM(uint32_t peer);
  void loadDM(uint32_t peer);
  void saveCHAN(uint8_t channel);
  void loadCHAN(uint8_t channel);
  void saveAll();
  void loadAll();

  std::map<uint32_t, std::deque<ChatEntry>> dm_;
  std::map<uint8_t , std::deque<ChatEntry>> ch_;
};

} // namespace chat
