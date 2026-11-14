#pragma once
#if HAS_LORA_FEM
#include "LnaAutoSelector.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include <stdint.h>

// Noise-floor rise when the LNA is switched in at a noise-limited site; measured 23-25 dB on a KCT8103L at 915 MHz
#ifndef LORA_FEM_LNA_GAIN_DB
#define LORA_FEM_LNA_GAIN_DB 23
#endif

typedef enum { GC1109_PA, KCT8103L_PA, OTHER_FEM_TYPES } LoRaFEMType;

class LoRaFEMInterface
{
  public:
    LoRaFEMInterface() : fem_type(OTHER_FEM_TYPES), lna_auto_selector(LORA_FEM_LNA_GAIN_DB) {}
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
    /** Feed one paired noise reading in AUTO mode; returns true when it completed a decision window */
    bool addLnaAutoSample(int16_t rssiLnaOn, int16_t rssiLnaOff);
    int32_t getLnaAutoBenefitTenthsDb(void) { return lna_auto_selector.getLastBenefitTenthsDb(); }
    /** Gain ahead of the radio that its RSSI includes; subtract it to refer a reading to the antenna */
    int16_t getRxLnaGainDb(void) { return (lna_can_control && lna_enabled) ? LORA_FEM_LNA_GAIN_DB : 0; }
    int8_t powerConversion(int8_t loraOutputPower);
    bool isLnaCanControl(void) { return lna_can_control; }
    void setLnaCanControl(bool can_control) { lna_can_control = can_control; }

  private:
    LoRaFEMType fem_type;
    bool lna_enabled = true;
    bool lna_can_control = false;
    bool lna_auto = false;
    LnaAutoSelector lna_auto_selector;
    bool high_power_pa = true;
};
extern LoRaFEMInterface loraFEMInterface;

#endif
