#include "NRF52RtttlTicker.h"

#ifdef ARCH_NRF52

#include "DebugConfiguration.h"
#include "freertosinc.h"
#include <NonBlockingRtttl.h>
#include <timers.h>

namespace NRF52RtttlTicker
{
namespace
{
// 5 ms is inaudible against the tens-of-ms stall being fixed, and holds the daemon to 200 wakeups/s.
// Must exceed one tick, or pdMS_TO_TICKS() rounds to zero and xTimerCreate() fails back into polling.
constexpr uint32_t kTickMs = 5;
static_assert(kTickMs * configTICK_RATE_HZ >= 1000, "kTickMs rounds to zero ticks");

TimerHandle_t timer = nullptr;
SemaphoreHandle_t lock = nullptr;
// True only while the timer is servicing a song; pump() polls from the main loop whenever it is not.
bool timerRunning = false;
bool reportedInitFailure = false;

void onTick(TimerHandle_t)
{
    // The main thread holds the lock only across begin()/stop(); skip this tick rather than block the timer task.
    if (xSemaphoreTake(lock, 0) != pdTRUE)
        return;
    // onTick -> rtttl::play -> tone -> applyConfiguration is the deepest chain on the timer daemon's
    // 256 word stack, shared with Bluefruit; measured 61 words peak on a T-Echo Plus.
    if (rtttl::isPlaying())
        rtttl::play();
    else
        xTimerStop(timer, 0); // song finished on its own
    xSemaphoreGive(lock);
}

bool ensureInit()
{
    if (timer)
        return true;
    if (!lock)
        lock = xSemaphoreCreateMutex();
    if (lock)
        timer = xTimerCreate("rtttl", pdMS_TO_TICKS(kTickMs), pdTRUE, nullptr, onTick);
    if (!timer && !reportedInitFailure) {
        // begin() runs again on every nag restart, so only complain the first time.
        reportedInitFailure = true;
        LOG_ERROR("RTTTL timer unavailable, falling back to main-loop playback");
    }
    return timer != nullptr;
}
} // namespace

void begin(uint8_t pin, const char *song)
{
    if (!ensureInit()) {
        rtttl::begin(pin, song);
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    rtttl::begin(pin, song);
    xSemaphoreGive(lock);
    // A start rejected by a full timer command queue must fall back to polling, or the song never advances.
    timerRunning = xTimerStart(timer, pdMS_TO_TICKS(10)) == pdPASS;
    if (!timerRunning)
        LOG_WARN("RTTTL timer start rejected, falling back to main-loop playback");
}

void pump()
{
    if (timerRunning || !rtttl::isPlaying())
        return;
    if (!lock) { // no mutex means ensureInit() never made a timer, so nothing can race us
        rtttl::play();
        return;
    }
    // A rejected start or stop leaves the auto-reload timer live even with timerRunning clear, so take
    // the lock rather than assume onTick() is idle. play() is time gated, so a skipped tick costs nothing.
    if (xSemaphoreTake(lock, 0) != pdTRUE)
        return;
    rtttl::play();
    xSemaphoreGive(lock);
}

void stop()
{
    if (!timer) {
        rtttl::stop();
        return;
    }
    // Unlike the start, a rejected stop is harmless: rtttl::stop() below clears the playing flag,
    // so the next onTick() stops the timer itself.
    (void)xTimerStop(timer, pdMS_TO_TICKS(10));
    timerRunning = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    rtttl::stop();
    xSemaphoreGive(lock);
}
} // namespace NRF52RtttlTicker

#endif
