#pragma once

#include "configuration.h"
#include <stdint.h>

// Battery and input state reported by a power-bank controller, in the terms Power needs.
struct PowerBankStatus {
    uint16_t batteryMv = 0;
    int8_t batteryPercent = -1;
    bool charging = false;
    bool inputPresent = false;
};

// What a power bank's own status page shows, beyond the battery. Strings are static.
struct PowerBankDetail {
    static constexpr uint8_t MAX_PORTS = 3;
    static constexpr uint8_t MAX_TEMPERATURES = 2;
    enum PortState : uint8_t { PORT_IDLE, PORT_CHARGING_BANK, PORT_POWERING_DEVICE };

    // False while the controller is asleep: only the battery fields of the status are current.
    bool live = false;
    uint8_t numPorts = 0;
    const char *portNames[MAX_PORTS] = {};
    PortState portStates[MAX_PORTS] = {};
    const char *fastCharge = nullptr; // protocol in use on a port, or nullptr
    bool hasWireless = false;
    bool wirelessActive = false;
    bool wirelessFault = false; // foreign object on the pad
    const char *wirelessProtocol = nullptr;
    uint32_t wirelessMw = 0;
    uint16_t busMv = 0;
    uint16_t busMa = 0;
    uint8_t numTemperatures = 0;
    int8_t temperaturesC[MAX_TEMPERATURES] = {};
    bool overTemperature = false;
};

// A power-bank controller that reports its own battery and input state.
class PowerBank
{
  public:
    virtual ~PowerBank() = default;

    // False while no battery state is available.
    virtual bool getStatus(PowerBankStatus &out) = 0;

    // False if the controller has nothing to show beyond getStatus().
    virtual bool getDetail(PowerBankDetail &) { return false; }
};

#if HAS_POWERBANK
// Starts the board's power-bank controller; provided by the controller's driver.
PowerBank *powerBankSetup();

// The running power bank, once Power has set it up.
extern PowerBank *powerBank;
#endif
