#include "configuration.h"

#if HAS_HLT331
#include "DebugConfiguration.h"
#include "Power.h"
#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include "mesh/Throttle.h"
#include "power/CW2217.h"
#include "power/HLT331Parser.h"
#include "power/HLT331PowerStatus.h"
#include "power/PowerBank.h"
#include <Arduino.h>
#if HAS_CW2217
#include <Wire.h>
#include <string.h>
#endif

// HLT331_DEBUG=1 logs status changes with the raw frame, IO1/IO2 activity, data going stale, and the
// fuel gauge's registers next to the HLT331's battery reading. Costs no flash when off.
#ifndef HLT331_DEBUG
#define HLT331_DEBUG 0
#endif
#if HLT331_DEBUG
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
// No frame for this long and the last frame no longer describes the ports.
constexpr uint32_t STALE_MS = 3000;
constexpr uint32_t WARN_INTERVAL_MS = 60 * 1000;
constexpr uint32_t GAUGE_INTERVAL_MS = 3000;
#ifdef PIN_USB_VBUS_EN
// initVariant() connects C2's VBUS so a computer on C2 gets the boot log; frames start about 6 s in.
constexpr uint32_t BOOT_VBUS_HOLD_MS = 15 * 1000;
// C2 has to stop feeding the bank for this long before USB is cut, so a brief change does not drop it.
constexpr uint32_t VBUS_RELEASE_MS = 3000;
#endif

#if HLT331_DEBUG
constexpr uint32_t HEARTBEAT_MS = 10 * 1000;
volatile uint32_t io1Pulses = 0;
void io1Isr()
{
    io1Pulses = io1Pulses + 1;
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
        // Read now so Power's first reading at boot already has the battery.
        readGauge();
        asleep = digitalRead(PIN_HLT331_IO2) == LOW;
#ifdef PIN_USB_VBUS_EN
        vbusHoldUntilMs = Time::timerEndsAtMillis(BOOT_VBUS_HOLD_MS);
#endif
#if HLT331_DEBUG
        attachInterrupt(digitalPinToInterrupt(PIN_HLT331_IO1), io1Isr, RISING);
        lastIo2 = !asleep;
        LOG_DEBUG_HLT331("HLT331: setup, IO1=%d IO2=%d", digitalRead(PIN_HLT331_IO1), lastIo2);
        logGauge();
#endif
    }

    bool getStatus(PowerBankStatus &out) override { return hlt331MergeStatus(sources(), out); }

    bool hasDetail() const override { return true; }

    bool getDetail(PowerBankDetail &out) override
    {
        if (!haveFrame)
            return false;
        hlt331Detail(parser.status(), isFresh(), out);
        return true;
    }

  private:
    bool isFresh() const { return haveFrame && !Throttle::hasElapsed(lastFrameMs, STALE_MS); }

    HLT331Sources sources() const
    {
        HLT331Sources in;
        in.gaugeValid = gaugeValid;
        in.gaugeMv = gauge.voltageMv;
        in.gaugePercent = gauge.percent;
        in.haveFrame = haveFrame;
        in.fresh = isFresh();
        in.asleep = asleep;
        in.controller = parser.status();
        return in;
    }

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
        asleep = digitalRead(PIN_HLT331_IO2) == LOW;
#ifdef PIN_USB_VBUS_EN
        updateBoardVbus();
#endif

        const uint32_t rejected = parser.badCrcFrames() + parser.badLengthFrames();
        if (rejected != lastRejected && (lastWarnMs == 0 || Throttle::hasElapsed(lastWarnMs, WARN_INTERVAL_MS))) {
            lastRejected = rejected;
            lastWarnMs = Time::stampMillis();
            LOG_WARN("HLT331: rejected frames, %u bad CRC, %u bad length", (unsigned)parser.badCrcFrames(),
                     (unsigned)parser.badLengthFrames());
        }

        if (Throttle::hasElapsed(lastGaugeMs, GAUGE_INTERVAL_MS))
            readGauge();

        // Plugging in, unplugging and the battery appearing or going are worth more than Power's next poll.
        PowerBankStatus now;
        const bool available = getStatus(now);
        if (available != lastAvailable || now.charging != lastReported.charging ||
            now.inputPresent != lastReported.inputPresent) {
            lastAvailable = available;
            lastReported = now;
            if (power)
                power->setIntervalFromNow(0);
        }
#if HLT331_DEBUG
        debugLog(gotFrame);
#endif
        return POLL_INTERVAL_MS;
    }

#ifdef PIN_USB_VBUS_EN
    // Connected immediately when C2 starts feeding the bank; released once the boot hold is over and C2 has
    // stopped for VBUS_RELEASE_MS, so the bank can power a device plugged into C2.
    void updateBoardVbus()
    {
        if (hlt331WantsC2Vbus(isFresh(), parser.status())) {
            vbusUnwantedSinceMs = 0;
            setBoardVbus(true);
            return;
        }
        if (vbusUnwantedSinceMs == 0)
            vbusUnwantedSinceMs = Time::stampMillis();
        if (Throttle::deadlinePassed(vbusHoldUntilMs) && Throttle::hasElapsed(vbusUnwantedSinceMs, VBUS_RELEASE_MS))
            setBoardVbus(false);
    }

    void setBoardVbus(bool connect)
    {
        if (connect == boardVbus)
            return;
        boardVbus = connect;
        digitalWrite(PIN_USB_VBUS_EN, connect ? PIN_USB_VBUS_EN_ACTIVE : !PIN_USB_VBUS_EN_ACTIVE);
        LOG_INFO("HLT331: C2 VBUS %s", connect ? "connected to the board, USB on" : "released for C2 output, USB off");
    }

    bool boardVbus = true; // initVariant() connects it
    uint32_t vbusHoldUntilMs = 0;
    uint32_t vbusUnwantedSinceMs = 0;
#endif

    void readGauge()
    {
        lastGaugeMs = Time::getMillis();
#if HAS_CW2217
        // Read twice: a conversion landing between bytes tears the voltage and charge words.
        uint8_t first[CW2217::REG_COUNT];
        gaugeValid = false;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (!readGaugeRegisters(first) || !readGaugeRegisters(gaugeRegs))
                return;
            if (memcmp(&first[CW2217::REG_VCELL], &gaugeRegs[CW2217::REG_VCELL], 4) == 0) {
                gaugeValid = CW2217::decode(gaugeRegs, gauge);
                return;
            }
        }
#endif
    }

#if HAS_CW2217
    static bool readGaugeRegisters(uint8_t regs[CW2217::REG_COUNT])
    {
        Wire.beginTransmission(CW2217_ADDR);
        Wire.write(CW2217::REG_CHIP_ID);
        if (Wire.endTransmission() != 0 || Wire.requestFrom((uint8_t)CW2217_ADDR, CW2217::REG_COUNT) != CW2217::REG_COUNT)
            return false;
        for (uint8_t i = 0; i < CW2217::REG_COUNT; i++)
            regs[i] = Wire.read();
        return true;
    }

    uint8_t gaugeRegs[CW2217::REG_COUNT] = {};
#endif

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
                LOG_DEBUG_HLT331("HLT331: no frame for %u ms", (unsigned)STALE_MS);
            wasFresh = fresh;
        }

        const int io2 = !asleep;
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
            PowerBankStatus s;
            const bool available = getStatus(s);
            LOG_DEBUG_HLT331("HLT331: heartbeat, %u good / %u bad CRC / %u bad length frames, IO1=%d IO2=%d",
                             (unsigned)parser.goodFrames(), (unsigned)parser.badCrcFrames(), (unsigned)parser.badLengthFrames(),
                             digitalRead(PIN_HLT331_IO1), io2);
            if (available)
                LOG_DEBUG_HLT331("HLT331: reporting battery %u mV %d%% from %s, charging=%d input=%d", s.batteryMv,
                                 s.batteryPercent, s.batteryMv == gauge.voltageMv && gaugeValid ? "gauge" : "HLT331", s.charging,
                                 s.inputPresent);
            else
                LOG_DEBUG_HLT331("HLT331: reporting no battery");
            logGauge();
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

    // Registers 0x00-0x0F and the IC state, raw and decoded, to check the register map against the HLT331.
    void logGauge()
    {
#if HAS_CW2217
        char hex[CW2217::REG_COUNT * 3 + 1] = {};
        for (uint8_t i = 0; i < CW2217::REG_COUNT; i++)
            snprintf(&hex[i * 3], 4, "%02x ", gaugeRegs[i]);
        int icState = -1;
        Wire.beginTransmission(CW2217_ADDR);
        Wire.write(CW2217::REG_IC_STATE);
        if (Wire.endTransmission() == 0 && Wire.requestFrom((uint8_t)CW2217_ADDR, (uint8_t)1) == 1)
            icState = Wire.read();
        LOG_DEBUG_HLT331("CW2217 regs 00-0f: %sIC state 0x%x", hex, icState);
        if (gaugeValid)
            LOG_DEBUG_HLT331("CW2217 decoded: %u mV %u%% %d.%dC profile=%d", gauge.voltageMv, gauge.percent,
                             gauge.tempTenthsC / 10, abs(gauge.tempTenthsC % 10), gauge.profileLoaded);
        else
            LOG_DEBUG_HLT331("CW2217: no valid reading");
#else
        LOG_DEBUG_HLT331("HLT331: no fuel gauge on this board");
#endif
    }

    bool loggedFrame = false;
    uint8_t lastLogged[HLT331Parser::DATA_LENGTH] = {};
    bool wasFresh = false;
    int lastIo2 = -1;
    uint32_t lastIo1Pulses = 0;
    uint32_t lastHeartbeatMs = 0;
#endif

    HLT331Parser parser;
    bool haveFrame = false;
    bool asleep = false;
    uint32_t lastFrameMs = 0;
    uint32_t lastRejected = 0;
    uint32_t lastWarnMs = 0;
    CW2217::Reading gauge;
    bool gaugeValid = false;
    uint32_t lastGaugeMs = 0;
    PowerBankStatus lastReported;
    bool lastAvailable = false;
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
