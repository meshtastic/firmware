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
    bool wasDetected = false;
    // Verdicts throttled by minimum_broadcast_secs are latched here instead of discarded, tracked
    // separately so a later SendState can't overwrite an already-pending Detected.
    bool pendingDetected = false;
    bool pendingState = false;
    bool pendingStateIsDetected = false;
    // Which of the two above became pending first, while neither was already outstanding; used to
    // preserve send order when both end up pending at once.
    bool pendingDetectedFirst = false;
    void sendDetectionMessage();
    void sendCurrentStateMessage(bool state);
    bool hasDetectionEvent();
};

extern DetectionSensorModule *detectionSensorModule;