#include "AtakPluginModule.h"
#include "Default.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"

AtakPluginModule *atakPluginModule;

AtakPluginModule::AtakPluginModule()
    : SinglePortModule("atak", meshtastic_PortNum_ATAK_PLUGIN_V2), concurrency::OSThread("AtakPlugin")
{
}

int32_t AtakPluginModule::runOnce()
{
    return default_broadcast_interval_secs * 1000;
}

ProcessMessage AtakPluginModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    (void)mp; // Passthrough — no processing needed, apps handle compression/decompression
    return ProcessMessage::CONTINUE;
}
