#pragma once
#include "Default.h"
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"

/**
 * Position module for sending/receiving positions into the mesh
 */
class PositionModule : public ProtobufModule<meshtastic_Position>, private concurrency::OSThread
{
    CallbackObserver<PositionModule, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<PositionModule, const meshtastic::Status *>(this, &PositionModule::handleStatusUpdate);

    /// The id of the last packet we sent, to allow us to cancel it if we make something fresher
    PacketId prevPacketId = 0;

    /// We limit our GPS broadcasts to a max rate
    uint32_t lastGpsSend = 0;

    // Store the latest good lat / long
    int32_t lastGpsLatitude = 0;
    int32_t lastGpsLongitude = 0;

    /// We force a rebroadcast if the radio settings change
    uint32_t currentGeneration = 0;

  public:
    /** Constructor
     * name is for debugging output
     */
    PositionModule();

    /**
     * Send our position into the mesh
     */
    void sendOurPosition(NodeNum dest, bool wantReplies = false, uint8_t channel = 0);
    void sendOurPosition();

    void handleNewPosition();

    // Pure broadcast-policy helpers, split out so they're unit-testable without the module.
    // True when two coordinates truncate to the same precision cell (so a re-broadcast would be a
    // duplicate). precision 0 or >=32 returns false: no coarse cell to hold within, never suppress.
    static bool positionWithinPrecisionCell(int32_t aLat, int32_t aLon, int32_t bLat, int32_t bLon, uint32_t precision);
    // Effective min interval: stationary positions are held to stationaryFloorMs (when that is the
    // longer of the two); otherwise the normal configured interval.
    static uint32_t effectiveBroadcastIntervalMs(uint32_t configuredIntervalMs, bool stationary, uint32_t stationaryFloorMs);

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *p) override;

    virtual void alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_Position *p) override;

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual meshtastic_MeshPacket *allocReply() override;

    /** Does our periodic broadcast */
    virtual int32_t runOnce() override;

  private:
    meshtastic_MeshPacket *allocPositionPacket();
    struct SmartPosition getDistanceTraveledSinceLastSend(meshtastic_PositionLite currentPosition);
    // True when our position is unchanged since the last broadcast: it truncates to the same
    // precision grid cell, so re-sending would be a duplicate that traffic management dedups
    // downstream anyway. Used to hold stationary broadcasts to a 12h floor. useConfiguredPrecision
    // gauges movement at our own configured (unclamped) precision rather than the on-wire
    // (public-clamped) precision — trackers report finer movement.
    bool positionUnchangedSinceLastSend(const meshtastic_PositionLite &selfPos, bool useConfiguredPrecision);
    meshtastic_MeshPacket *allocAtakPli();
    void trySetRtc(meshtastic_Position p, bool isLocal, bool forceUpdate = false);
    uint32_t precision;
    void sendLostAndFoundText();
    bool hasQualityTimesource();
    bool hasGPS();
    uint32_t lastSentReply = 0; // Last time we sent a position reply (used for reply throttling only)

#if USERPREFS_EVENT_MODE
    // In event mode we want to prevent excessive position broadcasts
    // we set the minimum interval to 5m
    const uint32_t minimumTimeThreshold =
        max(uint32_t(300000), Default::getConfiguredOrDefaultMs(config.position.broadcast_smart_minimum_interval_secs,
                                                                default_broadcast_smart_minimum_interval_secs));
#else
    const uint32_t minimumTimeThreshold = Default::getConfiguredOrDefaultMs(config.position.broadcast_smart_minimum_interval_secs,
                                                                            default_broadcast_smart_minimum_interval_secs);
#endif
};

struct SmartPosition {
    float distanceTraveled;
    uint32_t distanceThreshold;
    bool hasTraveledOverThreshold;
};

extern PositionModule *positionModule;