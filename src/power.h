#pragma once

/**
 * Per @spattinson
 * MIN_BAT_MILLIVOLTS seems high. Typical 18650 are different chemistry to LiPo, even for LiPos that chart seems a bit off, other
 * charts put 3690mV at about 30% for a lipo, for 18650 i think 10% remaining iis in the region of 3.2-3.3V. Reference 1st graph
 * in [this test report](https://lygte-info.dk/review/batteries2012/Samsung%20INR18650-30Q%203000mAh%20%28Pink%29%20UK.html)
 * looking at the red line - discharge at 0.2A - he gets a capacity of 2900mah, 90% of 2900 = 2610, that point in the graph looks
 * to be a shade above 3.2V
 */
#define MIN_BAT_MILLIVOLTS 3250 // millivolts. 10% per https://blog.ampow.com/lipo-voltage-chart/

#define BAT_MILLIVOLTS_FULL 4100
#define BAT_MILLIVOLTS_EMPTY 3500

namespace meshtastic
{

/// Describes the state of the power system.
struct PowerStatus {
    /// Whether we have a battery connected
    bool haveBattery;
    /// Battery voltage in mV, valid if haveBattery is true
    int batteryVoltageMv;
    /// Battery charge percentage, either read directly or estimated
    int batteryChargePercent;
    /// Whether USB is connected
    bool usb;
    /// Whether we are charging the battery
    bool charging;
};

} // namespace meshtastic

extern meshtastic::PowerStatus powerStatus;
