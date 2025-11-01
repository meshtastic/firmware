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

using namespace concurrency;

ButtonsLEDsAndMsgs::ButtonsLEDsAndMsgs(const char *name) : OSThread(name)
{
    _originName = name;
}

bool ButtonsLEDsAndMsgs::initButton(const ButtonConfigModules &config)
{
    // We do not register with InputBroker here. This module will send text
    // messages directly when button events occur, keeping behavior local to
    // this module and avoiding upstream INPUT_BROKER_* dependencies.
    _longPressTime = config.longPressTime;
    _longLongPressTime = config.longLongPressTime;
    _pinNum = config.pinNumber;
    _activeLow = config.activeLow;
    _touchQuirk = config.touchQuirk;
    _intRoutine = config.intRoutine;
    _longLongPress = config.longLongPress;
    // LED config (module-local)
    _ledPin = config.ledPin;
    _ledActiveLow = config.ledActiveLow;
    // Channel index for sending text messages from this button/module
    if (config.channelIndex != 0xFF) {
        _channelIndex = config.channelIndex;
    }
    bool ledDefaulted = false;
    // If caller didn't configure an LED, default to GPIO2 active-low
    if (_ledPin < 0) {
        _ledPin = 2;
        _ledActiveLow = true;
        ledDefaulted = true;
    }

    if (_ledPin >= 0) {
        pinMode(_ledPin, OUTPUT);
        // Ensure LED is off initially
        if (_ledActiveLow)
            digitalWrite(_ledPin, HIGH);
        else
            digitalWrite(_ledPin, LOW);
        _ledOnUntil = 0;
        LOG_INFO("ButtonsLEDsAndMsgs(%s): initialized LED pin=%d activeLow=%d defaulted=%d", _originName ? _originName : "(null)", _ledPin, _ledActiveLow ? 1 : 0, ledDefaulted ? 1 : 0);
    }

    // Initialize board-specific LEDs and buttons (per your wiring)
#ifdef GreenLED
    pinMode(GreenLED, OUTPUT);
    digitalWrite(GreenLED, HIGH); // active-low -> HIGH = off
#endif
#ifdef RedLED
    pinMode(RedLED, OUTPUT);
    digitalWrite(RedLED, HIGH);
#endif
    // Combined RGB startup blink: toggle all available board LEDs together so
    // the user can visually verify each color at boot. This remains a brief
    // blocking test during init to ensure visibility.
    bool anyLed = false;
#ifdef RedLED
    pinMode(RedLED, OUTPUT);
    digitalWrite(RedLED, HIGH);
    anyLed = true;
#endif
#ifdef GreenLED
    pinMode(GreenLED, OUTPUT);
    digitalWrite(GreenLED, HIGH);
    anyLed = true;
#endif
#ifdef BlueLED
    pinMode(BlueLED, OUTPUT);
    digitalWrite(BlueLED, HIGH);
    anyLed = true;
#endif
    if (anyLed) {
        if (Serial) {
            Serial.println("ButtonsLEDsAndMsgs: STARTUP RGB BLINK");
        }
        for (int i = 0; i < 3; ++i) {
#ifdef RedLED
            digitalWrite(RedLED, LOW);
#endif
#ifdef GreenLED
            digitalWrite(GreenLED, LOW);
#endif
#ifdef BlueLED
            digitalWrite(BlueLED, LOW);
#endif
            delay(300);
#ifdef RedLED
            digitalWrite(RedLED, HIGH);
#endif
#ifdef GreenLED
            digitalWrite(GreenLED, HIGH);
#endif
#ifdef BlueLED
            digitalWrite(BlueLED, HIGH);
#endif
            delay(200);
        }
        if (Serial) {
            Serial.println("ButtonsLEDsAndMsgs: STARTUP RGB BLINK done");
        }
    }

    // Configure buttons (active-low INPUT_PULLUP)
#ifdef RED_BUTTON_PIN
    pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(RED_BUTTON_PIN) == LOW) {
        LOG_DEBUG("ButtonsLEDsAndMsgs(%s): RED button (pin %d) is LOW at init", _originName ? _originName : "(null)", RED_BUTTON_PIN);
    }
#endif
#ifdef GREEN_BUTTON_PIN
    pinMode(GREEN_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(GREEN_BUTTON_PIN) == LOW) {
        LOG_DEBUG("ButtonsLEDsAndMsgs(%s): GREEN button (pin %d) is LOW at init", _originName ? _originName : "(null)", GREEN_BUTTON_PIN);
    }
#endif
#ifdef BLUE_BUTTON_PIN
    pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(BLUE_BUTTON_PIN) == LOW) {
        LOG_DEBUG("ButtonsLEDsAndMsgs(%s): BLUE button (pin %d) is LOW at init", _originName ? _originName : "(null)", BLUE_BUTTON_PIN);
    }
#endif

    // Initialize simple debounce state
    if (config.pullupSense != 0) {
        pinMode(config.pinNumber, config.pullupSense);
    }
    _singlePress = config.singlePress;
    _lastDebounceTime = 0;
    // Use a slightly larger debounce to tolerate bouncy switches; can be
    // tuned later if needed.
    _debounceMs = 50;
    _lastRawState = isButtonPressed(config.pinNumber);
    _stableState = _lastRawState;
    attachButtonInterrupts();
#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    // Used to detach and reattach interrupts
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
    LOG_INFO("ButtonsLEDsAndMsgs(%s): initButton pin=%u activeLow=%d activePullup=%d", _originName ? _originName : "(null)",
             (unsigned)config.pinNumber, (int)config.activeLow, (int)config.activePullup);
    // Observe incoming text messages so we can parse LED control commands
    if (textMessageModule) {
        textObserver.observe(textMessageModule);
    }
    return true;
}

int ButtonsLEDsAndMsgs::handleTextMessage(const meshtastic_MeshPacket *mp)
{
    if (!mp || !mp->decoded.payload.size)
        return 0;

    // Only parse simple text commands of the form: "LED:<ch>:ON" or "LED:<ch>:OFF"
    const char *s = (const char *)mp->decoded.payload.bytes;
    size_t len = mp->decoded.payload.size;
    if (len < 6)
        return 0;

    if (strncmp(s, "LED:", 4) != 0)
        return 0;

    // Parse channel number
    const char *p = s + 4;
    int ch = atoi(p);
    // find ':' after channel
    const char *col = strchr(p, ':');
    if (!col)
        return 0;
    const char *cmd = col + 1;

    // Only act on commands that match our configured channel index
    if (_channelIndex != 0xFF && ch != (int)_channelIndex)
        return 0;

    if (strncmp(cmd, "ON", 2) == 0) {
        if (_ledPin >= 0) {
            setLed(true);
            _ledOnUntil = millis() + 5000; // leave on for 5s (commanded)
        }
        return 1;
    } else if (strncmp(cmd, "OFF", 3) == 0) {
        if (_ledPin >= 0) {
            setLed(false);
            _ledOnUntil = 0;
        }
        return 1;
    }
    return 0;
}

int32_t ButtonsLEDsAndMsgs::runOnce()
{
    // Turn off LED if its timer expired (non-blocking)
    if (_ledPin >= 0 && _ledOnUntil != 0 && millis() >= _ledOnUntil) {
        setLed(false);
        _ledOnUntil = 0;
    }

    // Read the raw pin state early so we can make sleep decisions and use it
    // as the first sample for debounce.
    bool rawState = isButtonPressed(_pinNum);

    // If the button is pressed we suppress CPU sleep until release. The
    // previous logic inverted this flag which allowed the CPU to sleep and
    // miss quick taps. When pressed -> prevent sleep (canSleep = false).
    canSleep = !rawState;

    // Check for combination timeout
    if (waitingForLongPress && (millis() - shortPressTime) > BUTTON_COMBO_TIMEOUT_MS) {
        waitingForLongPress = false;
    }

    // Simple polling-based debounce and edge-detect (not using OneButton)
    // Apply debounce to the raw sample we collected above. Trigger on the
    // stable transition to the pressed state.
    if (rawState != _lastRawState) {
        _lastDebounceTime = millis();
        _lastRawState = rawState;
    }
    if ((millis() - _lastDebounceTime) >= _debounceMs) {
        if (_stableState != rawState) {
            _stableState = rawState;
            // If the new stable state is 'pressed', trigger action
            if (_stableState) {
                LOG_DEBUG("ButtonsLEDsAndMsgs(%s): stable press detected on pin %u", _originName ? _originName : "(null)", (unsigned)_pinNum);
                triggerPressAction();
            }
        }
    }

    if (btnEvent != BUTTON_EVENT_NONE) {
            InputEvent evt;
            evt.source = _originName;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
        LOG_INFO("ButtonsLEDsAndMsgs(%s): preparing event %d from pin %u", _originName ? _originName : "(null)", (int)btnEvent,
                 (unsigned)_pinNum);
        switch (btnEvent) {
        case BUTTON_EVENT_PRESSED:
        case BUTTON_EVENT_LONG_PRESSED:
        case BUTTON_EVENT_DOUBLE_PRESSED:
        case BUTTON_EVENT_MULTI_PRESSED:
        case BUTTON_EVENT_LONG_RELEASED:
        case BUTTON_EVENT_PRESSED_SCREEN:
        case BUTTON_EVENT_TOUCH_LONG_PRESSED:
        case BUTTON_EVENT_COMBO_SHORT_LONG:
            // Treat any press type as a single press action when differentiation
            // is not required.
            triggerPressAction();
            waitingForLongPress = false;
            leadUpPlayed = false;
            break;
        default:
            break;
        }
    }
    btnEvent = BUTTON_EVENT_NONE;

    // Polling-based module; use a tick that matches the debounce window and
    // balances responsiveness with CPU usage.
    (void)waitingForLongPress; // not used in this simplified flow
    return 50;
}

void ButtonsLEDsAndMsgs::attachButtonInterrupts()
{
    // No-op: using polling debounce in runOnce() for greater reliability.
}

void ButtonsLEDsAndMsgs::detachButtonInterrupts()
{
    // No-op
}

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

void ButtonsLEDsAndMsgs::storeClickCount()
{
    // OneButton removed; we don't track multipress counts in the simplified
    // implementation. Ensure count is zeroed.
    multipressClickCount = 0;
}

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
        snprintf(buf, sizeof(buf), "BTN_PRESS %s", _originName ? _originName : "(unknown)");
        sendTextToChannel(buf, _channelIndex);
    }

    // Inject the input event into the standard InputBroker so other modules
    // and the UI receive canonical button events. If InputBroker is not
    // available (rare), fall back to local observers.
    if (inputBroker) {
        InputEvent ievt;
        ievt.source = _originName;
        ievt.kbchar = 0;
        ievt.touchX = 0;
        ievt.touchY = 0;
        ievt.inputEvent = _singlePress;
        inputBroker->injectInputEvent(&ievt);
    } else {
        InputEvent evt;
        evt.source = _originName;
        evt.kbchar = 0;
        evt.touchX = 0;
        evt.touchY = 0;
        evt.inputEvent = _singlePress;
        this->notifyObservers(&evt);
    }

    // Flash the module-local LED; restart timeout on repeated presses
    if (_ledPin >= 0) {
        setLed(true);
        _ledOnUntil = millis() + 500; // 500ms visible flash
    }
}

void ButtonsLEDsAndMsgs::setLed(bool on)
{
    if (_ledPin < 0)
        return;
    LOG_DEBUG("ButtonsLEDsAndMsgs(%s): setLed %d on pin %d activeLow=%d", _originName ? _originName : "(null)", on,
              _ledPin, _ledActiveLow ? 1 : 0);
    if (_ledActiveLow)
        digitalWrite(_ledPin, on ? LOW : HIGH);
    else
        digitalWrite(_ledPin, on ? HIGH : LOW);
}

ButtonsLEDsAndMsgs *buttonsLEDsAndMsgs = nullptr;

int ButtonsLEDsAndMsgs::handleInputEvent(const InputEvent *event)
{
    if (!event)
        return 0;

    // Map canonical input events to channel indices (example mapping)
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

    if (ch == 0)
        return 0;

    char buf[64];
    snprintf(buf, sizeof(buf), "Button event from %s", event->source ? event->source : "button");
    sendTextToChannel(buf, ch);
    return 1;
}

void ButtonsLEDsAndMsgs::sendTextToChannel(const char *text, uint8_t channel)
{
    uint32_t now = millis();
    if (now - _lastSendMs < 300)
        return; // rate limit
    _lastSendMs = now;

    if (!router || !service) {
        LOG_WARN("ButtonsLEDsAndMsgs: router/service not initialized, skipping send");
        return;
    }

    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_WARN("ButtonsLEDsAndMsgs: failed to allocate packet");
        return;
    }
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t len = strlen(text);
    if (len > sizeof(p->decoded.payload.bytes))
        len = sizeof(p->decoded.payload.bytes);
    memcpy(p->decoded.payload.bytes, text, len);
    p->decoded.payload.size = len;
    p->channel = channel;
    p->want_ack = false;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}