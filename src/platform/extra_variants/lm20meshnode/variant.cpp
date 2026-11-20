#include "configuration.h"

#ifdef LM20MESHNODE

#include <Wire.h>

// nPM1300 setup for the LM20MeshNode (https://github.com/cvaldess/LM20MeshNode).
//
// The board starts with what the PMIC does on its own: BUCK2 (3.3 V main rail, VSET2) on, BUCK1 (GNSS rail,
// VSET1 grounded) off, battery charger disabled. Here the firmware takes over the GNSS rail and the charger.
// Register addresses are 16 bit, instance base address in the high byte (nPM1300 Product Specification v1.3).
// BUCK2 is left on its VSET2 pin setting, so nothing here can move the MCU's own supply.

namespace
{
constexpr uint8_t NPM1300_ADDR = 0x6B;

constexpr uint16_t VBUSIN_TASKUPDATEILIMSW = 0x0200; // 1: switch from VBUSINILIMSTARTUP (100 mA) to VBUSINILIM0
constexpr uint16_t VBUSIN_VBUSINILIM0 = 0x0201;
constexpr uint16_t BCHARGER_BCHGENABLESET = 0x0304;
constexpr uint16_t BCHARGER_BCHGENABLECLR = 0x0305;
constexpr uint16_t BCHARGER_BCHGISETMSB = 0x0308;
constexpr uint16_t BCHARGER_BCHGISETLSB = 0x0309;
constexpr uint16_t BCHARGER_BCHGVTERM = 0x030C;
constexpr uint16_t BCHARGER_BCHGVTERMR = 0x030D;
constexpr uint16_t BUCK_BUCK1NORMVOUT = 0x0408;
constexpr uint16_t BUCK_BUCKENCTRL = 0x040C;
constexpr uint16_t BUCK_BUCKSWCTRLSEL = 0x040F;

constexpr uint8_t VBUSINILIM_500MA = 5;
constexpr uint8_t VTERM_4V20 = 8;
constexpr uint8_t VTERM_4V00 = 4;
constexpr uint8_t BUCK1NORMVOUT_3V3 = 23;
constexpr uint8_t BUCKENCTRL_BUCK1_GPIO0 = 1;  // BUCK1 follows GPIO0 (P1.05, PIN_GPS_EN), not inverted
constexpr uint8_t BUCKSWCTRLSEL_BUCK1_REG = 1; // BUCK1 voltage from BUCK1NORMVOUT; BUCK2 stays on VSET2

static_assert(NPM1300_CHARGE_CURRENT_MA >= 32 && NPM1300_CHARGE_CURRENT_MA <= 800 && NPM1300_CHARGE_CURRENT_MA % 2 == 0,
              "nPM1300 charge current: 32 to 800 mA in 2 mA steps");

bool npm1300Write(uint16_t reg, uint8_t value)
{
    Wire.beginTransmission(NPM1300_ADDR);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}
} // namespace

void lateInitVariant()
{
    bool ok = true;

    // GNSS rail: 3.3 V, switched by GPIO0. GPIO0 is an input after reset, driven by the MCU's PIN_GPS_EN, so
    // the GPS code powers the receiver on and off; it probes the receiver later, from its own thread.
    // The voltage first, so BUCK1 never starts at its 1.2 V default.
    ok &= npm1300Write(BUCK_BUCK1NORMVOUT, BUCK1NORMVOUT_3V3);
    ok &= npm1300Write(BUCK_BUCKSWCTRLSEL, BUCKSWCTRLSEL_BUCK1_REG);
    ok &= npm1300Write(BUCK_BUCKENCTRL, BUCKENCTRL_BUCK1_GPIO0);

    // Charger: 1S LiPo, 4.20 V (4.00 V in the warm region), on-board 10 kOhm NTC (ADCNTCRSEL reset value).
    // The charge current may only change with the charger disabled.
    ok &= npm1300Write(BCHARGER_BCHGENABLECLR, 1);
    ok &= npm1300Write(BCHARGER_BCHGISETMSB, NPM1300_CHARGE_CURRENT_MA / 4);
    ok &= npm1300Write(BCHARGER_BCHGISETLSB, (NPM1300_CHARGE_CURRENT_MA / 2) & 1);
    ok &= npm1300Write(BCHARGER_BCHGVTERM, VTERM_4V20);
    ok &= npm1300Write(BCHARGER_BCHGVTERMR, VTERM_4V00);
    // USB input limit 500 mA instead of the 100 mA startup limit. Unplugging VBUS brings the PMIC back to the
    // startup limit, so until the next boot the battery charges from what the node leaves of 100 mA.
    ok &= npm1300Write(VBUSIN_VBUSINILIM0, VBUSINILIM_500MA);
    ok &= npm1300Write(VBUSIN_TASKUPDATEILIMSW, 1);
    ok &= npm1300Write(BCHARGER_BCHGENABLESET, 1);

    if (ok)
        LOG_INFO("nPM1300: GNSS rail 3.3 V on GPIO0, charger on at %d mA", NPM1300_CHARGE_CURRENT_MA);
    else
        LOG_WARN("nPM1300: a write to 0x%x was not acknowledged, the GNSS rail or the charger may be off", NPM1300_ADDR);
}

#endif
