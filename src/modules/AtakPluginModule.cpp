#include "AtakPluginModule.h"

AtakPluginModule *atakPluginModule;

AtakPluginModule::AtakPluginModule()
    : SinglePortModule("atak", meshtastic_PortNum_ATAK_PLUGIN_V2)
{
}

ProcessMessage AtakPluginModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    (void)mp; // Passthrough — no processing needed, apps handle compression/decompression
    return ProcessMessage::CONTINUE;
}
