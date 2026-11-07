#pragma once

#include "configuration.h"

#ifdef HELTEC_MESH_POCKET_V2

#include "Observer.h"
#include "PowerStatus.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include <CW221X.h>
#include <HLT331Serial.h>

enum class MeshPocketV2PowerBankEventKind : uint8_t {
    StatusEdge,
    FaultEdge,
    Io1Pulse,
    PageRefresh,
    SnapshotOnly,
};

struct MeshPocketV2PowerBankEvent {
    MeshPocketV2PowerBankEventKind kind = MeshPocketV2PowerBankEventKind::SnapshotOnly;
    bool active = false;
    bool fault = false;
};

struct MeshPocketV2PowerBankSnapshot {
    bool cwReady = false;
    bool hltValid = false;
    bool active = false;
    bool fault = false;
    bool hltIo1DrainActive = false;
    bool hltIo1Pending = false;
    bool hltIo2Working = false;
    uint32_t hltIo1PulseCount = 0;
    uint32_t hltLastIo1PulseMs = 0;
    uint32_t cwUpdatedMs = 0;
    uint32_t updatedMs = 0;
    CW221X::Status cw = {};
    HLT331Serial::Status hlt = {};
};

class MeshPocketV2PowerBank : public concurrency::OSThread
{
  public:
    MeshPocketV2PowerBank();

    bool setup();
    void handleHltIo1PulseFromIsr();
    bool copySnapshot(MeshPocketV2PowerBankSnapshot &out);
    bool copyPowerStatusFields(meshtastic::OptionalBool &hasBattery, meshtastic::OptionalBool &usbPowered,
                               meshtastic::OptionalBool &charging, int32_t &voltageMv, int8_t &percent);
    bool isAvailable() const { return started; }

    Observable<const MeshPocketV2PowerBankEvent *> onEvent;

  private:
    int32_t runOnce() override;

    void readGauge(uint32_t now);
    void readHlt331(uint32_t now);
    bool consumeHltIo1Pending(uint32_t now);
    void publishEvent(MeshPocketV2PowerBankEventKind kind);
    bool computeActive(const HLT331Serial::Status &status) const;
    bool computeFault(const HLT331Serial::Status &status, const CW221X::Status &cwStatus) const;
    void updateSnapshotLocked(uint32_t now);

    CW221X gauge;
    HLT331Serial hlt331;
    MeshPocketV2PowerBankSnapshot snapshot = {};
    concurrency::Lock lock;
    bool started = false;
    bool gaugeStarted = false;
    bool hltStarted = false;
    HLT331Serial::Status lastWakeStatus = {};
    bool hasWakeStatus = false;
    bool lastFault = false;
    bool hltIo1DrainActive = false;
    volatile bool hltIo1Pending = false;
    volatile uint32_t hltIo1PulseCount = 0;
    uint32_t hltLastIo1PulseMs = 0;
    uint32_t lastGaugeReadMs = 0;
    uint32_t lastHltReadMs = 0;
    uint32_t hltIo1DrainStartedMs = 0;
    bool hltIo1PulseEventPending = false;
    uint8_t fastGaugeReadsRemaining = 0;
};

extern MeshPocketV2PowerBank *meshPocketV2PowerBank;

void meshPocketV2PowerBankSetup();
bool meshPocketV2PowerBankCopySnapshot(MeshPocketV2PowerBankSnapshot &out);
bool meshPocketV2PowerBankCopyPowerStatusFields(meshtastic::OptionalBool &hasBattery, meshtastic::OptionalBool &usbPowered,
                                                meshtastic::OptionalBool &charging, int32_t &voltageMv, int8_t &percent);

#endif // HELTEC_MESH_POCKET_V2
