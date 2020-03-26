#pragma once

namespace meshtastic
{

/// Describes the state of the power system.
struct PowerStatus {
    /// Whether we have a battery connected
    bool haveBattery;
    /// Battery voltage in mV, valid if haveBattery is true
    int batteryVoltageMv;
    /// Whether USB is connected
    bool usb;
    /// Whether we are charging the battery
    bool charging;
};

} // namespace meshtastic
