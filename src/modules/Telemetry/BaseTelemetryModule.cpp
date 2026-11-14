#include "BaseTelemetryModule.h"
#include "UptimeClock.h"

#ifdef ARCH_ESP32
#include <esp_rtc_time.h>
// RTC memory survives a timer deep sleep, and the RTC clock measures the time actually away
RTC_DATA_ATTR static TelemetryScheduler::Retained retainedSchedules[TELEMETRY_SLEEP_SLOTS];
#elif defined(ARCH_NRF52)
#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include <cstring>
#ifdef FSCom
#define TELEMETRY_SLEEP_FILE
// The nRF52 sleep ends in a soft reset that re-initialises RAM, and the core's linker script has no
// .noinit, so the schedules go through a file: written once per sleep, consumed on the next boot
static constexpr const char *SLEEP_STATE_PATH = "/prefs/telem_sleep.bin";
struct SleepStateFile {
    static constexpr uint32_t MAGIC = 0x544c5331; // "TLS1"
    uint32_t magic;
    uint32_t sleepMs;     // planned; the low-power delay is timed by the 32 kHz RTC
    uint32_t committedMs; // uptime when the sleep started; each slot's clockUs holds its own save uptime
    TelemetryScheduler::Retained slots[TELEMETRY_SLEEP_SLOTS];
};
static SleepStateFile sleepState;
static bool sleepStateLoaded = false;

// Every boot reads and drops the file, so a sleep cut short by a power loss cannot be replayed later
static void loadSleepState()
{
    if (sleepStateLoaded)
        return;
    sleepStateLoaded = true;
    memset(&sleepState, 0, sizeof(sleepState));
    {
        concurrency::LockGuard g(spiLock);
        if (!FSCom.exists(SLEEP_STATE_PATH))
            return;
        auto f = FSCom.open(SLEEP_STATE_PATH, FILE_O_READ);
        const bool complete = f && f.read((uint8_t *)&sleepState, sizeof(sleepState)) == sizeof(sleepState);
        if (f)
            f.close();
        FSCom.remove(SLEEP_STATE_PATH);
        if (!complete)
            memset(&sleepState, 0, sizeof(sleepState));
    }
    // Only the soft reset that ends our sleep; a pin reset or power-on means the time away is unknown
    if (sleepState.magic != SleepStateFile::MAGIC || !(readResetReason() & POWER_RESETREAS_SREQ_Msk))
        memset(&sleepState, 0, sizeof(sleepState));
}

static void writeSleepState(uint32_t sleepMs)
{
    sleepState.magic = SleepStateFile::MAGIC;
    sleepState.sleepMs = sleepMs;
    sleepState.committedMs = Time::getMillis();
    concurrency::LockGuard g(spiLock);
    FSCom.remove(SLEEP_STATE_PATH);
    auto f = FSCom.open(SLEEP_STATE_PATH, FILE_O_WRITE);
    if (!f) {
        LOG_WARN("Telemetry: cannot keep the schedule across sleep");
        return;
    }
    f.write((const uint8_t *)&sleepState, sizeof(sleepState));
    f.close();
}
#endif
#endif

BaseTelemetryModule *BaseTelemetryModule::sleepParticipants[MAX_SLEEP_PARTICIPANTS] = {};
bool BaseTelemetryModule::sentToMeshThisWake = false;
uint8_t BaseTelemetryModule::numSleepParticipants = 0;

void BaseTelemetryModule::registerForSharedSleep()
{
    for (uint8_t i = 0; i < numSleepParticipants; i++) {
        if (sleepParticipants[i] == this)
            return;
    }
    if (numSleepParticipants < MAX_SLEEP_PARTICIPANTS)
        sleepParticipants[numSleepParticipants++] = this;
}

void BaseTelemetryModule::unregisterForSharedSleep()
{
    for (uint8_t i = 0; i < numSleepParticipants; i++) {
        if (sleepParticipants[i] != this)
            continue;
        sleepParticipants[i] = sleepParticipants[--numSleepParticipants];
        sleepParticipants[numSleepParticipants] = nullptr;
        return;
    }
}

uint32_t BaseTelemetryModule::sharedSleepBudgetMs()
{
    uint32_t budget = UINT32_MAX;
    for (uint8_t i = 0; i < numSleepParticipants; i++) {
        if (sleepParticipants[i]->isTelemetryBusy())
            return 0;
        const uint32_t needed = sleepParticipants[i]->msUntilNeededAwake();
        if (needed < budget)
            budget = needed;
    }
    if (budget == UINT32_MAX)
        return 0; // nobody schedules a wake-up, leave sleeping to the module's own interval
    return budget > TELEMETRY_BOOT_MARGIN_MS ? budget - TELEMETRY_BOOT_MARGIN_MS : 0;
}

uint32_t BaseTelemetryModule::sharedReadAnchorMs(const BaseTelemetryModule *self)
{
    for (uint8_t i = 0; i < numSleepParticipants; i++) {
        if (sleepParticipants[i] == self)
            continue;
        const uint32_t stamp = sleepParticipants[i]->readGridStampMs();
        if (stamp)
            return stamp;
    }
    return 0;
}

void BaseTelemetryModule::enterSharedDeepSleep()
{
    const uint32_t sleepMs = sharedSleepBudgetMs();
    if (sleepMs < TELEMETRY_MIN_DEEP_SLEEP_MS) {
        LOG_DEBUG("Telemetry: next wake-up in %ums, stay awake", (unsigned)sleepMs);
        return;
    }

    // Waking for every read, saving the NodeDB each time would wear the flash several times faster than
    // sleeping per mesh interval did: only after a mesh publish. Done here, before the sleep state is
    // stamped, so the seconds it takes are not missing from the time away.
    if (sentToMeshThisWake && nodeDB) {
        nodeDB->saveToDisk();
        sentToMeshThisWake = false;
    }

    for (uint8_t i = 0; i < numSleepParticipants; i++)
        sleepParticipants[i]->onTelemetryDeepSleep(sleepMs);

    // Again now that the save has taken its time and the hooks may have slept sensors that need waking early
    const uint32_t finalMs = sharedSleepBudgetMs();
    if (finalMs < TELEMETRY_MIN_DEEP_SLEEP_MS) {
        LOG_DEBUG("Telemetry: next wake-up in %ums after the pre-sleep work, stay awake", (unsigned)finalMs);
        return;
    }
#ifdef TELEMETRY_SLEEP_FILE
    writeSleepState(finalMs);
#endif

    LOG_INFO("Telemetry: deep sleep %ums until the next read or publish", (unsigned)finalMs);
    doDeepSleep(finalMs, true, true);
}

void BaseTelemetryModule::saveScheduleForSleep(TelemetrySleepSlot slot, const TelemetryScheduler &schedule)
{
    if (slot >= TELEMETRY_SLEEP_SLOTS)
        return;
#ifdef ARCH_ESP32
    schedule.saveForSleep(retainedSchedules[slot], Time::getMillis(), esp_rtc_get_time_us());
#elif defined(TELEMETRY_SLEEP_FILE)
    loadSleepState(); // so a later boot-time load cannot wipe what is saved here
    schedule.saveForSleep(sleepState.slots[slot], Time::getMillis(), Time::getMillis());
#else
    (void)schedule;
#endif
}

bool BaseTelemetryModule::restoreScheduleAfterSleep(TelemetrySleepSlot slot, TelemetryScheduler &schedule)
{
    if (slot >= TELEMETRY_SLEEP_SLOTS)
        return false;
    const uint32_t nowMs = Time::getMillis();
#ifdef ARCH_ESP32
    if (wakeCause != ESP_SLEEP_WAKEUP_TIMER)
        return false;
    TelemetryScheduler::Retained &r = retainedSchedules[slot];
    return schedule.restoreAfterSleep(r, nowMs, (uint32_t)((esp_rtc_get_time_us() - r.clockUs) / 1000));
#elif defined(TELEMETRY_SLEEP_FILE)
    loadSleepState();
    TelemetryScheduler::Retained &r = sleepState.slots[slot];
    // Awake after the save, then the planned sleep, then this boot's uptime
    const uint32_t awakeMs = sleepState.committedMs - (uint32_t)r.clockUs;
    return schedule.restoreAfterSleep(r, nowMs, awakeMs + sleepState.sleepMs + nowMs);
#else
    (void)schedule;
    (void)nowMs;
    return false;
#endif
}
