#include "configuration.h"

#ifdef HELTEC_MESH_POCKET_V2

#include "DebugConfiguration.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "concurrency/LockGuard.h"
#include "power/MeshPocketV2PowerBank.h"
#include <Arduino.h>
#include <Wire.h>

namespace
{
constexpr uint32_t HLT_POLL_INTERVAL_MS = 100;
constexpr uint32_t HLT_STATUS_INTERVAL_MS = 500;
constexpr uint32_t GAUGE_INTERVAL_MS = 3000;
constexpr uint32_t FAST_GAUGE_INTERVAL_MS = 500;
constexpr uint32_t HLT_STALE_MS = 3000;
constexpr uint32_t HLT_IO1_READ_WINDOW_MS = 120;
} // namespace

MeshPocketV2PowerBank *meshPocketV2PowerBank = nullptr;

namespace
{
void meshPocketV2HltIo1Isr()
{
    if (meshPocketV2PowerBank != nullptr)
        meshPocketV2PowerBank->handleHltIo1PulseFromIsr();
}
} // namespace

MeshPocketV2PowerBank::MeshPocketV2PowerBank() : concurrency::OSThread("MeshPocketV2PowerBank", HLT_POLL_INTERVAL_MS) {}

bool MeshPocketV2PowerBank::setup()
{
    if (started)
        return true;

    pinMode(PIN_IO_CSA, OUTPUT);
    digitalWrite(PIN_IO_CSA, LOW);

    gaugeStarted = gauge.begin(Wire);
    snapshot.cwReady = gaugeStarted;
    if (!gaugeStarted) {
        snapshot.cw.error = gauge.lastError();
        LOG_WARN("MeshPocketV2PowerBank: CW221X init failed, error=%d", (int)snapshot.cw.error);
    } else {
        readGauge(Time::stampMillis());
    }

    hltStarted = hlt331.begin(Serial2, PIN_9006_RX, PIN_9006_TX);
    if (!hltStarted) {
        snapshot.hlt.error = hlt331.lastError();
        LOG_WARN("MeshPocketV2PowerBank: HLT331 init failed, error=%d", (int)snapshot.hlt.error);
    } else {
        hlt331.configurePins(PIN_CW_INT, PIN_9006_IO1, PIN_9006_IO2, -1);
        attachInterrupt(digitalPinToInterrupt(PIN_9006_IO1), meshPocketV2HltIo1Isr, RISING);
        readHlt331(Time::stampMillis());
    }

    started = gaugeStarted || hltStarted;
    enabled = started;
    return started;
}

void MeshPocketV2PowerBank::handleHltIo1PulseFromIsr()
{
    if (hltStarted) {
        hltIo1Pending = true;
        hltIo1PulseCount++;
    }
}

bool MeshPocketV2PowerBank::copySnapshot(MeshPocketV2PowerBankSnapshot &out)
{
    concurrency::LockGuard guard(&lock);
    out = snapshot;
    return started;
}

bool MeshPocketV2PowerBank::copyPowerStatusFields(meshtastic::OptionalBool &hasBattery, meshtastic::OptionalBool &usbPowered,
                                                  meshtastic::OptionalBool &charging, int32_t &voltageMv, int8_t &percent)
{
    MeshPocketV2PowerBankSnapshot local;
    if (!copySnapshot(local))
        return false;

    const bool hasGauge = local.cwReady;
    const bool hasHlt = local.hltValid && local.hltIo2Working && local.hlt.ageMs <= HLT_STALE_MS;
    if (!hasGauge && !hasHlt)
        return false;

    hasBattery = meshtastic::OptUnknown;
    usbPowered = meshtastic::OptUnknown;
    charging = meshtastic::OptUnknown;
    voltageMv = -1;
    percent = 0;

    if (hasGauge) {
        hasBattery = meshtastic::OptTrue;
        voltageMv = local.cw.voltageMv;
        percent = local.cw.socPercent;
    } else if (hasHlt) {
        hasBattery = meshtastic::OptTrue;
        voltageMv = local.hlt.batteryMv;
        percent = local.hlt.socPercent;
    }

    if (hasHlt) {
        const bool inputPresent = local.hlt.charging || local.hlt.c1State == HLT331Serial::PORT_CHARGING ||
                                  local.hlt.c2State == HLT331Serial::PORT_CHARGING;
        usbPowered = inputPresent ? meshtastic::OptTrue : meshtastic::OptFalse;
        charging = local.hlt.charging ? meshtastic::OptTrue : meshtastic::OptFalse;
    }

    return true;
}

int32_t MeshPocketV2PowerBank::runOnce()
{
    if (!started)
        return disable();

    const uint32_t now = Time::stampMillis();

    if (hltStarted)
        hlt331.poll();

    if (hltStarted && consumeHltIo1Pending(now)) {
        hltIo1DrainActive = true;
        hltIo1DrainStartedMs = now;
        fastGaugeReadsRemaining = 3;
        {
            concurrency::LockGuard guard(&lock);
            snapshot.hltIo1DrainActive = true;
            snapshot.hltIo1Pending = true;
            snapshot.hltIo1PulseCount = hltIo1PulseCount;
            snapshot.hltLastIo1PulseMs = hltLastIo1PulseMs;
            snapshot.updatedMs = now;
        }
    }

    if (hltStarted && hltIo1DrainActive && Throttle::hasElapsed(hltIo1DrainStartedMs, HLT_IO1_READ_WINDOW_MS)) {
        hltIo1DrainActive = false;
        hltIo1PulseEventPending = true;
        readHlt331(now);
    }

    if (hltStarted && (lastHltReadMs == 0 || Throttle::hasElapsed(lastHltReadMs, HLT_STATUS_INTERVAL_MS)))
        readHlt331(now);

    const uint32_t gaugeInterval = fastGaugeReadsRemaining > 0 ? FAST_GAUGE_INTERVAL_MS : GAUGE_INTERVAL_MS;
    if (gaugeStarted && (lastGaugeReadMs == 0 || Throttle::hasElapsed(lastGaugeReadMs, gaugeInterval)))
        readGauge(now);

    return HLT_POLL_INTERVAL_MS;
}

bool MeshPocketV2PowerBank::consumeHltIo1Pending(uint32_t now)
{
    noInterrupts();
    const bool pending = hltIo1Pending;
    hltIo1Pending = false;
    const uint32_t pulseCount = hltIo1PulseCount;
    interrupts();

    if (pending) {
        hltLastIo1PulseMs = now;
        concurrency::LockGuard guard(&lock);
        snapshot.hltIo1Pending = true;
        snapshot.hltIo1PulseCount = pulseCount;
        snapshot.hltLastIo1PulseMs = hltLastIo1PulseMs;
    }

    return pending;
}

void MeshPocketV2PowerBank::readGauge(uint32_t now)
{
    CW221X::Status status = {};
    const bool ready = gauge.readStatus(status);
    if (!ready) {
        status.ready = false;
        status.error = gauge.lastError();
    }

    {
        concurrency::LockGuard guard(&lock);
        snapshot.cwReady = ready;
        snapshot.cw = status;
        snapshot.cwUpdatedMs = now;
        updateSnapshotLocked(now);
    }

    lastGaugeReadMs = now;
    if (fastGaugeReadsRemaining > 0)
        fastGaugeReadsRemaining--;

    publishEvent(MeshPocketV2PowerBankEventKind::PageRefresh);
}

void MeshPocketV2PowerBank::readHlt331(uint32_t now)
{
    HLT331Serial::Status status = {};
    const bool valid = hlt331.readStatus(status);

    const bool publishIo1Pulse = hltIo1PulseEventPending;
    hltIo1PulseEventPending = false;

    MeshPocketV2PowerBankEventKind eventKind =
        publishIo1Pulse ? MeshPocketV2PowerBankEventKind::Io1Pulse : MeshPocketV2PowerBankEventKind::PageRefresh;

    {
        concurrency::LockGuard guard(&lock);
        snapshot.hltValid = valid;
        snapshot.hlt = status;
        snapshot.hltIo2Working = status.systemWorking;
        updateSnapshotLocked(now);

        // Keep the last valid state across communication gaps.
        if (valid && status.ageMs <= HLT_STALE_MS) {
            const bool stateChanged =
                !hasWakeStatus || status.systemWorking != lastWakeStatus.systemWorking ||
                (status.systemWorking &&
                 (status.charging != lastWakeStatus.charging || status.c1State != lastWakeStatus.c1State ||
                  status.c2State != lastWakeStatus.c2State || status.wirelessWork != lastWakeStatus.wirelessWork ||
                  status.wirelessFull != lastWakeStatus.wirelessFull));
            const bool faultKnown = snapshot.fault || (snapshot.cwReady && status.systemWorking);
            if (stateChanged) {
                eventKind = MeshPocketV2PowerBankEventKind::StatusEdge;
                fastGaugeReadsRemaining = 3;
            } else if (faultKnown && snapshot.fault != lastFault) {
                eventKind = MeshPocketV2PowerBankEventKind::FaultEdge;
                fastGaugeReadsRemaining = 3;
            }
            lastWakeStatus = status;
            hasWakeStatus = true;
            if (faultKnown)
                lastFault = snapshot.fault;
        }
    }

    lastHltReadMs = now;
    publishEvent(eventKind);
}

void MeshPocketV2PowerBank::updateSnapshotLocked(uint32_t now)
{
    snapshot.hltIo1DrainActive = hltIo1DrainActive;
    snapshot.hltIo1Pending = hltIo1Pending;
    snapshot.hltIo1PulseCount = hltIo1PulseCount;
    snapshot.hltLastIo1PulseMs = hltLastIo1PulseMs;
    snapshot.active = snapshot.hltValid && snapshot.hlt.ageMs <= HLT_STALE_MS && computeActive(snapshot.hlt);
    snapshot.fault = computeFault(snapshot.hlt, snapshot.cw);
    snapshot.updatedMs = now;
}

bool MeshPocketV2PowerBank::computeActive(const HLT331Serial::Status &status) const
{
    if (!status.systemWorking)
        return false;

    return status.charging || status.c1State == HLT331Serial::PORT_CHARGING || status.c1State == HLT331Serial::PORT_DISCHARGING ||
           status.c2State == HLT331Serial::PORT_CHARGING || status.c2State == HLT331Serial::PORT_DISCHARGING ||
           status.wirelessWork || status.busMv > 0 || status.systemWorking;
}

bool MeshPocketV2PowerBank::computeFault(const HLT331Serial::Status &status, const CW221X::Status &cwStatus) const
{
    const bool hltLive = status.valid && status.systemWorking && status.ageMs <= HLT_STALE_MS;
    return (hltLive && (status.ntcOverTemp || status.wirelessFod)) || (cwStatus.ready && !cwStatus.profile.matches);
}

void MeshPocketV2PowerBank::publishEvent(MeshPocketV2PowerBankEventKind kind)
{
    MeshPocketV2PowerBankEvent event;
    {
        concurrency::LockGuard guard(&lock);
        event.kind = kind;
        event.active = snapshot.active;
        event.fault = snapshot.fault;
    }
    onEvent.notifyObservers(&event);
}

void meshPocketV2PowerBankSetup()
{
    if (meshPocketV2PowerBank == nullptr)
        meshPocketV2PowerBank = new MeshPocketV2PowerBank();
    meshPocketV2PowerBank->setup();
}

bool meshPocketV2PowerBankCopySnapshot(MeshPocketV2PowerBankSnapshot &out)
{
    return meshPocketV2PowerBank != nullptr && meshPocketV2PowerBank->copySnapshot(out);
}

bool meshPocketV2PowerBankCopyPowerStatusFields(meshtastic::OptionalBool &hasBattery, meshtastic::OptionalBool &usbPowered,
                                                meshtastic::OptionalBool &charging, int32_t &voltageMv, int8_t &percent)
{
    return meshPocketV2PowerBank != nullptr &&
           meshPocketV2PowerBank->copyPowerStatusFields(hasBattery, usbPowered, charging, voltageMv, percent);
}

#endif // HELTEC_MESH_POCKET_V2
