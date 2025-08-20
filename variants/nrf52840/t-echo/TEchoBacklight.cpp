#include "TEchoBacklight.h"

#if defined(TTGO_T_ECHO) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

#include "RadioLibInterface.h"
#include "configuration.h"

TEchoBacklight *tEchoBacklight = nullptr;

TEchoBacklight::TEchoBacklight() : OSThread("TEchoBacklight")
{
    setInterval(POLL_INTERVAL_MS);
    OSThread::disable();
}

void TEchoBacklight::setPin(uint8_t pin)
{
    pinMode(pin, OUTPUT);
    off();
}

void TEchoBacklight::start()
{
    pinMode(PIN_BUTTON_TOUCH, INPUT_PULLUP);
    attachInterrupt(PIN_BUTTON_TOUCH, touchISR, FALLING);
}

int32_t TEchoBacklight::runOnce()
{
    bool awaitingRelease = false;
    
    switch (state) {
    case REST:
        break;
        
    case IRQ:
        if (!RadioLibInterface::instance || RadioLibInterface::instance->isSending()) {
            LOG_INFO("TEchoBacklight: Touch ignored - radio transmitting");
            state = REST;
            break;
        }
        LOG_INFO("TEchoBacklight: Touch detected - peek()");
        peek();
        state = POLLING_UNFIRED;
        awaitingRelease = true;
        break;
        
    case POLLING_UNFIRED: {
        uint32_t length = millis() - irqAtMillis;
        
        if (!isTouchPressed()) {
            state = REST;
            if (length > DEBOUNCE_MS && length < LATCH_TIME_MS) {
                LOG_INFO("TEchoBacklight: Short press (%lums) - off()", length);
                off();
            } else {
                LOG_INFO("TEchoBacklight: Touch released too quick (%lums) - debounced", length);
            }
        } else {
            awaitingRelease = true;
            if (length >= LATCH_TIME_MS) {
                LOG_INFO("TEchoBacklight: Long press (%lums) - starting latch blink", length);
                state = BLINKING;
                blinkStartTime = millis();
                blinkStep = 0;
                setBacklight(false);
            }
        }
        break;
    }
    
    case BLINKING: {
        uint32_t elapsed = millis() - blinkStartTime;
        if (elapsed >= BLINK_DELAY_MS) {
            blinkStep++;
            blinkStartTime = millis();
            
            setBacklight(blinkStep % 2 == 1);
            
            if (blinkStep == BLINK_STEPS) {
                backlightLatched = true;
                state = POLLING_FIRED;
                LOG_INFO("TEchoBacklight: Blink complete - latched ON");
            }
        }
        awaitingRelease = true;
        break;
    }
    
    case POLLING_FIRED:
        if (!isTouchPressed()) {
            LOG_INFO("TEchoBacklight: Long press released");
            state = REST;
        } else {
            awaitingRelease = true;
        }
        break;
    }
    
    if (!awaitingRelease) {
        stopThread();
    }
    
    return POLL_INTERVAL_MS;
}

void TEchoBacklight::setBacklight(bool on)
{
    digitalWrite(PIN_EINK_EN, on ? HIGH : LOW);
}

bool TEchoBacklight::isTouchPressed()
{
    return digitalRead(PIN_BUTTON_TOUCH) == LOW;
}

void TEchoBacklight::peek()
{
    setBacklight(true);
    backlightLatched = false;
}

void TEchoBacklight::latch()
{
    if (backlightLatched) {
        LOG_INFO("TEchoBacklight: latch() - turning OFF");
        backlightLatched = false;
        setBacklight(false);
    } else {
        LOG_INFO("TEchoBacklight: latch() - turning ON");
        backlightLatched = true;
        setBacklight(true);
    }
}

void TEchoBacklight::off()
{
    backlightLatched = false;
    setBacklight(false);
}

void TEchoBacklight::touchISR()
{
    static volatile bool isrRunning = false;
    
    if (!isrRunning && tEchoBacklight) {
        isrRunning = true;
        if (tEchoBacklight->state == REST) {
            tEchoBacklight->state = IRQ;
            tEchoBacklight->irqAtMillis = millis();
            tEchoBacklight->startThread();
            LOG_INFO("TEchoBacklight: ISR triggered - starting thread");
        }
        isrRunning = false;
    }
}

void TEchoBacklight::startThread()
{
    if (!OSThread::enabled) {
        OSThread::setInterval(POLL_INTERVAL_MS);
        OSThread::enabled = true;
    }
}

void TEchoBacklight::stopThread()
{
    if (OSThread::enabled) {
        OSThread::disable();
    }
    state = REST;
}

#endif
