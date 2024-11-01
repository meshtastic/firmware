#pragma once
#include "SinglePortModule.h"

class DetectionSensorModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    DetectionSensorModule()
        : SinglePortModule("detection", meshtastic_PortNum_DETECTION_SENSOR_APP), OSThread("DetectionSensorModule")
    {
    }

  protected:
    virtual int32_t runOnce() override;

  private:
    bool firstTime = true;
    uint32_t lastSentToMesh = 0;
    bool wasDetected = false;
    void sendDetectionMessage();
    void sendCurrentStateMessage(bool state);
    bool hasDetectionEvent();
};

extern DetectionSensorModule *detectionSensorModule;