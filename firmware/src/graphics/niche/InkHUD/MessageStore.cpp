#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MessageStore.h"

#include "SafeFile.h"

using namespace NicheGraphics;

// Hard limits on how much message data to write to flash
// Avoid filling the storage if something goes wrong
// Normal usage should be well below this size
constexpr uint8_t MAX_MESSAGES_SAVED = 10;
constexpr uint32_t MAX_MESSAGE_SIZE = 250;

InkHUD::MessageStore::MessageStore(std::string label)
{
    filename = "";
    filename += "/NicheGraphics";
    filename += "/";
    filename += label;
    filename += ".msgs";
}

// Write the contents of the MessageStore::messages object to flash
// Takes the firmware's SPI lock during FS operations. Implemented for consistency, but only relevant when using SD card.
// Need to lock and unlock around specific FS methods, as the SafeFile class takes the lock for itself internally
void InkHUD::MessageStore::saveToFlash()
{
    assert(!filename.empty());

#ifdef FSCom
    // Make the directory, if doesn't already exist
    // This is the same directory accessed by NicheGraphics::FlashData
    spiLock->lock();
    FSCom.mkdir("/NicheGraphics");
    spiLock->unlock();

    // Open or create the file
    // No "full atomic": don't save then rename
    auto f = SafeFile(filename.c_str(), false);

    LOG_INFO("Saving messages in %s", filename.c_str());

    // Take firmware's SPI Lock while writing
    spiLock->lock();

    // 1st byte: how many messages will be written to store
    f.write(messages.size());

    // For each message
    for (uint8_t i = 0; i < messages.size() && i < MAX_MESSAGES_SAVED; i++) {
        Message &m = messages.at(i);
        f.write((uint8_t *)&m.timestamp, sizeof(m.timestamp));                    // Write timestamp. 4 bytes
        f.write((uint8_t *)&m.sender, sizeof(m.sender));                          // Write sender NodeId. 4 Bytes
        f.write((uint8_t *)&m.channelIndex, sizeof(m.channelIndex));              // Write channel index. 1 Byte
        f.write((uint8_t *)m.text.c_str(), min(MAX_MESSAGE_SIZE, m.text.size())); // Write message text. Variable length
        f.write('\0');                                                            // Append null term
        LOG_DEBUG("Wrote message %u, length %u, text \"%s\"", (uint32_t)i, min(MAX_MESSAGE_SIZE, m.text.size()), m.text.c_str());
    }

    // Release firmware's SPI lock, because SafeFile::close needs it
    spiLock->unlock();

    bool writeSucceeded = f.close();

    if (!writeSucceeded) {
        LOG_ERROR("Can't write data!");
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
}

// Attempt to load the previous contents of the MessageStore:message deque from flash.
// Filename is controlled by the "label" parameter
// Takes the firmware's SPI lock during FS operations. Implemented for consistency, but only relevant when using SD card.
void InkHUD::MessageStore::loadFromFlash()
{
    // Hopefully redundant. Initial intention is to only load / save once per boot.
    messages.clear();

#ifdef FSCom

    // Take the firmware's SPI Lock, in case filesystem is on SD card
    concurrency::LockGuard guard(spiLock);

    // Check that the file *does* actually exist
    if (!FSCom.exists(filename.c_str())) {
        LOG_WARN("'%s' not found. Using default values", filename.c_str());
        return;
    }

    // Check that the file *does* actually exist
    if (!FSCom.exists(filename.c_str())) {
        LOG_INFO("'%s' not found.", filename.c_str());
        return;
    }

    // Open the file
    auto f = FSCom.open(filename.c_str(), FILE_O_READ);

    if (f.size() == 0) {
        LOG_INFO("%s is empty", filename.c_str());
        f.close();
        return;
    }

    // If opened, start reading
    if (f) {
        LOG_INFO("Loading threaded messages '%s'", filename.c_str());

        // First byte: how many messages are in the flash store
        uint8_t flashMessageCount = 0;
        f.readBytes((char *)&flashMessageCount, 1);
        LOG_DEBUG("Messages available: %u", (uint32_t)flashMessageCount);

        // For each message
        for (uint8_t i = 0; i < flashMessageCount && i < MAX_MESSAGES_SAVED; i++) {
            Message m;

            // Read meta data (fixed width)
            f.readBytes((char *)&m.timestamp, sizeof(m.timestamp));
            f.readBytes((char *)&m.sender, sizeof(m.sender));
            f.readBytes((char *)&m.channelIndex, sizeof(m.channelIndex));

            // Read characters until we find a null term
            char c;
            while (m.text.size() < MAX_MESSAGE_SIZE) {
                f.readBytes(&c, 1);
                if (c != '\0')
                    m.text += c;
                else
                    break;
            }

            // Store in RAM
            messages.push_back(m);

            LOG_DEBUG("#%u, timestamp=%u, sender(num)=%u, text=\"%s\"", (uint32_t)i, m.timestamp, m.sender, m.text.c_str());
        }

        f.close();
    } else {
        LOG_ERROR("Could not open / read %s", filename.c_str());
    }
#else
    LOG_ERROR("Filesystem not implemented");
    state = LoadFileState::NO_FILESYSTEM;
#endif
    return;
}

#endif