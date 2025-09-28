#pragma once
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <deque>
#include <string>

// Max number of messages we’ll keep in history
constexpr size_t MAX_MESSAGES_SAVED = 20;
constexpr size_t MAX_MESSAGE_SIZE = 220; // safe bound for text payload

// Explicit message classification
enum class MessageType : uint8_t { BROADCAST = 0, DM_TO_US = 1 };

// Delivery status for messages we sent
enum class AckStatus : uint8_t {
    NONE = 0,    // just sent, waiting (no symbol shown)
    ACKED = 1,   // got a valid ACK from destination
    NACKED = 2,  // explicitly failed
    TIMEOUT = 3, // no ACK after retry window
    RELAYED = 4  // got an ACK from relay, not destination
};

struct StoredMessage {
    uint32_t timestamp;   // When message was created (secs since boot/RTC)
    uint32_t sender;      // NodeNum of sender
    uint8_t channelIndex; // Channel index used
    std::string text;     // UTF-8 text payload

    // Destination node.
    // 0xffffffff (NODENUM_BROADCAST) means broadcast,
    // otherwise this is the NodeNum of the DM recipient.
    uint32_t dest;

    // Explicit classification (derived from dest when loading old messages)
    MessageType type;

    // Marks whether the timestamp was stored relative to boot time
    // (true = millis()/1000 fallback, false = epoch/RTC absolute)
    bool isBootRelative;

    // Delivery status (only meaningful for our own sent messages)
    AckStatus ackStatus;

    // Default constructor to initialize all fields safely
    StoredMessage()
        : timestamp(0), sender(0), channelIndex(0), text(""), dest(0xffffffff), type(MessageType::BROADCAST),
          isBootRelative(false), ackStatus(AckStatus::NONE) // start as NONE (waiting, no symbol)
    {
    }
};

class MessageStore
{
  public:
    explicit MessageStore(const std::string &label);

    // Live RAM methods (always current, used by UI and runtime)
    void addLiveMessage(const StoredMessage &msg);
    const std::deque<StoredMessage> &getLiveMessages() const { return liveMessages; }

    // Persistence methods (used only on boot/shutdown)
    void addMessage(const StoredMessage &msg);
    const StoredMessage &addFromPacket(const meshtastic_MeshPacket &mp); // Incoming/outgoing → RAM only
    void addFromString(uint32_t sender, uint8_t channelIndex, const std::string &text);
    void saveToFlash();
    void loadFromFlash();

    // Clear all messages (RAM + persisted queue)
    void clearAllMessages();

    // Dismiss helpers
    void dismissOldestMessage();
    void dismissNewestMessage();

    // New targeted dismiss helpers
    void dismissOldestMessageInChannel(uint8_t channel);
    void dismissOldestMessageWithPeer(uint32_t peer);

    // Unified accessor (for UI code, defaults to RAM buffer)
    const std::deque<StoredMessage> &getMessages() const { return liveMessages; }

    // Optional: direct access to persisted copy (mainly for debugging/inspection)
    const std::deque<StoredMessage> &getPersistedMessages() const { return messages; }

    // Helper filters for future use
    std::deque<StoredMessage> getChannelMessages(uint8_t channel) const;
    std::deque<StoredMessage> getDirectMessages() const;
    std::deque<StoredMessage> getConversationWith(uint32_t peer) const;

    // Upgrade boot-relative timestamps once RTC is valid
    void upgradeBootRelativeTimestamps();

  private:
    std::deque<StoredMessage> liveMessages;
    std::deque<StoredMessage> messages; // persisted copy
    std::string filename;
};

// Global instance (defined in MessageStore.cpp)
extern MessageStore messageStore;
