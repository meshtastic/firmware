#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/**
 * ATAK Plugin V2 module - passthrough for ATAK_PLUGIN_V2 payloads.
 * The wire format includes a leading flags byte followed by opaque payload bytes.
 * Depending on the flags, the payload may be zstd dictionary-compressed or raw/uncompressed protobuf.
 * Compression/decompression and payload interpretation are handled by the apps
 * (Android, iOS, ATAK plugin); firmware forwards the bytes unchanged on the
 * ATAK_PLUGIN_V2 port.
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
