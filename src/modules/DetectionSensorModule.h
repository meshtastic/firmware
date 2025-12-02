#pragma once
#include "SinglePortModule.h"

#if (defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)) &&            \
    !(defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2))
#define ESP32_WITH_EXT0
#endif

class DetectionSensorModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    DetectionSensorModule() : SinglePortModule("detection", meshtastic_PortNum_DETECTION_SENSOR_APP), OSThread("DetectionSensor")
    {
    }

#ifdef ARCH_NRF52
    boolean shouldSleep();
    void lpDelay();
    void lpLoop(uint32_t msecToWake);
#elif defined(ESP32_WITH_EXT0)
    bool skipGPIO(int gpio);
#endif

  protected:
    virtual int32_t runOnce() override;

  private:
    bool firstTime = true;
    uint32_t lastSentToMesh = 0;
    bool wasDetected = false;
    void sendDetectionMessage();
    void sendCurrentStateMessage(bool state);
    bool hasDetectionEvent();
    boolean getState();
#ifdef ESP32_WITH_EXT0
    bool isRtcGpio(int gpio);
    void printRtcPins();
#endif
};

extern DetectionSensorModule *detectionSensorModule;