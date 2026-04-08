#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/**
 * ATAK Plugin V2 module - passthrough for zstd dictionary-compressed CoT payloads.
 * All compression/decompression is handled by the apps (Android, iOS, ATAK plugin).
 * Firmware simply forwards opaque bytes on the ATAK_PLUGIN_V2 port.
 */
class AtakPluginModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    /** Constructor
     * name is for debugging output
     */
    AtakPluginModule();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    /* Does our periodic broadcast */
    int32_t runOnce() override;
};

extern AtakPluginModule *atakPluginModule;
