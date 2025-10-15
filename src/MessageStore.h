#pragma once

#if HAS_SCREEN

// Disable debug logging entirely on release builds of HELTEC_MESH_SOLAR for space constraints
#if defined(HELTEC_MESH_SOLAR)
#define LOG_DEBUG(...)
#endif

// Enable or disable message persistence (flash storage)
// Define -DENABLE_MESSAGE_PERSISTENCE=0 in build_flags to disable it entirely
#ifndef ENABLE_MESSAGE_PERSISTENCE
#define ENABLE_MESSAGE_PERSISTENCE 1
#endif

#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <deque>
#include <string>

// How many messages are stored (RAM + flash).
// Define -DMESSAGE_HISTORY_LIMIT=N in build_flags to control memory usage.
#ifndef MESSAGE_HISTORY_LIMIT
#define MESSAGE_HISTORY_LIMIT 20
#endif

// Internal alias used everywhere in code – do NOT redefine elsewhere.
#define MAX_MESSAGES_SAVED MESSAGE_HISTORY_LIMIT

// Maximum text payload size per message in bytes.
// This still defines the max message length, but we no longer reserve this space per message.
#define MAX_MESSAGE_SIZE 220

// Total shared text pool size for all messages combined.
// The text pool is RAM-only. Text is re-stored from flash into the pool on boot.
#ifndef MESSAGE_TEXT_POOL_SIZE
#define MESSAGE_TEXT_POOL_SIZE (MAX_MESSAGES_SAVED * MAX_MESSAGE_SIZE)
#endif

// Explicit message classification
enum class MessageType : uint8_t {
    BROADCAST = 0, // broadcast message
    DM_TO_US = 1   // direct message addressed to this node
};

// Delivery status for messages we sent
enum class AckStatus : uint8_t {
    NONE = 0,    // just sent, waiting (no symbol shown)
    ACKED = 1,   // got a valid ACK from destination
    NACKED = 2,  // explicitly failed
    TIMEOUT = 3, // no ACK after retry window
    RELAYED = 4  // got an ACK from relay, not destination
};

struct StoredMessage {
    uint32_t timestamp;   // When message was created (secs since boot or RTC)
    uint32_t sender;      // NodeNum of sender
    uint8_t channelIndex; // Channel index used
    uint32_t dest;        // Destination node (broadcast or direct)
    MessageType type;     // Derived from dest (explicit classification)
    bool isBootRelative;  // true = millis()/1000 fallback; false = epoch/RTC absolute
    AckStatus ackStatus;  // Delivery status (only meaningful for our own sent messages)

    // Text storage metadata — rebuilt from flash at boot
    uint16_t textOffset; // Offset into global text pool (valid only after loadFromFlash())
    uint16_t textLength; // Length of text in bytes

    // Default constructor initializes all fields safely
    StoredMessage()
        : timestamp(0), sender(0), channelIndex(0), dest(0xffffffff), type(MessageType::BROADCAST), isBootRelative(false),
          ackStatus(AckStatus::NONE), textOffset(0), textLength(0)
    {
    }
};

class MessageStore
{
  public:
    explicit MessageStore(const std::string &label);

    // Live RAM methods (always current, used by UI and runtime)
    void addLiveMessage(StoredMessage &&msg);
    void addLiveMessage(const StoredMessage &msg); // convenience overload
    const std::deque<StoredMessage> &getLiveMessages() const { return liveMessages; }

    // Add new messages from packets or manual input
    const StoredMessage &addFromPacket(const meshtastic_MeshPacket &mp);                // Incoming/outgoing → RAM only
    void addFromString(uint32_t sender, uint8_t channelIndex, const std::string &text); // Manual add

    // Persistence methods (used only on boot/shutdown)
    void saveToFlash();   // Save messages to flash
    void loadFromFlash(); // Load messages from flash

    // Clear all messages (RAM + persisted queue + text pool)
    void clearAllMessages();

    // Dismiss helpers
    void dismissOldestMessage(); // remove oldest from RAM (and flash on save)

    // New targeted dismiss helpers
    void dismissOldestMessageInChannel(uint8_t channel);
    void dismissOldestMessageWithPeer(uint32_t peer);

    // Unified accessor (for UI code, defaults to RAM buffer)
    const std::deque<StoredMessage> &getMessages() const { return liveMessages; }

    // Helper filters for future use
    std::deque<StoredMessage> getChannelMessages(uint8_t channel) const; // Only broadcast messages on a channel
    std::deque<StoredMessage> getDirectMessages() const;                 // Only direct messages

    // Upgrade boot-relative timestamps once RTC is valid
    void upgradeBootRelativeTimestamps();

    // Retrieve the C-string text for a stored message
    static const char *getText(const StoredMessage &msg);

    // Allocate text into pool (used by sender-side code)
    static uint16_t storeText(const char *src, size_t len);

    // Used when loading from flash to rebuild the text pool
    static uint16_t rebuildTextFromFlash(const char *src, size_t len);

  private:
    std::deque<StoredMessage> liveMessages; // Single in-RAM message buffer (also used for persistence)
    std::string filename;                   // Flash filename for persistence
};

// Global instance (defined in MessageStore.cpp)
extern MessageStore messageStore;

#endif
