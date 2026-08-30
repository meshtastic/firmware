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
    // A verdict produced while minimum_broadcast_secs was still throttling sends is latched here
    // rather than discarded, so it fires as soon as the throttle window reopens instead of being
    // silently lost (wasDetected keeps updating every poll for correct edge comparisons regardless).
    bool pendingSend = false;
    bool pendingSendIsState = false;
    bool pendingIsDetected = false;
    void sendDetectionMessage();
    void sendCurrentStateMessage(bool state);
    bool hasDetectionEvent();
};

extern DetectionSensorModule *detectionSensorModule;