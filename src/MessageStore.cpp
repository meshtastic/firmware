#include "configuration.h"
#if HAS_SCREEN
#include "FSCommon.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "gps/RTC.h"
#include "graphics/draw/MessageRenderer.h"
#include <cstring> // memcpy

// Helper: assign a timestamp (RTC if available, else boot-relative)
static inline void assignTimestamp(StoredMessage &sm)
{
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (nowSecs) {
        sm.timestamp = nowSecs;
        sm.isBootRelative = false;
    } else {
        sm.timestamp = millis() / 1000;
        sm.isBootRelative = true;
    }
}

// Generic push with cap (used by live + persisted queues)
template <typename T> static inline void pushWithLimit(std::deque<T> &queue, const T &msg)
{
    if (queue.size() >= MAX_MESSAGES_SAVED)
        queue.pop_front();
    queue.push_back(msg);
}

template <typename T> static inline void pushWithLimit(std::deque<T> &queue, T &&msg)
{
    if (queue.size() >= MAX_MESSAGES_SAVED)
        queue.pop_front();
    queue.emplace_back(std::move(msg));
}

MessageStore::MessageStore(const std::string &label)
{
    filename = "/Messages_" + label + ".msgs";
}

// Live message handling (RAM only)
void MessageStore::addLiveMessage(StoredMessage &&msg)
{
    pushWithLimit(liveMessages, std::move(msg));
}
void MessageStore::addLiveMessage(const StoredMessage &msg)
{
    pushWithLimit(liveMessages, msg);
}

// Add from incoming/outgoing packet
const StoredMessage &MessageStore::addFromPacket(const meshtastic_MeshPacket &packet)
{
    StoredMessage sm;
    assignTimestamp(sm); // set timestamp (RTC or boot-relative)
    sm.channelIndex = packet.channel;
    strncpy(sm.text, reinterpret_cast<const char *>(packet.decoded.payload.bytes), MAX_MESSAGE_SIZE - 1);
    sm.text[MAX_MESSAGE_SIZE - 1] = '\0';

    if (packet.from == 0) {
        // Outgoing (phone-originated)
        sm.sender = nodeDB->getNodeNum();
        sm.dest = (packet.decoded.dest == 0) ? NODENUM_BROADCAST : packet.decoded.dest;
        sm.type = (sm.dest == NODENUM_BROADCAST) ? MessageType::BROADCAST : MessageType::DM_TO_US;
        sm.ackStatus = AckStatus::NONE;
    } else {
        // Incoming
        sm.sender = packet.from;
        sm.dest = packet.decoded.dest;
        sm.type = (sm.dest == NODENUM_BROADCAST)      ? MessageType::BROADCAST
                  : (sm.dest == nodeDB->getNodeNum()) ? MessageType::DM_TO_US
                                                      : MessageType::BROADCAST;
        sm.ackStatus = AckStatus::ACKED;
    }

    addLiveMessage(sm);
    return liveMessages.back();
}

// Outgoing/manual message
void MessageStore::addFromString(uint32_t sender, uint8_t channelIndex, const std::string &text)
{
    StoredMessage sm;

    // Always use our local time (helper handles RTC vs boot time)
    assignTimestamp(sm);

    sm.sender = sender;
    sm.channelIndex = channelIndex;
    strncpy(sm.text, text.c_str(), MAX_MESSAGE_SIZE - 1);
    sm.text[MAX_MESSAGE_SIZE - 1] = '\0';

    // Default manual adds to broadcast
    sm.dest = NODENUM_BROADCAST;
    sm.type = MessageType::BROADCAST;

    // Outgoing messages start as NONE until ACK/NACK arrives
    sm.ackStatus = AckStatus::NONE;

    addLiveMessage(sm);
}

#if ENABLE_MESSAGE_PERSISTENCE

// Use a compile-time constant so the array bound can be used in the struct
static constexpr size_t TEXT_LEN = MAX_MESSAGE_SIZE;

// Compact, fixed-size on-flash representation
struct __attribute__((packed)) StoredMessageRecord {
    uint32_t timestamp;
    uint32_t sender;
    uint8_t channelIndex;
    uint32_t dest;
    uint8_t isBootRelative;
    uint8_t ackStatus;   // static_cast<uint8_t>(AckStatus)
    char text[TEXT_LEN]; // null-terminated
};

// Serialize one StoredMessage to flash
static inline void writeMessageRecord(SafeFile &f, const StoredMessage &m)
{
    StoredMessageRecord rec = {};
    rec.timestamp = m.timestamp;
    rec.sender = m.sender;
    rec.channelIndex = m.channelIndex;
    rec.dest = m.dest;
    rec.isBootRelative = m.isBootRelative;
    rec.ackStatus = static_cast<uint8_t>(m.ackStatus);

    strncpy(rec.text, m.text, TEXT_LEN - 1);
    rec.text[TEXT_LEN - 1] = '\0';
    f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
}

// Deserialize one StoredMessage from flash; returns false on short read
static inline bool readMessageRecord(File &f, StoredMessage &m)
{
    StoredMessageRecord rec = {};
    if (f.readBytes(reinterpret_cast<char *>(&rec), sizeof(rec)) != sizeof(rec))
        return false;

    m.timestamp = rec.timestamp;
    m.sender = rec.sender;
    m.channelIndex = rec.channelIndex;
    m.dest = rec.dest;
    m.isBootRelative = rec.isBootRelative;
    m.ackStatus = static_cast<AckStatus>(rec.ackStatus);
    strncpy(m.text, rec.text, MAX_MESSAGE_SIZE - 1);
    m.text[MAX_MESSAGE_SIZE - 1] = '\0';
    m.type = (m.dest == NODENUM_BROADCAST) ? MessageType::BROADCAST : MessageType::DM_TO_US;
    return true;
}

void MessageStore::saveToFlash()
{
#ifdef FSCom
    // Ensure root exists
    spiLock->lock();
    FSCom.mkdir("/");
    spiLock->unlock();

    SafeFile f(filename.c_str(), false);

    spiLock->lock();
    uint8_t count = static_cast<uint8_t>(liveMessages.size());
    if (count > MAX_MESSAGES_SAVED)
        count = MAX_MESSAGES_SAVED;
    f.write(&count, 1);

    for (uint8_t i = 0; i < count; ++i) {
        writeMessageRecord(f, liveMessages[i]);
    }
    spiLock->unlock();

    f.close();
#endif
}

void MessageStore::loadFromFlash()
{
    liveMessages.clear();
#ifdef FSCom
    concurrency::LockGuard guard(spiLock);

    if (!FSCom.exists(filename.c_str()))
        return;

    auto f = FSCom.open(filename.c_str(), FILE_O_READ);
    if (!f)
        return;

    uint8_t count = 0;
    f.readBytes(reinterpret_cast<char *>(&count), 1);
    if (count > MAX_MESSAGES_SAVED)
        count = MAX_MESSAGES_SAVED;

    for (uint8_t i = 0; i < count; ++i) {
        StoredMessage m;
        if (!readMessageRecord(f, m))
            break;
        liveMessages.push_back(m);
    }

    f.close();
#endif
}

#else
// If persistence is disabled, these functions become no-ops
void MessageStore::saveToFlash() {}
void MessageStore::loadFromFlash() {}
#endif

// Clear all messages (RAM + persisted queue)
void MessageStore::clearAllMessages()
{
    liveMessages.clear();

#ifdef FSCom
    SafeFile f(filename.c_str(), false);
    uint8_t count = 0;
    f.write(&count, 1); // write "0 messages"
    f.close();
#endif
}

// Internal helper: erase first or last message matching a predicate
template <typename Predicate> static void eraseIf(std::deque<StoredMessage> &deque, Predicate pred, bool fromBack = false)
{
    if (fromBack) {
        // Iterate from the back and erase the first match from the end
        for (auto it = deque.rbegin(); it != deque.rend(); ++it) {
            if (pred(*it)) {
                deque.erase(std::next(it).base());
                break;
            }
        }
    } else {
        // Manual forward search to avoid std::find_if
        auto it = deque.begin();
        for (; it != deque.end(); ++it) {
            if (pred(*it))
                break;
        }
        if (it != deque.end())
            deque.erase(it);
    }
}

// Dismiss oldest message (RAM + persisted queue)
void MessageStore::dismissOldestMessage()
{
    eraseIf(liveMessages, [](StoredMessage &) { return true; });
    saveToFlash();
}

// Dismiss oldest message in a specific channel
void MessageStore::dismissOldestMessageInChannel(uint8_t channel)
{
    auto pred = [channel](const StoredMessage &m) { return m.type == MessageType::BROADCAST && m.channelIndex == channel; };
    eraseIf(liveMessages, pred);
    saveToFlash();
}

// Dismiss oldest message in a direct conversation with a peer
void MessageStore::dismissOldestMessageWithPeer(uint32_t peer)
{
    auto pred = [peer](const StoredMessage &m) {
        if (m.type != MessageType::DM_TO_US)
            return false;
        uint32_t other = (m.sender == nodeDB->getNodeNum()) ? m.dest : m.sender;
        return other == peer;
    };
    eraseIf(liveMessages, pred);
    saveToFlash();
}

// Helper filters for future use
std::deque<StoredMessage> MessageStore::getChannelMessages(uint8_t channel) const
{
    std::deque<StoredMessage> result;
    for (const auto &m : liveMessages) {
        if (m.type == MessageType::BROADCAST && m.channelIndex == channel) {
            result.push_back(m);
        }
    }
    return result;
}

std::deque<StoredMessage> MessageStore::getDirectMessages() const
{
    std::deque<StoredMessage> result;
    for (const auto &m : liveMessages) {
        if (m.type == MessageType::DM_TO_US) {
            result.push_back(m);
        }
    }
    return result;
}

// Upgrade boot-relative timestamps once RTC is valid
// Only same-boot boot-relative messages are healed.
// Persisted boot-relative messages from old boots stay ??? forever.
void MessageStore::upgradeBootRelativeTimestamps()
{
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (nowSecs == 0)
        return; // Still no valid RTC

    uint32_t bootNow = millis() / 1000;

    auto fix = [&](std::deque<StoredMessage> &dq) {
        for (auto &m : dq) {
            if (m.isBootRelative && m.timestamp <= bootNow) {
                uint32_t bootOffset = nowSecs - bootNow;
                m.timestamp += bootOffset;
                m.isBootRelative = false;
            }
            // else: persisted from old boot → stays ??? forever
        }
    };
    fix(liveMessages);
}

// Global definition
MessageStore messageStore("default");
#endif