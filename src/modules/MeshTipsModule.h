#pragma once
#include "Observer.h"
#include "ProtobufModule.h"
#include "RadioInterface.h"
#include "SinglePortModule.h"

typedef struct {
    meshtastic_Config_LoRaConfig_ModemPreset preset;
    uint16_t slot;
} MeshTipsModule_TXSettings;

/**
 * Base class for the tips robot
 */
class MeshTipsModule
{
  public:
    /**
     * Constructor
     */
    MeshTipsModule();

    /**
     * Configure the radio to send the target packet, or return to default config if p is NULL
     */
    static bool configureRadioForPacket(RadioInterface *iface, meshtastic_MeshPacket *p);

    /**
     * Get target modem settings
     */
    MeshTipsModule_TXSettings stripTargetRadioSettings(meshtastic_MeshPacket *p);
};

/**
 * Tips module for sending tips into the mesh
 */
class MeshTipsNodeInfoModule : private MeshTipsModule, public ProtobufModule<meshtastic_User>, private concurrency::OSThread
{
  public:
    /**
     * Constructor
     */
    MeshTipsNodeInfoModule();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *p) override;

    /**
     * Send NodeInfo to the mesh
     */
    void sendTipsNodeInfo();

    /** Does our periodic broadcast */
    virtual int32_t runOnce() override;
};
extern MeshTipsNodeInfoModule *meshTipsNodeInfoModule;

/**
 * Text message handling for the tips robot
 */
class MeshTipsMessageModule : private MeshTipsModule, public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    /**
     * Constructor
     */
    MeshTipsMessageModule() : MeshTipsModule(), SinglePortModule("tips", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

  protected:
    /**
     * Called to handle a particular incoming message
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /**
     * Indicate whether this module wants to process the packet
     */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};
extern MeshTipsMessageModule *meshTipsMessageModule;