#pragma once
#include "DetectionSensorBurst.h"
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
    bool dwellArmed = false;
    uint32_t dwellStartedMs = 0;
    bool pinWasActive = false;
    uint32_t pinActiveStartedMs = 0;
    DetectionSensorBurstState burst;
    // True only when an alert was actually mesh-sent for the current burst.
    bool alertSentToMeshThisBurst = false;
    bool alertDeliveryPending = false;
    uint32_t pendingAlertBurstMs = 0;
    bool sendDetectionMessage(uint32_t burstMs);
    void sendClearedMessage(uint32_t activeMs, uint32_t burstMs);
    void sendCurrentStateMessage(bool state);
    bool pinIsActive();
};

extern DetectionSensorModule *detectionSensorModule;
