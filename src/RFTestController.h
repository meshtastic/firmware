#pragma once

#ifdef MESHTASTIC_RF_TEST_FIRMWARE

#include "mesh/RadioInterface.h"
#include <memory>

class RFTestController
{
  public:
    explicit RFTestController(std::unique_ptr<RadioInterface> radioInterface);

    void loop();
    bool isActive() const { return active; }

  private:
    enum class Mode { Off, LoRaPreamble, ContinuousWave };

    static constexpr float DEFAULT_FREQ_MHZ = 433.125f;
    static constexpr int8_t DEFAULT_MODULE_POWER_DBM = 33;
    static constexpr int8_t MIN_CHIP_POWER_DBM = -9;
    static constexpr int8_t MAX_CHIP_POWER_DBM = 21;
    static constexpr int8_t E22_PA_GAIN_DB = 12;

    std::unique_ptr<RadioInterface> ownedRadio;
    RadioInterface *radio = nullptr;
    Mode mode = Mode::LoRaPreamble;
    float frequencyMhz = DEFAULT_FREQ_MHZ;
    int8_t requestedModulePowerDbm = DEFAULT_MODULE_POWER_DBM;
    int8_t chipPowerDbm = MAX_CHIP_POWER_DBM;
    bool active = false;
    char lineBuffer[80] = {};
    size_t lineLength = 0;

    void processLine(char *line);
    void printHelp();
    void printStatus();
    void setMode(const char *modeName);
    void setFrequency(float mhz);
    void setPower(int powerDbm);
    bool parseFloat(const char *text, float &value) const;
    bool parseInt(const char *text, int &value) const;
    void start();
    void stop();
    bool applySettings();
    bool restartIfActive();
    const char *modeName() const;
    int8_t modulePowerToChipPower(int modulePowerDbm) const;
    void println(const char *message);
};

extern RFTestController *rfTestController;

#endif
