#pragma once
#include "SinglePortModule.h"

class DetectionSensorModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    DetectionSensorModule() : SinglePortModule("detection", meshtastic_PortNum_DETECTION_SENSOR_APP), OSThread("DetectionSensor")
    {
    }

  protected:
    virtual int32_t runOnce() override;

  private:
    bool firstTime = true;
    uint32_t lastSentToMesh = 0;
    // Pin/pullup actually behind the current pinMode()+attachInterrupt() setup; 0 = not configured
    // yet. Compared against moduleConfig each poll so a runtime change gets picked up without a
    // reboot instead of leaving the interrupt bound to a stale pin.
    uint32_t configuredMonitorPin = 0;
    bool configuredUsePullup = false;
    void configureMonitorPin();
    // Below: written from both runOnce() and the monitor-pin interrupt (see attachInterrupt and
    // updatePendingVerdict()); volatile so neither side caches a stale value across that boundary.
    volatile bool wasDetected = false;
    // Verdicts throttled by minimum_broadcast_secs are latched here instead of discarded, tracked
    // separately so a later SendState can't overwrite an already-pending Detected.
    volatile bool pendingDetected = false;
    volatile bool pendingState = false;
    volatile bool pendingStateIsDetected = false;
    // Which of the two above is the older still-outstanding one; updated whenever pendingDetected
    // or pendingState newly transitions false->true, so send order matches occurrence order.
    volatile bool pendingDetectedFirst = false;
    void sendDetectionMessage();
    void sendCurrentStateMessage(bool state);
    bool hasDetectionEvent();
    // Samples the pin and updates the pending-verdict state; called from both runOnce() and the
    // interrupt. Call with interrupts disabled from runOnce() to avoid reentrant corruption.
    void updatePendingVerdict();
};

extern DetectionSensorModule *detectionSensorModule;
