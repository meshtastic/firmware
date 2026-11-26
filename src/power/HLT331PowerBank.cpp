#include "configuration.h"

#if HAS_HLT331
#include "DebugConfiguration.h"
#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include "mesh/Throttle.h"
#include "power/HLT331Parser.h"
#include "power/PowerBank.h"
#include <Arduino.h>

#ifndef HLT331_SERIAL
#define HLT331_SERIAL Serial2
#endif
#ifndef HLT331_BAUD
#define HLT331_BAUD 115200
#endif

namespace
{
// Polled faster than one frame fills the 64-byte UART buffer.
constexpr uint32_t POLL_INTERVAL_MS = 100;
// No frame for this long and the last status no longer describes the bank.
constexpr uint32_t STALE_MS = 3000;
constexpr uint32_t WARN_INTERVAL_MS = 60 * 1000;

class HLT331PowerBank : public PowerBank, private concurrency::OSThread
{
  public:
    HLT331PowerBank() : concurrency::OSThread("HLT331", POLL_INTERVAL_MS) { HLT331_SERIAL.begin(HLT331_BAUD); }

    bool getStatus(PowerBankStatus &out) override
    {
        if (!haveFrame || Throttle::hasElapsed(lastFrameMs, STALE_MS))
            return false;
        const HLT331Status &s = parser.status();
        out.batteryMv = s.batteryMv;
        out.batteryPercent = s.batteryPercent;
        out.charging = s.charging;
        out.inputPresent = s.inputPresent();
        return true;
    }

  private:
    int32_t runOnce() override
    {
        while (HLT331_SERIAL.available()) {
            if (parser.feed(HLT331_SERIAL.read())) {
                if (!haveFrame)
                    LOG_INFO("HLT331: first status frame, battery %u mV %u%%", parser.status().batteryMv,
                             parser.status().batteryPercent);
                haveFrame = true;
                lastFrameMs = Time::getMillis();
            }
        }
        const uint32_t rejected = parser.badCrcFrames() + parser.badLengthFrames();
        if (rejected != lastRejected && (lastWarnMs == 0 || Throttle::hasElapsed(lastWarnMs, WARN_INTERVAL_MS))) {
            lastRejected = rejected;
            lastWarnMs = Time::stampMillis();
            LOG_WARN("HLT331: rejected frames, %u bad CRC, %u bad length", (unsigned)parser.badCrcFrames(),
                     (unsigned)parser.badLengthFrames());
        }
        return POLL_INTERVAL_MS;
    }

    HLT331Parser parser;
    bool haveFrame = false;
    uint32_t lastFrameMs = 0;
    uint32_t lastRejected = 0;
    uint32_t lastWarnMs = 0;
};
} // namespace

PowerBank *powerBankSetup()
{
    static HLT331PowerBank *bank = nullptr;
    if (!bank)
        bank = new HLT331PowerBank();
    return bank;
}
#endif
