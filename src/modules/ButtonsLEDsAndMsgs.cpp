/* ===========================================================================================
    © Dale McBeath 2025  - Use and distribute freely, but give credit.
		
		
    HELTEC V3 GPIO Pins:   (this is difficult to nail down, as meshtastic use isn't clear on schematic free pins )

    x   0 -  User Button 
    x   1 -  VBAT Read
       2 - 
        3 -
    ?   4 -  not sure, had problems, but may have been code error
        5 - 
        6 -
        7 -  
    x   8 - 14 - LoRa chip SPI 
    x  17 - 18 - OLED display I2C
    x  19 - 21 - Secondary I2C bus (for sensors etc)
    ?  22 - 25 - not available on pins 
    x  26, 33, 34, 36, 37 - SPI
    x  27 thru 32 - not available on pins
    x  35 -  White LED on board - used for heart-beat and WRITE status 
    ?  38, thru 42 - JTAG programming on some boards (use with caution, not tested)
    x  43 - 44 - CP2102 chip UART (used for programming/serial console)
       45 - 
       46 - 
       47 - (sometimes used for GPS)
       48 - (sometimes used for GPS)


    - This board has 3 buttons and 3 common Anode LEDs
    - Buttons are active-low and need internal pull-ups
    - Button map: RED at GPIO5, GREEN at GPIO6, BLUE at GPIO45

   =========================================================================================== */

#include "ButtonsLEDsAndMsgs.h"
#include "meshUtils.h"
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "input/InputBroker.h"
#include "modules/TextMessageModule.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include "sleep.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
#include <ctype.h>

using namespace concurrency;

// Module-local pending text queue (best-effort retry when router allocation fails)
struct ModulePendingText {
    char text[64];
    uint8_t channel;
};
static const int PENDING_TEXTS_MODULE = 12;
static ModulePendingText pendingModuleTexts[PENDING_TEXTS_MODULE];
static int pendingModuleHead = 0;
static int pendingModuleTail = 0;

void enqueuePendingModuleText(const char *text, uint8_t channel)
{
    int next = (pendingModuleTail + 1) % PENDING_TEXTS_MODULE;
    if (next == pendingModuleHead) {
        // queue full, drop oldest by advancing head
        pendingModuleHead = (pendingModuleHead + 1) % PENDING_TEXTS_MODULE;
    }
    strncpy(pendingModuleTexts[pendingModuleTail].text, text, sizeof(pendingModuleTexts[pendingModuleTail].text) - 1);
    pendingModuleTexts[pendingModuleTail].text[sizeof(pendingModuleTexts[pendingModuleTail].text) - 1] = '\0';
    pendingModuleTexts[pendingModuleTail].channel = channel;
    pendingModuleTail = next;
}

int processPendingModuleTexts(int count)
{
    int sent = 0;
    while (pendingModuleHead != pendingModuleTail && sent < count) {
        auto &e = pendingModuleTexts[pendingModuleHead];
        meshtastic_MeshPacket *p = nullptr;
        if (router) p = router->allocForSending();
        if (!p) break; // stop trying if allocation still fails
        p->to = NODENUM_BROADCAST;
        p->channel = e.channel;
        p->want_ack = false;
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        size_t len = strlen(e.text);
        if (len > sizeof(p->decoded.payload.bytes)) len = sizeof(p->decoded.payload.bytes);
        memcpy(p->decoded.payload.bytes, e.text, len);
        p->decoded.payload.size = len;
        LOG_INFO("ButtonsLEDsAndMsgs: retrying queued text '%s' on channel %u", e.text, (unsigned)e.channel);
        service->sendToMesh(p, RX_SRC_LOCAL, true);
        pendingModuleHead = (pendingModuleHead + 1) % PENDING_TEXTS_MODULE;
        ++sent;
    }
    return sent;
}

ButtonsLEDsAndMsgs::ButtonsLEDsAndMsgs(const char *name) : OSThread(name)
{
    _originName = name;
}

bool ButtonsLEDsAndMsgs::initButton(const ButtonConfigModules &config)
{
    _longPressTime = config.longPressTime;
    _longLongPressTime = config.longLongPressTime;
    _pinNum = config.pinNumber;
    _activeLow = config.activeLow;
    _touchQuirk = config.touchQuirk;
    _intRoutine = config.intRoutine;
    _longLongPress = config.longLongPress;
    _ledPin = config.ledPin;
    _ledActiveLow = config.ledActiveLow;
    if (config.channelIndex != 0xFF) {
        _channelIndex = config.channelIndex;
    }

    bool ledDefaulted = false;
    if (_ledPin < 0) {
        _ledPin = 2;
        _ledActiveLow = true;
        ledDefaulted = true;
    }


    // DM: if this LED pin is valid, set it to un-lit, off  state via it's _ledActiveLow (config.ledActiveLow) state
    if (_ledPin >= 0) {
        pinMode(_ledPin, OUTPUT);
        if (_ledActiveLow)
            digitalWrite(_ledPin, HIGH);
        else
            digitalWrite(_ledPin, LOW);
        _ledOnUntil = 0;
        LOG_INFO("ButtonsLEDsAndMsgs(%s): initialized LED pin=%d activeLow=%d defaulted=%d", _originName ? _originName : "(null)", _ledPin, _ledActiveLow ? 1 : 0, ledDefaulted ? 1 : 0);
    }


    // Initialize board-specific LEDs (make sure they exist before using)
#ifdef RedLED
    pinMode(RedLED, OUTPUT);
    digitalWrite(RedLED, HIGH);
#endif
#ifdef GreenLED
    pinMode(GreenLED, OUTPUT);
    digitalWrite(GreenLED, HIGH);
#endif
#ifdef BlueLED
    pinMode(BlueLED, OUTPUT);
    digitalWrite(BlueLED, HIGH);
#endif


    // Check if any LED was defined above
    //
    bool anyLed = false;
#ifdef RedLED || defined(GreenLED) || defined(BlueLED)
    anyLed = true;
#endif


    // 
    if (anyLed) {
        _startupBlinkPending = true;
        _startupBlinkDone    = false;
        _startupBlinkPhase   = 0;
        _startupBlinkCount   = 0;
        if (Serial) {
            Serial.println("ButtonsLEDsAndMsgs: SCHEDULED STARTUP RGB BLINK");
        }
    }

    // Configure button pin (leave pullup handling to caller if desired)
    pinMode(_pinNum, INPUT_PULLUP);
    _debounceMs   = 50;
    _lastRawState = isButtonPressed(_pinNum);
    _stableState  = _lastRawState;

    // Observe incoming text messages so we can parse LED control commands
    if (textMessageModule) {
        textObserver.observe(textMessageModule);
    }

    LOG_INFO("ButtonsLEDsAndMsgs(%s): initButton pin=%u activeLow=%d", _originName ? _originName : "(null)", (unsigned)_pinNum, (int)_activeLow);
    return true;
}

int32_t ButtonsLEDsAndMsgs::runOnce()
{
    // Simple one-shot startup blink: when pending, turn the board LEDs on for ~200ms then restore.
    if (_startupBlinkPending && !_startupBlinkDone && router && service) {
        const uint32_t onMs = 200;
        if (_startupBlinkPhase == 0) {
            // start single on-phase
            _startupBlinkPhase = 1;
#ifdef RedLED
            digitalWrite(RedLED, LOW);
#endif
#ifdef GreenLED
            digitalWrite(GreenLED, LOW);
#endif
#ifdef BlueLED
            digitalWrite(BlueLED, LOW);
#endif
            _startupBlinkUntil = millis() + onMs;
            if (Serial) Serial.println("ButtonsLEDsAndMsgs: STARTUP single blink ON");
        } else {
            uint32_t now = millis();
            if (now >= _startupBlinkUntil) {
#ifdef RedLED
                digitalWrite(RedLED, HIGH);
#endif
#ifdef GreenLED
                digitalWrite(GreenLED, HIGH);
#endif
#ifdef BlueLED
                digitalWrite(BlueLED, HIGH);
#endif
                _startupBlinkDone = true;
                _startupBlinkPending = false;
                _startupBlinkPhase = 0;
                if (Serial) Serial.println("ButtonsLEDsAndMsgs: STARTUP single blink DONE");
            }
        }
    }

    // Non-blocking LED expiry
    if (_ledPin >= 0 && _ledOnUntil != 0 && millis() >= _ledOnUntil) {
        setLed(false);
        _ledOnUntil = 0;
    }

    // Retry queued texts (bounded per tick)
    processPendingModuleTexts(4);

    // Debounce sampling
    bool rawState = isButtonPressed(_pinNum);
    canSleep = !rawState;

    if (rawState != _lastRawState) {
        _lastDebounceTime = millis();
        _lastRawState = rawState;
    }
    if ((millis() - _lastDebounceTime) >= _debounceMs) {
        if (_stableState != rawState) {
            _stableState = rawState;
            if (_stableState) {
                LOG_DEBUG("ButtonsLEDsAndMsgs(%s): stable press detected on pin %u", _originName ? _originName : "(null)", (unsigned)_pinNum);
                triggerPressAction();
            }
        }
    }

    // If startup blink is still pending, poll more frequently to make the blink timing smooth
    if (_startupBlinkPending && !_startupBlinkDone) {
        return 10; // check every 10ms during startup blink
    }
    return 50;
}

void ButtonsLEDsAndMsgs::attachButtonInterrupts() { /* polling-based: no-op */ }
void ButtonsLEDsAndMsgs::detachButtonInterrupts() { /* no-op */ }

#ifdef ARCH_ESP32
int ButtonsLEDsAndMsgs::beforeLightSleep(void *unused)
{
    detachButtonInterrupts();
    return 0;
}

int ButtonsLEDsAndMsgs::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachButtonInterrupts();
    return 0;
}
#endif

void ButtonsLEDsAndMsgs::storeClickCount() { multipressClickCount = 0; }

void ButtonsLEDsAndMsgs::triggerPressAction()
{
    InputEvent evt;
    evt.source = _originName;
    evt.kbchar = 0;
    evt.touchX = 0;
    evt.touchY = 0;
    evt.inputEvent = _singlePress;
    this->notifyObservers(&evt);

    if (_channelIndex != 0xFF) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s pressed", _originName ? _originName : "button");
        sendTextToChannel(buf, _channelIndex);
    }

    if (inputBroker) {
        InputEvent ievt;
        ievt.source = _originName;
        ievt.kbchar = 0;
        ievt.touchX = 0;
        ievt.touchY = 0;
        ievt.inputEvent = _singlePress;
        inputBroker->injectInputEvent(&ievt);
    } else {
        InputEvent evt2;
        evt2.source = _originName;
        evt2.kbchar = 0;
        evt2.touchX = 0;
        evt2.touchY = 0;
        evt2.inputEvent = _singlePress;
        this->notifyObservers(&evt2);
    }

    if (_ledPin >= 0) {
        setLed(true);
        _ledOnUntil = millis() + 500;
    }
}

void ButtonsLEDsAndMsgs::setLed(bool on)
{
    if (_ledPin < 0) return;
    LOG_DEBUG("ButtonsLEDsAndMsgs(%s): setLed %d on pin %d activeLow=%d", _originName ? _originName : "(null)", on, _ledPin, _ledActiveLow ? 1 : 0);
    if (_ledActiveLow)
        digitalWrite(_ledPin, on ? LOW : HIGH);
    else
        digitalWrite(_ledPin, on ? HIGH : LOW);
}

ButtonsLEDsAndMsgs *buttonsLEDsAndMsgs = nullptr;

int ButtonsLEDsAndMsgs::handleInputEvent(const InputEvent *event)
{
    if (!event) return 0;
    uint8_t ch = 0;
    switch (event->inputEvent) {
    case INPUT_BROKER_CANCEL:
        ch = 1;
        break;
    case INPUT_BROKER_BACK:
        ch = 2;
        break;
    case INPUT_BROKER_ALT_PRESS:
        ch = 3;
        break;
    default:
        break;
    }
    if (ch == 0) return 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "Button event from %s", event->source ? event->source : "button");
    sendTextToChannel(buf, ch);
    return 1;
}

void ButtonsLEDsAndMsgs::sendTextToChannel(const char *text, uint8_t channel)
{
    uint32_t now = millis();
    if (now - _lastSendMs < 300) return; // rate limit
    _lastSendMs = now;

    // Show local feedback immediately
    if (screen) screen->showSimpleBanner(text, 2000);

    if (!router || !service) {
        LOG_WARN("ButtonsLEDsAndMsgs: router/service not initialized, enqueueing text");
        enqueuePendingModuleText(text, channel);
        return;
    }

    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_WARN("ButtonsLEDsAndMsgs: failed to allocate packet, enqueueing for retry");
        enqueuePendingModuleText(text, channel);
        return;
    }
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t len = strlen(text);
    if (len > sizeof(p->decoded.payload.bytes)) len = sizeof(p->decoded.payload.bytes);
    memcpy(p->decoded.payload.bytes, text, len);
    p->decoded.payload.size = len;
    p->channel = channel;
    p->want_ack = false;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

int ButtonsLEDsAndMsgs::handleTextMessage(const meshtastic_MeshPacket *mp)
{
    // Very small parser for LED commands: "LED:<pin>:ON" or "LED:<pin>:OFF"
    // Accepts either a GPIO pin number or a small index 1=Red,2=Green,3=Blue for convenience.
    if (!mp) return 0;
    if (mp->decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) return 0;
    const char *s = (const char *)mp->decoded.payload.bytes;
    if (!s) return 0;
    if (strncmp(s, "LED:", 4) == 0) {
        char idtok[32] = {0};
        char cmd[16] = {0};
        if (sscanf(s + 4, "%31[^:]:%15s", idtok, cmd) >= 1) {
            bool on = false;
            if (strcasecmp(cmd, "ON") == 0) on = true;

            int mappedPin = -1;

            // If idtok is numeric, treat as index or GPIO pin
            bool allDigits = true;
            for (size_t i = 0; i < strlen(idtok); ++i) {
                if (!isdigit((unsigned char)idtok[i])) { allDigits = false; break; }
            }
            if (allDigits && idtok[0] != '\0') {
                int pin = atoi(idtok);
                mappedPin = pin;
                // Allow index mapping: 1=Red, 2=Green, 3=Blue
                if (pin == 1) {
#ifdef RedLED
                    mappedPin = RedLED;
#endif
                } else if (pin == 2) {
#ifdef GreenLED
                    mappedPin = GreenLED;
#endif
                } else if (pin == 3) {
#ifdef BlueLED
                    mappedPin = BlueLED;
#endif
                }
            } else {
                // Accept color names: RedLED, RED, Red, R etc.
                if (strcasecmp(idtok, "RedLED") == 0 || strcasecmp(idtok, "RED") == 0 || strcasecmp(idtok, "Red") == 0 || strcasecmp(idtok, "R") == 0) {
#ifdef RedLED
                    mappedPin = RedLED;
#endif
                } else if (strcasecmp(idtok, "GreenLED") == 0 || strcasecmp(idtok, "GREEN") == 0 || strcasecmp(idtok, "Green") == 0 || strcasecmp(idtok, "G") == 0) {
#ifdef GreenLED
                    mappedPin = GreenLED;
#endif
                } else if (strcasecmp(idtok, "BlueLED") == 0 || strcasecmp(idtok, "BLUE") == 0 || strcasecmp(idtok, "Blue") == 0 || strcasecmp(idtok, "B") == 0) {
#ifdef BlueLED
                    mappedPin = BlueLED;
#endif
                } else {
                    LOG_WARN("ButtonsLEDsAndMsgs: unknown LED id '%s' in command", idtok);
                }
            }

            LOG_DEBUG("ButtonsLEDsAndMsgs: parsed LED cmd '%s' -> mappedPin=%d action=%s", idtok, mappedPin, on ? "ON" : "OFF");

            if (mappedPin >= 0) {
                if (mappedPin == _ledPin) {
                    setLed(on);
                } else {
                    LOG_DEBUG("ButtonsLEDsAndMsgs: LED cmd for mappedPin %d does not match module's LED pin %d", mappedPin, _ledPin);
                }
            }
        }
    }
    return 0;
}
