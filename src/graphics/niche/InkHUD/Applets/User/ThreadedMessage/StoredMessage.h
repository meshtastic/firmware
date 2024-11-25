#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

One message, as stored by ThreadedMessageApplets.
A small number of these are held in RAM, and serialized to FS at shutdown.

*/

#pragma once

#include "configuration.h"

#include "mesh/MeshTypes.h"

namespace NicheGraphics::InkHUD
{

struct StoredMessage {
    uint32_t timestamp; // Epoch seconds
    NodeNum sender;
    std::string text;
};

} // namespace NicheGraphics::InkHUD

#endif