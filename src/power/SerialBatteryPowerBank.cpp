#include "configuration.h"

#if HAS_SERIAL_BATTERY_LEVEL
#include "DebugConfiguration.h"
#include "concurrency/OSThread.h"
#include "power/PowerBank.h"
#include "power/SerialBatteryParser.h"
#include <Arduino.h>
#include <SoftwareSerial.h>

namespace
{
// 4800 baud fills SoftwareSerial's 64-byte buffer in about 130 ms.
constexpr uint32_t POLL_INTERVAL_MS = 50;

// ThinkNode M4: the power-bank MCU's battery report over a software UART. Charging comes from
// EXT_CHRG_DETECT, which Power reads itself, so it is not reported here.
class SerialBatteryPowerBank : public PowerBank, private concurrency::OSThread
{
  public:
    SerialBatteryPowerBank() : concurrency::OSThread("SerialBattery", POLL_INTERVAL_MS) { serial.begin(4800); }

    // The frame rate is not known, so the last report is kept rather than aged out.
    bool getStatus(PowerBankStatus &out) override
    {
        if (parser.goodFrames() == 0)
            return false;
        out.batteryMv = parser.status().batteryMv;
        out.batteryPercent = parser.status().batteryPercent;
        out.charging = false;
        out.inputPresent = false;
        return true;
    }

  private:
    int32_t runOnce() override
    {
        while (serial.available()) {
            if (parser.feed(serial.read()) && parser.goodFrames() == 1)
                LOG_INFO("SerialBattery: first report, %u mV %u%%", parser.status().batteryMv, parser.status().batteryPercent);
        }
        if (parser.badFrames() != lastBadFrames) {
            lastBadFrames = parser.badFrames();
            LOG_WARN("SerialBattery: %u frames without an end byte", (unsigned)lastBadFrames);
        }
        return POLL_INTERVAL_MS;
    }

    SoftwareSerial serial = SoftwareSerial(SERIAL_BATTERY_RX, SERIAL_BATTERY_TX);
    SerialBatteryParser parser;
    uint32_t lastBadFrames = 0;
};
} // namespace

PowerBank *powerBankSetup()
{
    static SerialBatteryPowerBank *bank = nullptr;
    if (!bank)
        bank = new SerialBatteryPowerBank();
    return bank;
}
#endif
