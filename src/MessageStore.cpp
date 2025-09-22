#include "MessageStore.h"
#include "FSCommon.h"
#include "NodeDB.h" // for nodeDB->getNodeNum()
#include "SPILock.h"
#include "SafeFile.h"
#include "configuration.h" // for millis()
#include "graphics/draw/MessageRenderer.h"

using graphics::MessageRenderer::setThreadMode;
using graphics::MessageRenderer::ThreadMode;

MessageStore::MessageStore(const std::string &label)
{
    filename = "/Messages_" + label + ".msgs";
}

// === Live message handling (RAM only) ===
void MessageStore::addLiveMessage(const StoredMessage &msg)
{
    if (liveMessages.size() >= MAX_MESSAGES_SAVED) {
        liveMessages.pop_front(); // keep only most recent N
    }
    liveMessages.push_back(msg);
}

// === Persistence queue (used only on shutdown/reboot) ===
void MessageStore::addMessage(const StoredMessage &msg)
{
    if (messages.size() >= MAX_MESSAGES_SAVED) {
        messages.pop_front();
    }
    messages.push_back(msg);
}

void MessageStore::addFromPacket(const meshtastic_MeshPacket &packet)
{
    StoredMessage sm;

    sm.timestamp = packet.rx_time ? packet.rx_time : (millis() / 1000);
    sm.sender = packet.from;
    sm.channelIndex = packet.channel;
    sm.text = std::string(reinterpret_cast<const char *>(packet.decoded.payload.bytes));

    // Classification logic
    if (packet.to == NODENUM_BROADCAST || packet.decoded.dest == NODENUM_BROADCAST) {
        sm.dest = NODENUM_BROADCAST;
        sm.type = MessageType::BROADCAST;
    } else if (packet.to == nodeDB->getNodeNum()) {
        sm.dest = nodeDB->getNodeNum(); // DM to us
        sm.type = MessageType::DM_TO_US;
    } else {
        sm.dest = NODENUM_BROADCAST; // fallback
        sm.type = MessageType::BROADCAST;
    }

    addLiveMessage(sm);

    // === Auto-switch thread view on new message ===
    if (sm.type == MessageType::BROADCAST) {
        setThreadMode(ThreadMode::CHANNEL, sm.channelIndex);
    } else if (sm.type == MessageType::DM_TO_US) {
        setThreadMode(ThreadMode::DIRECT, -1, sm.sender);
    }
}

// === Outgoing/manual message ===
void MessageStore::addFromString(uint32_t sender, uint8_t channelIndex, const std::string &text)
{
    StoredMessage sm;
    sm.timestamp = millis() / 1000;
    sm.sender = sender;
    sm.channelIndex = channelIndex;
    sm.text = text;

    // Default manual adds to broadcast
    sm.dest = NODENUM_BROADCAST;
    sm.type = MessageType::BROADCAST;

    addLiveMessage(sm);
}

// === Save RAM queue to flash (called on shutdown) ===
void MessageStore::saveToFlash()
{
#ifdef FSCom
    // Copy live RAM buffer into persistence queue
    messages = liveMessages;

    spiLock->lock();
    FSCom.mkdir("/"); // ensure root exists
    spiLock->unlock();

    SafeFile f(filename.c_str(), false);

    spiLock->lock();
    uint8_t count = messages.size();
    f.write(&count, 1);

    for (uint8_t i = 0; i < messages.size() && i < MAX_MESSAGES_SAVED; i++) {
        const StoredMessage &m = messages.at(i);
        f.write((uint8_t *)&m.timestamp, sizeof(m.timestamp));
        f.write((uint8_t *)&m.sender, sizeof(m.sender));
        f.write((uint8_t *)&m.channelIndex, sizeof(m.channelIndex));
        f.write((uint8_t *)&m.dest, sizeof(m.dest));
        f.write((uint8_t *)m.text.c_str(), std::min(static_cast<size_t>(MAX_MESSAGE_SIZE), m.text.size()));
        f.write('\0'); // null terminator
    }
    spiLock->unlock();

    f.close();
#else
    // Filesystem not available, skip persistence
#endif
}

// === Load persisted messages into RAM (called at boot) ===
void MessageStore::loadFromFlash()
{
    messages.clear();
    liveMessages.clear();
#ifdef FSCom
    concurrency::LockGuard guard(spiLock);

    if (!FSCom.exists(filename.c_str()))
        return;
    auto f = FSCom.open(filename.c_str(), FILE_O_READ);
    if (!f)
        return;

    uint8_t count = 0;
    f.readBytes((char *)&count, 1);

    for (uint8_t i = 0; i < count && i < MAX_MESSAGES_SAVED; i++) {
        StoredMessage m;
        f.readBytes((char *)&m.timestamp, sizeof(m.timestamp));
        f.readBytes((char *)&m.sender, sizeof(m.sender));
        f.readBytes((char *)&m.channelIndex, sizeof(m.channelIndex));
        f.readBytes((char *)&m.dest, sizeof(m.dest));

        char c;
        while (m.text.size() < MAX_MESSAGE_SIZE) {
            if (f.readBytes(&c, 1) <= 0)
                break;
            if (c == '\0')
                break;
            m.text.push_back(c);
        }

        // Recompute type from dest
        if (m.dest == NODENUM_BROADCAST) {
            m.type = MessageType::BROADCAST;
        } else {
            m.type = MessageType::DM_TO_US;
        }

        messages.push_back(m);
        liveMessages.push_back(m); // restore into RAM buffer
    }
    f.close();
#endif
}

// === Clear all messages (RAM + persisted queue) ===
void MessageStore::clearAllMessages()
{
    liveMessages.clear();
    messages.clear();

#ifdef FSCom
    SafeFile f(filename.c_str(), false);
    uint8_t count = 0;
    f.write(&count, 1); // write "0 messages"
    f.close();
#endif
}

// === Dismiss oldest message (RAM + persisted queue) ===
void MessageStore::dismissOldestMessage()
{
    if (!liveMessages.empty()) {
        liveMessages.pop_front();
    }
    if (!messages.empty()) {
        messages.pop_front();
    }
    saveToFlash();
}

// === Dismiss newest message (RAM + persisted queue) ===
void MessageStore::dismissNewestMessage()
{
    if (!liveMessages.empty()) {
        liveMessages.pop_back();
    }
    if (!messages.empty()) {
        messages.pop_back();
    }
    saveToFlash();
}

// === Helper filters for future use ===
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

// === Global definition ===
MessageStore messageStore("default");
