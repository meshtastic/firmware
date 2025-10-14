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

// Maximum text payload size per message in bytes (fixed).
// All messages use the same size to simplify memory handling and avoid dynamic allocations.
#define MAX_MESSAGE_SIZE 220

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

// A single stored message in RAM and/or flash
struct StoredMessage {
    uint32_t timestamp;          // When message was created (secs since boot or RTC)
    uint32_t sender;             // NodeNum of sender
    uint8_t channelIndex;        // Channel index used
    char text[MAX_MESSAGE_SIZE]; // Fixed-size buffer for message text (null-terminated)

    uint32_t dest; // Destination node (broadcast or direct)

    MessageType type; // Derived from dest (explicit classification)

    bool isBootRelative; // true = millis()/1000 fallback; false = epoch/RTC absolute

    AckStatus ackStatus; // Delivery status (only meaningful for our own sent messages)

    // Default constructor initializes all fields safely
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
    void addLiveMessage(StoredMessage &&msg);
    void addLiveMessage(const StoredMessage &msg); // convenience overload
    const std::deque<StoredMessage> &getLiveMessages() const { return liveMessages; }

    // Add new messages from packets or manual input
    const StoredMessage &addFromPacket(const meshtastic_MeshPacket &mp);                // Incoming/outgoing → RAM only
    void addFromString(uint32_t sender, uint8_t channelIndex, const std::string &text); // Manual add

    // Persistence methods (used only on boot/shutdown)
    void saveToFlash();   // Save messages to flash
    void loadFromFlash(); // Load messages from flash

    // Clear all messages (RAM + persisted queue)
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

  private:
    std::deque<StoredMessage> liveMessages; // Single in-RAM message buffer (also used for persistence)
    std::string filename;                   // Flash filename for persistence
};

// Global instance (defined in MessageStore.cpp)
extern MessageStore messageStore;

#endif
