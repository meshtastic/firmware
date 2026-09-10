#ifdef SEEED_WIO_TRACKER_L2

#include "WakeKey.h"
#include "DebugConfiguration.h"
#include "PowerFSM.h"
#include "SPILock.h"
#include "graphics/Screen.h"     // BaseUI
#include "graphics/TFTDisplay.h" // BaseUI
#include "main.h"

#if defined(HAS_TFT) && HAS_TFT
#include "graphics/DeviceScreen.h" // MUI
extern DeviceScreen *deviceScreen;
#endif

#include PCA95X5_INC
extern PCA95X5_CLS io;

// wake button handling
static bool isPca9535WakeKeyPressed()
{
    concurrency::LockGuard guard(spiLock);
    return !io.digitalRead(EXPANDS_BTN_WAKE_UP);
}

WakeKeyInterruptThread *WakeKeyInterruptThread::wakeKey = nullptr;

WakeKeyInterruptThread *WakeKeyInterruptThread::instance(void)
{
    if (!wakeKey)
        wakeKey = new WakeKeyInterruptThread();
    return wakeKey;
}

WakeKeyInterruptThread::WakeKeyInterruptThread() : concurrency::OSThread("WioL2WakeKeyInt", SAMPLE_MS)
{
    // Do not run unless an edge arrives.
    OSThread::disable();
#ifdef ARCH_ESP32
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
}

void WakeKeyInterruptThread::begin(void)
{
    pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
    attachInterrupt(BOARD_PCA9535_INT, WakeKeyInterruptThread::isr, FALLING);
}

int32_t WakeKeyInterruptThread::runOnce(void)
{
    const uint32_t now = millis();

    // Safe, sequential handling of the edge flag outside the ISR context
    if (rawIrqSignaled) {
        rawIrqSignaled = false;
        if (state == State::REST) {
            state = State::IRQ_PENDING;
            irqAtMs = now;
        }
    }

    // Ignore side-key handling while BOOT/user button is held.
    if (digitalRead(BUTTON_PIN) == LOW) {
        LOG_WARN("ignore wake button press");
        resetStateAndStop();
        return OSThread::disable();
    }

    switch (state) {
    case State::IRQ_PENDING:
        // Initial debounce after expander interrupt edge.
        if ((uint32_t)(now - irqAtMs) < DEBOUNCE_MS) {
            return SAMPLE_MS;
        }

        if (isPca9535WakeKeyPressed()) {
            LOG_DEBUG("wake button pressed");
#if defined(HAS_TFT) && HAS_TFT
            if (deviceScreen)
                deviceScreen->toggleDisplay();
#endif
#if defined(HAS_SCREEN) && HAS_SCREEN
            if (screen)
                screen->setOn(sleeping);
#endif
            if (sleeping) {
                powerFSM.trigger(EVENT_PRESS);
            }
            sleeping = !sleeping;
            state = State::PRESSED;
            return SAMPLE_MS;
        }

        // Spurious/cleared edge.
        resetStateAndStop();
        return OSThread::disable();

    case State::PRESSED: {
        if (isPca9535WakeKeyPressed()) {
            return SAMPLE_MS;
        }

        resetStateAndStop();
        return OSThread::disable();
    }

    case State::REST:
    default:
        return OSThread::disable();
    }
}

void IRAM_ATTR WakeKeyInterruptThread::isr(void)
{
    if (wakeKey) {
        wakeKey->rawIrqSignaled = true;
        wakeKey->enabled = true;
        wakeKey->setInterval(0); // TODO
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
        runASAP = true;
    }
}

void WakeKeyInterruptThread::onInterruptEdge(void)
{
    if (state != State::REST) {
        return;
    }

    state = State::IRQ_PENDING;
    irqAtMs = millis();
    startThread();
}

void WakeKeyInterruptThread::startThread(void)
{
    if (!OSThread::enabled) {
        OSThread::setIntervalFromNow(0);
        OSThread::enabled = true;
        runASAP = true;
    }
}

void WakeKeyInterruptThread::resetStateAndStop(void)
{
    state = State::REST;
    if (OSThread::enabled) {
        OSThread::disable();
    }
}

#ifdef ARCH_ESP32
int WakeKeyInterruptThread::onLightSleep(void *)
{
    detachInterrupt(BOARD_PCA9535_INT);
    // Clear any latched PCA9535 interrupt before enabling GPIO wake.
    // If INT is left asserted low, light sleep exits immediately.
    spiLock->lock();
    volatile bool dummy = io.digitalRead(EXPANDS_BTN_WAKE_UP);
    (void)dummy;
    spiLock->unlock();
    resetStateAndStop();
    sleeping = true;
    return 0;
}

int WakeKeyInterruptThread::onLightSleepEnd(esp_sleep_wakeup_cause_t cause)
{
    (void)cause;
    // Consume any pending interrupt source before reattaching ISR.
    // Check BEFORE clearing: if INT is still asserted the button may still be held,
    // and no new falling edge will fire until it is released.
    bool intAsserted = (digitalRead(BOARD_PCA9535_INT) == LOW);
    spiLock->lock();
    (void)io.digitalRead(EXPANDS_BTN_WAKE_UP);
    spiLock->unlock();
    pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
    attachInterrupt(BOARD_PCA9535_INT, WakeKeyInterruptThread::isr, FALLING);
    if (intAsserted) {
        onInterruptEdge();
    }
    return 0;
}

#endif

#endif