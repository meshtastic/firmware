#include "HLT331PowerStatus.h"

bool hlt331MergeStatus(const HLT331Sources &in, PowerBankStatus &out)
{
    bool useGauge = in.gaugeValid;
    if (useGauge && in.fresh) {
        const int diff = (int)in.gaugeMv - (int)in.controller.batteryMv;
        if (diff > HLT331_GAUGE_MAX_DISAGREEMENT_MV || diff < -HLT331_GAUGE_MAX_DISAGREEMENT_MV)
            useGauge = false;
    }

    const bool controllerUsable = in.haveFrame && (in.fresh || in.asleep);
    if (useGauge) {
        out.batteryMv = in.gaugeMv;
        out.batteryPercent = in.gaugePercent;
        // At the end of a charge the gauge stops at about 99.4 % while the HLT331 already says full.
        if (out.batteryPercent == 99 && controllerUsable && in.controller.batteryPercent == 100)
            out.batteryPercent = 100;
    } else if (controllerUsable) {
        out.batteryMv = in.controller.batteryMv;
        out.batteryPercent = in.controller.batteryPercent;
    } else {
        return false;
    }
    out.charging = in.fresh && in.controller.charging;
    out.inputPresent = in.fresh && in.controller.inputPresent();
    return true;
}

bool hlt331WantsC2Vbus(bool fresh, const HLT331Status &s)
{
    return fresh && s.c2State == HLT331Status::PORT_CHARGING;
}

static PowerBankDetail::PortState portState(uint8_t state)
{
    switch (state) {
    case HLT331Status::PORT_CHARGING:
        return PowerBankDetail::PORT_CHARGING_BANK;
    case HLT331Status::PORT_DISCHARGING:
        return PowerBankDetail::PORT_POWERING_DEVICE;
    default:
        return PowerBankDetail::PORT_IDLE;
    }
}

void hlt331Detail(const HLT331Status &s, bool live, bool asleep, PowerBankDetail &out)
{
    out = PowerBankDetail();
    out.live = live;
    out.asleep = asleep;
    out.numPorts = 2;
    out.portNames[0] = "C1";
    out.portNames[1] = "C2";
    out.portStates[0] = portState(s.c1State);
    out.portStates[1] = portState(s.c2State);
    out.fastCharge = hlt331FastChargeName(s.fastChargeMode);
    out.hasWireless = true;
    out.wirelessActive = s.wirelessActive;
    out.wirelessFault = s.wirelessForeignObject;
    out.wirelessProtocol = hlt331WirelessProtocolName(s.wirelessProtocol);
    out.wirelessMw = s.wirelessMw;
    out.busMv = s.busMv;
    out.busMa = s.busMa;
    out.numTemperatures = 2;
    out.temperaturesC[0] = s.ntc1TempC;
    out.temperaturesC[1] = s.ntc2TempC;
    out.overTemperature = s.ntcOverTemp;
}

const char *hlt331FastChargeName(uint8_t mode)
{
    static const char *const names[] = {nullptr, "QC2", "QC3", "QC3+", "FCP", "SCP", "PD", "PPS", "PE1.1", "PE2.0", "?", "AFC"};
    return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "?";
}

const char *hlt331WirelessProtocolName(uint8_t protocol)
{
    static const char *const names[] = {nullptr, "BPP", "Apple 7.5W", "Apple", "EPP", "Samsung", "Huawei", "Qi2 ltd", "Qi2"};
    return protocol < sizeof(names) / sizeof(names[0]) ? names[protocol] : "?";
}
