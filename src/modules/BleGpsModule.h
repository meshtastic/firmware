#pragma once
#include "Default.h"
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"

/**
 * BLE GPS Module for sending GPS position to Android app via BLE
 * This module periodically sends the current GPS position to connected phone via Bluetooth Low Energy
 */
class BleGpsModule : public ProtobufModule<meshtastic_Position>, private concurrency::OSThread
{
  private:
    /// Timestamp of last position sent to phone
    uint32_t lastSentToPhone = 0;
    
    /// Interval between position updates sent to phone (in milliseconds)
    uint32_t sendIntervalMs = 5000; // 5 seconds (configurable)

  public:
    /** Constructor
     * Initializes the module with position app port and sets up periodic execution
     */
    BleGpsModule();

  protected:
    /** Called to handle a particular incoming message
    
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *p) override;

    /** Does our periodic broadcast to phone via BLE
     * Called by OSThread at regular intervals
     * 
     * @return interval in milliseconds until next execution
     */
    virtual int32_t runOnce() override;

  private:
    /**
     * Sends current GPS position to connected phone via BLE
     * Only sends if position is valid and enough time has passed since last send
     */
    void sendPositionToPhone();

    /**
     * Gets the current GPS position from nodeDB
     * 
     * @return meshtastic_Position struct with current position data, or empty if no valid position
     */
    meshtastic_Position getCurrentPosition();
};

extern BleGpsModule *bleGpsModule;

