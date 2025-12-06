#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

We hold a few recent messages, for features like the threaded message applet.
This class contains a struct for storing those messages,
and methods for serializing them to flash.

*/

#pragma once

#include "configuration.h"

#include <deque>

#include "mesh/MeshTypes.h"

namespace NicheGraphics::InkHUD
{

class MessageStore
{
  public:
    // A stored message
    struct Message {
        uint32_t timestamp; // Epoch seconds
        NodeNum sender = 0;
        uint8_t channelIndex;
        std::string text;
    };

    MessageStore() = delete;
    explicit MessageStore(std::string label); // Label determines filename in flash

    void saveToFlash();
    void loadFromFlash();

    std::deque<Message> messages; // Interact with this object!

  private:
    std::string filename;
};

} // namespace NicheGraphics::InkHUD

#endif
