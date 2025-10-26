#include "ButtonThread.h"
#include "meshUtils.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "input/InputBroker.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include "sleep.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif


#include "MeshRadio.h"
#include "graphics/Screen.h"



// Send message on a specific (private) channel. Will NOT send on channel 0.
static void sendMessageForPinWithChannel(int pin, uint8_t channel)
{
    // Don't send on public/default channel 0
    if (channel == 0) {
        LOG_INFO("ButtonThread: configured channel is 0, skipping send to avoid public channel");
        return;
    }

    // Build a dynamic message that reflects the actual GPIO pin number so it
    // always matches the wiring (user rewired to GPIO5/6/7, etc.). This avoids
    // keeping multiple conditional branches in sync and keeps the payload
    // predictable.
    char msgbuf[48];
    snprintf(msgbuf, sizeof(msgbuf), "Button GPIO%d pressed", pin);
    const char *msg = msgbuf;


    // Log for debugging: which pin/channel/message we're about to send
    const char *chname = channels.getName(channel);
    LOG_INFO("ButtonThread: pin=%d sending '%s' on channel=%u (%s)", pin, msg, channel, chname ? chname : "<unknown>");

    // Sanity check: make sure the configured channel index is valid
    if (channel >= channels.getNumChannels()) {
        LOG_ERROR("ButtonThread: configured channel %u is out of range (numChannels=%u). Skipping send.", (unsigned)channel,
                  (unsigned)channels.getNumChannels());
        return;
    }

    // Construct a mesh packet and send through MeshService so the message goes
    // through the standard routing/phone-cc pipeline. This mirrors what
    // CannedMessageModule::sendText does but is accessible from here.
    meshtastic_MeshPacket *p = nullptr;
    if (router) {
        p = router->allocForSending();
    }
    if (!p) {
        LOG_WARN("ButtonThread: failed to allocate packet for sending '%s'", msg);
        return;
    }

    // Prepare packet fields
    p->to = NODENUM_BROADCAST;
    p->channel = channel;
    // Button-originated UI messages are best-effort — don't request ACKs to reduce airtime and queue pressure
    p->want_ack = false;

    // Ensure this is treated as a TEXT message by receivers
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.want_response = false;

    // Copy payload but guard against overflow
    size_t msglen = strlen(msg);
    if (msglen > meshtastic_Constants_DATA_PAYLOAD_LEN) {
        LOG_WARN("ButtonThread: message too long (%u), truncating to %d", (unsigned)msglen, meshtastic_Constants_DATA_PAYLOAD_LEN);
        msglen = meshtastic_Constants_DATA_PAYLOAD_LEN;
    }
    p->decoded.payload.size = msglen;
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);

    // Debug: show exact payload bytes/size and a hex dump of the payload (do this before calling sendToMesh)
    LOG_DEBUG("ButtonThread: payload size=%u payload='%.*s'", (unsigned)p->decoded.payload.size, (int)p->decoded.payload.size, p->decoded.payload.bytes);
    // Hex dump for tools that prefer hex visibility
    {
        const uint8_t *bytes = (const uint8_t *)p->decoded.payload.bytes;
        size_t len = p->decoded.payload.size;
        char hexbuf[3 * 64] = {0};
        size_t pos = 0;
        for (size_t i = 0; i < len && pos + 3 < sizeof(hexbuf); ++i) {
            pos += snprintf(&hexbuf[pos], sizeof(hexbuf) - pos, "%02x ", bytes[i]);
        }
        LOG_DEBUG("ButtonThread: payload hex=%s", hexbuf);
    }

    // Info log for quick verification in serial monitors -- log BEFORE sendToMesh because service may free the packet
    LOG_INFO("ButtonThread: queuing packet to=%u channel=%u want_ack=%u size=%u", (unsigned)p->to, (unsigned)p->channel, (unsigned)p->want_ack, (unsigned)p->decoded.payload.size);

    // Send to mesh and phone (ccToPhone = true) so UI/ACKs behave like normal messages
    service->sendToMesh(p, RX_SRC_LOCAL, true);

    // Don't access 'p' after this point: ownership may have been transferred and packet freed by sendToMesh

    if (screen) {
        screen->showSimpleBanner(msg, 3000);
    }
}









using namespace concurrency;

#if HAS_BUTTON
#endif
ButtonThread::ButtonThread(const char *name) : OSThread(name)
{
    _originName = name;
}

bool ButtonThread::initButton(const ButtonConfig &config)
{
    if (inputBroker)
        inputBroker->registerSource(this);
    _longPressTime = config.longPressTime;
    _longLongPressTime = config.longLongPressTime;
    _pinNum = config.pinNumber;
    _activeLow = config.activeLow;
    _touchQuirk = config.touchQuirk;
    _intRoutine = config.intRoutine;
    _longLongPress = config.longLongPress;
    // store configured private channel (0 means public/broadcast)
    _channelIndex = config.privateChannel;

    userButton = OneButton(config.pinNumber, config.activeLow, config.activePullup);

    if (config.pullupSense != 0) {
        pinMode(config.pinNumber, config.pullupSense);
    }

    _singlePress = config.singlePress;
    userButton.attachClick(
        [](void *callerThread) -> void {
            ButtonThread *thread = (ButtonThread *)callerThread;
            thread->btnEvent = BUTTON_EVENT_PRESSED;
        },
        this);

    _longPress = config.longPress;
    userButton.attachLongPressStart(
        [](void *callerThread) -> void {
            ButtonThread *thread = (ButtonThread *)callerThread;
            // if (millis() > 30000) // hold off 30s after boot
            thread->btnEvent = BUTTON_EVENT_LONG_PRESSED;
        },
        this);
    userButton.attachLongPressStop(
        [](void *callerThread) -> void {
            ButtonThread *thread = (ButtonThread *)callerThread;
            // if (millis() > 30000) // hold off 30s after boot
            thread->btnEvent = BUTTON_EVENT_LONG_RELEASED;
        },
        this);

    if (config.doublePress != INPUT_BROKER_NONE) {
        _doublePress = config.doublePress;
        userButton.attachDoubleClick(
            [](void *callerThread) -> void {
                ButtonThread *thread = (ButtonThread *)callerThread;
                thread->btnEvent = BUTTON_EVENT_DOUBLE_PRESSED;
            },
            this);
    }

    if (config.triplePress != INPUT_BROKER_NONE) {
        _triplePress = config.triplePress;
        userButton.attachMultiClick(
            [](void *callerThread) -> void {
                ButtonThread *thread = (ButtonThread *)callerThread;
                thread->storeClickCount();
                thread->btnEvent = BUTTON_EVENT_MULTI_PRESSED;
            },
            this);
    }
    if (config.shortLong != INPUT_BROKER_NONE) {
        _shortLong = config.shortLong;
    }
#ifdef USE_EINK
    userButton.setDebounceMs(0);
#else
    userButton.setDebounceMs(1);
#endif
    userButton.setPressMs(_longPressTime);

    if (screen) {
        userButton.setClickMs(20);
    } else {
        userButton.setClickMs(BUTTON_CLICK_MS);
    }
    attachButtonInterrupts();
#ifdef ARCH_ESP32
    // Register callbacks for before and after lightsleep
    // Used to detach and reattach interrupts
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
    return true;
}

int32_t ButtonThread::runOnce()
{
    // If the button is pressed we suppress CPU sleep until release
    canSleep = true; // Assume we should not keep the board awake

    // Check for combination timeout
    if (waitingForLongPress && (millis() - shortPressTime) > BUTTON_COMBO_TIMEOUT_MS) {
        waitingForLongPress = false;
    }

    userButton.tick();
    canSleep &= userButton.isIdle();

    // Check if we should play lead-up sound during long press
    // Play lead-up when button has been held for BUTTON_LEADUP_MS but before long press triggers
    bool buttonCurrentlyPressed = isButtonPressed(_pinNum);

    // Detect start of button press
    if (buttonCurrentlyPressed && !buttonWasPressed) {
        buttonPressStartTime = millis();
        leadUpPlayed = false;
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    // Progressive lead-up sound system
    if (buttonCurrentlyPressed && (millis() - buttonPressStartTime) >= BUTTON_LEADUP_MS) {

        // Start the progressive sequence if not already active
        if (!leadUpSequenceActive) {
            leadUpSequenceActive = true;
            lastLeadUpNoteTime = millis();
            playNextLeadUpNote(); // Play the first note immediately
        }
        // Continue playing notes at intervals
        else if ((millis() - lastLeadUpNoteTime) >= 400) { // 400ms interval between notes
            if (playNextLeadUpNote()) {
                lastLeadUpNoteTime = millis();
            } else {
                leadUpPlayed = true;
            }
        }
    }

    // Reset when button is released
    if (!buttonCurrentlyPressed && buttonWasPressed) {
        leadUpSequenceActive = false;
        resetLeadUpSequence();
    }

    buttonWasPressed = buttonCurrentlyPressed;

    // new behavior
    if (btnEvent != BUTTON_EVENT_NONE) {
        InputEvent evt;
        evt.source = _originName;
        evt.kbchar = 0;
        evt.touchX = 0;
        evt.touchY = 0;
        switch (btnEvent) {
        case BUTTON_EVENT_PRESSED: {
            // Forward single press to InputBroker (but NOT as DOWN/SELECT, just forward a "button press" event)
            evt.inputEvent = _singlePress;
            // evt.kbchar = _singlePress; // todo: fix this. Some events are kb characters rather than event types
            this->notifyObservers(&evt);

            // Start tracking for potential combination
            waitingForLongPress = true;
            shortPressTime = millis();

            // Custom behavior: send a short text message for specific GPIO pins
            // on the configured private channel. This will NOT send on public
            // channel 0 (the helper ignores channel 0).
                // Throttle rapid button-triggered sends to avoid radio queue overload
                const uint32_t cooldownMs = 300; // 300ms cooldown between sends from this button
                uint32_t now = millis();
                if ((now - _lastSendMs) < cooldownMs) {
                    LOG_DEBUG("ButtonThread: skipping send for pin %d, cooldown active (%u ms left)", _pinNum,
                              (unsigned)(cooldownMs - (now - _lastSendMs)));
                } else {
                    _lastSendMs = now;
                    sendMessageForPinWithChannel(_pinNum, _channelIndex);
                }

            break;
        }
        case BUTTON_EVENT_LONG_PRESSED: {
            // Ignore if: TX in progress
            // Uncommon T-Echo hardware bug, LoRa TX triggers touch button
            if (_touchQuirk && RadioLibInterface::instance && RadioLibInterface::instance->isSending())
                break;

            // Check if this is part of a short-press + long-press combination
            if (_shortLong != INPUT_BROKER_NONE && waitingForLongPress &&
                (millis() - shortPressTime) <= BUTTON_COMBO_TIMEOUT_MS) {
                evt.inputEvent = _shortLong;
                // evt.kbchar = _shortLong;
                this->notifyObservers(&evt);
                // Play the combination tune
                playComboTune();

                break;
            }
            if (_longPress != INPUT_BROKER_NONE) {
                // Forward long press to InputBroker (but NOT as DOWN/SELECT, just forward a "button long press" event)
                evt.inputEvent = _longPress;
                this->notifyObservers(&evt);
            }
            // Reset combination tracking
            waitingForLongPress = false;

            break;
        }

        case BUTTON_EVENT_DOUBLE_PRESSED: { // not wired in if screen detected
            LOG_INFO("Double press!");

            // Reset combination tracking
            waitingForLongPress = false;

            evt.inputEvent = _doublePress;
            // evt.kbchar = _doublePress;
            this->notifyObservers(&evt);
            playComboTune();

            break;
        }

        case BUTTON_EVENT_MULTI_PRESSED: { // not wired in when screen is present
            LOG_INFO("Mulitipress! %hux", multipressClickCount);

            // Reset combination tracking
            waitingForLongPress = false;

            switch (multipressClickCount) {
            case 3:
                evt.inputEvent = _triplePress;
                // evt.kbchar = _triplePress;
                this->notifyObservers(&evt);
                playComboTune();
                break;

            // No valid multipress action
            default:
                break;
            } // end switch: click count

            break;
        } // end multipress event

            // Do actual shutdown when button released, otherwise the button release
        // may wake the board immediatedly.
        case BUTTON_EVENT_LONG_RELEASED: {

            LOG_INFO("LONG PRESS RELEASE AFTER %u MILLIS", millis() - buttonPressStartTime);
            if (millis() > 30000 && _longLongPress != INPUT_BROKER_NONE &&
                (millis() - buttonPressStartTime) >= _longLongPressTime && leadUpPlayed) {
                evt.inputEvent = _longLongPress;
                this->notifyObservers(&evt);
            }
            // Reset combination tracking
            waitingForLongPress = false;
            leadUpPlayed = false;

            break;
        }

        // doesn't handle BUTTON_EVENT_PRESSED_SCREEN BUTTON_EVENT_TOUCH_LONG_PRESSED BUTTON_EVENT_COMBO_SHORT_LONG
        default: {
            break;
        }
        }
    }
    btnEvent = BUTTON_EVENT_NONE;

    // only pull when the button is pressed, we get notified via IRQ on a new press
    if (!userButton.isIdle() || waitingForLongPress) {
        return 50;
    }
    return 100; // FIXME: Why can't we rely on interrupts and use INT32_MAX here?
}

/*
 * Attach (or re-attach) hardware interrupts for buttons
 * Public method. Used outside class when waking from MCU sleep
 */
void ButtonThread::attachButtonInterrupts()
{
    // Interrupt for user button, during normal use. Improves responsiveness.
    attachInterrupt(_pinNum, _intRoutine, CHANGE);
}

/*
 * Detach the "normal" button interrupts.
 * Public method. Used before attaching a "wake-on-button" interrupt for MCU sleep
 */
void ButtonThread::detachButtonInterrupts()
{
    detachInterrupt(_pinNum);
}

#ifdef ARCH_ESP32

// Detach our class' interrupts before lightsleep
// Allows sleep.cpp to configure its own interrupts, which wake the device on user-button press
int ButtonThread::beforeLightSleep(void *unused)
{
    detachButtonInterrupts();
    return 0; // Indicates success
}

// Reconfigure our interrupts
// Our class' interrupts were disconnected during sleep, to allow the user button to wake the device from sleep
int ButtonThread::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachButtonInterrupts();
    return 0; // Indicates success
}

#endif

// Non-static method, runs during callback. Grabs info while still valid
void ButtonThread::storeClickCount()
{
    multipressClickCount = userButton.getNumberClicks();
}