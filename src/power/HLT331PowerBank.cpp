#include "configuration.h"

#if HAS_HLT331
#include "DebugConfiguration.h"
#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include "mesh/Throttle.h"
#include "power/HLT331Parser.h"
#include "power/PowerBank.h"
#include <Arduino.h>

// HLT331_DEBUG=1 logs status changes with the raw frame, IO1/IO2 activity, data going stale, and
// whether the fuel gauge answers on I2C. Costs no flash when off.
#ifndef HLT331_DEBUG
#define HLT331_DEBUG 0
#endif
#if HLT331_DEBUG
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#define LOG_DEBUG_HLT331(...) LOG_DEBUG(__VA_ARGS__)
#else
#define LOG_DEBUG_HLT331(...) ((void)0)
#endif

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

#if HLT331_DEBUG
constexpr uint32_t HEARTBEAT_MS = 10 * 1000;
volatile uint32_t io1Pulses = 0;
void io1Isr()
{
    io1Pulses++;
}
#endif

class HLT331PowerBank : public PowerBank, private concurrency::OSThread
{
  public:
    HLT331PowerBank() : concurrency::OSThread("HLT331", POLL_INTERVAL_MS)
    {
        pinMode(PIN_HLT331_IO1, INPUT_PULLDOWN);
        pinMode(PIN_HLT331_IO2, INPUT_PULLDOWN);
        HLT331_SERIAL.begin(HLT331_BAUD);
#if HLT331_DEBUG
        attachInterrupt(digitalPinToInterrupt(PIN_HLT331_IO1), io1Isr, RISING);
        lastIo2 = digitalRead(PIN_HLT331_IO2);
        LOG_DEBUG_HLT331("HLT331: setup, IO1=%d IO2=%d, %s", digitalRead(PIN_HLT331_IO1), lastIo2, gaugeProbe());
#endif
    }

    bool getStatus(PowerBankStatus &out) override
    {
        if (!isFresh())
            return false;
        const HLT331Status &s = parser.status();
        out.batteryMv = s.batteryMv;
        out.batteryPercent = s.batteryPercent;
        out.charging = s.charging;
        out.inputPresent = s.inputPresent();
        return true;
    }

  private:
    bool isFresh() const { return haveFrame && !Throttle::hasElapsed(lastFrameMs, STALE_MS); }

    int32_t runOnce() override
    {
        [[maybe_unused]] bool gotFrame = false;
        while (HLT331_SERIAL.available()) {
            if (parser.feed(HLT331_SERIAL.read())) {
                if (!haveFrame)
                    LOG_INFO("HLT331: first status frame, battery %u mV %u%%", parser.status().batteryMv,
                             parser.status().batteryPercent);
                haveFrame = true;
                gotFrame = true;
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
#if HLT331_DEBUG
        debugLog(gotFrame);
#endif
        return POLL_INTERVAL_MS;
    }

#if HLT331_DEBUG
    void debugLog(bool gotFrame)
    {
        // Flags, states, percent and temperatures; the voltages and current move on every frame.
        static constexpr uint8_t KEY_BYTES[] = {0, 1, 2, 3, 12};
        if (gotFrame) {
            bool changed = !loggedFrame;
            for (uint8_t i : KEY_BYTES)
                changed |= parser.lastFrame()[i] != lastLogged[i];
            if (changed)
                logFrame("change");
        }

        const bool fresh = isFresh();
        if (fresh != wasFresh) {
            if (fresh)
                LOG_DEBUG_HLT331("HLT331: frames arriving");
            else
                LOG_DEBUG_HLT331("HLT331: no frame for %u ms, status unavailable", (unsigned)STALE_MS);
            wasFresh = fresh;
        }

        const int io2 = digitalRead(PIN_HLT331_IO2);
        if (io2 != lastIo2) {
            LOG_DEBUG_HLT331("HLT331: IO2 -> %d", io2);
            lastIo2 = io2;
        }
        const uint32_t pulses = io1Pulses;
        if (pulses != lastIo1Pulses) {
            LOG_DEBUG_HLT331("HLT331: IO1 rising edge x%u (total %u), now %d", (unsigned)(pulses - lastIo1Pulses),
                             (unsigned)pulses, digitalRead(PIN_HLT331_IO1));
            lastIo1Pulses = pulses;
        }

        if (lastHeartbeatMs == 0 || Throttle::hasElapsed(lastHeartbeatMs, HEARTBEAT_MS)) {
            lastHeartbeatMs = Time::stampMillis();
            LOG_DEBUG_HLT331("HLT331: heartbeat, %u good / %u bad CRC / %u bad length frames, IO1=%d IO2=%d, %s",
                             (unsigned)parser.goodFrames(), (unsigned)parser.badCrcFrames(), (unsigned)parser.badLengthFrames(),
                             digitalRead(PIN_HLT331_IO1), io2, gaugeProbe());
            if (haveFrame)
                logFrame(fresh ? "last" : "last, stale");
        }
    }

    void logFrame(const char *why)
    {
        const uint8_t *d = parser.lastFrame();
        char hex[HLT331Parser::DATA_LENGTH * 3 + 1] = {};
        for (uint8_t i = 0; i < HLT331Parser::DATA_LENGTH; i++)
            snprintf(&hex[i * 3], 4, "%02x ", d[i]);
        const HLT331Status &s = parser.status();
        LOG_DEBUG_HLT331("HLT331 frame (%s): %s", why, hex);
        LOG_DEBUG_HLT331("HLT331 decoded: chg=%d pct=%u c1=%u c2=%u ntcHot=%d wl=%d fod=%d full=%d wlProto=%u fast=%u "
                         "ntc1=%dC ntc2=%dC bat=%umV bus=%umV/%umA wl=%umW",
                         s.charging, s.batteryPercent, s.c1State, s.c2State, s.ntcOverTemp, s.wirelessActive,
                         s.wirelessForeignObject, s.wirelessFull, s.wirelessProtocol, s.fastChargeMode, s.ntc1TempC, s.ntc2TempC,
                         s.batteryMv, s.busMv, s.busMa, (unsigned)s.wirelessMw);
        memcpy(lastLogged, d, sizeof(lastLogged));
        loggedFrame = true;
    }

    // Whether the board's fuel gauge answers, and its chip ID register.
    const char *gaugeProbe()
    {
#ifdef CW2217_ADDR
        Wire.beginTransmission(CW2217_ADDR);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0 || Wire.requestFrom((uint8_t)CW2217_ADDR, (uint8_t)1) != 1) {
            snprintf(gaugeText, sizeof(gaugeText), "gauge 0x%x no answer", CW2217_ADDR);
        } else {
            snprintf(gaugeText, sizeof(gaugeText), "gauge 0x%x id 0x%x", CW2217_ADDR, Wire.read());
        }
#else
        snprintf(gaugeText, sizeof(gaugeText), "no gauge defined");
#endif
        return gaugeText;
    }

    bool loggedFrame = false;
    uint8_t lastLogged[HLT331Parser::DATA_LENGTH] = {};
    bool wasFresh = false;
    int lastIo2 = -1;
    uint32_t lastIo1Pulses = 0;
    uint32_t lastHeartbeatMs = 0;
    char gaugeText[32] = {};
#endif

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
