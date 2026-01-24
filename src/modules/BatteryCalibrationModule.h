#pragma once

#include "SinglePortModule.h"
#include "BatteryCalibrationSampler.h"
#include "power.h"

class BatteryCalibrationModule : public SinglePortModule
{
  public:
    BatteryCalibrationModule();
    void startCalibration();
    void stopCalibration();
    bool isCalibrationActive() const { return calibrationActive; }
    bool persistCalibrationOcv();
    void handleSampleUpdate();

#if HAS_SCREEN
    bool wantUIFrame() override { return true; }
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    bool computeOcvFromSamples(uint16_t *ocvOut, size_t ocvCount);
    bool calibrationActive = false;
    bool calibrationOcvValid = false;
    uint16_t calibrationOcv[NUM_OCV_POINTS]{};
#if HAS_SCREEN
    void computeGraphBounds(OLEDDisplay *display, int16_t x, int16_t y, int16_t &graphX, int16_t &graphY, int16_t &graphW,
                            int16_t &graphH);
    void drawBatteryGraph(OLEDDisplay *display, int16_t graphX, int16_t graphY, int16_t graphW, int16_t graphH,
                          const BatteryCalibrationSampler::BatterySample *samples, uint16_t sampleCount, uint16_t sampleStart,
                          uint32_t minMv, uint32_t maxMv);
#endif

};
extern BatteryCalibrationModule *batteryCalibrationModule;