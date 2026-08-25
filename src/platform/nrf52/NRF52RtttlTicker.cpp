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
constexpr uint32_t kTickMs = 2;

TimerHandle_t timer = nullptr;
SemaphoreHandle_t lock = nullptr;
// True only while the timer is servicing a song; pump() polls from the main loop whenever it is not.
bool timerRunning = false;

void onTick(TimerHandle_t)
{
    // The main thread holds the lock only across begin()/stop(); skip this tick rather than block the timer task.
    if (xSemaphoreTake(lock, 0) != pdTRUE)
        return;
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
    if (!timer)
        LOG_ERROR("RTTTL timer unavailable, falling back to main-loop playback");
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
    if (!timerRunning && rtttl::isPlaying())
        rtttl::play();
}

void stop()
{
    if (!timer) {
        rtttl::stop();
        return;
    }
    xTimerStop(timer, pdMS_TO_TICKS(10));
    timerRunning = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    rtttl::stop();
    xSemaphoreGive(lock);
}
} // namespace NRF52RtttlTicker

#endif
