#pragma once
#if HAS_LORA_FEM
#include "LnaAutoSelector.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include <stdint.h>

typedef enum { GC1109_PA, KCT8103L_PA, STATION_G3_FEM, OTHER_FEM_TYPES } LoRaFEMType;

class LoRaFEMInterface
{
  public:
    LoRaFEMInterface() : fem_type(OTHER_FEM_TYPES), lna_auto_selector(0) {}
    virtual ~LoRaFEMInterface() {}
    void init(void);
    void setSleepModeEnable(void);
    void setTxModeEnable(void);
    void setRxModeEnable(void);
    void setRxModeEnableWhenMCUSleep(void);
    void setLnaMode(meshtastic_Config_LoRaConfig_FEM_LNA_Mode mode);
    bool isLnaEnabled(void) { return lna_enabled; }
    bool isLnaAuto(void) { return lna_auto; }
    /** Switch the RX path while already receiving; only call with the radio idle in RX */
    void setRxLnaPath(bool lnaOn);
    /** Time for the RX path to settle after setRxLnaPath() before RSSI reflects it */
    uint32_t getLnaSettleUs(void) { return lna_settle_us; }
    /** Feed one paired noise reading in AUTO mode; returns true when it completed a decision window */
    bool addLnaAutoSample(int16_t rssiLnaOn, int16_t rssiLnaOff);
    int32_t getLnaAutoBenefitTenthsDb(void) { return lna_auto_selector.getLastBenefitTenthsDb(); }
    /** Persist the AUTO decision if it differs from the saved one, so a reboot starts from it; call outside the radio lock */
    void saveLnaAutoDecision(void);
    /** Gain ahead of the radio that its RSSI includes; subtract it to refer a reading to the antenna */
    int16_t getRxLnaGainDb(void) { return (lna_can_control && lna_enabled) ? lna_gain_db : 0; }
    int8_t powerConversion(int8_t loraOutputPower);
    bool isLnaCanControl(void) { return lna_can_control; }

  private:
    /** Called from init() once a FEM's LNA is known to be switchable: driving pin to onLevel switches it in */
    void setLnaControl(uint8_t pin, uint8_t onLevel, int16_t gainDb, uint32_t settleUs);
    LoRaFEMType fem_type;
    bool lna_enabled = true;
    bool lna_can_control = false;
    uint8_t lna_pin = 0;
    uint8_t lna_on_level = LOW;
    int16_t lna_gain_db = 0;
    uint32_t lna_settle_us = 0;
    bool lna_auto = false;
    LnaAutoSelector lna_auto_selector;
    int8_t lna_auto_saved = -1; // decision on disk: -1 none, 0 bypassed, 1 in
    bool loadLnaAutoDecision(bool &lnaOn);
    bool high_power_pa = true;
};
extern LoRaFEMInterface loraFEMInterface;

#endif
