#include "kbI2cBase.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"
#include <algorithm>

#if defined(T_DECK_PRO)
#include "TDeckProKeyboard.h"
#include "main.h"
#elif defined(T_LORA_PAGER)
#include "TLoraPagerKeyboard.h"
#elif defined(M5STACK_CARDPUTER_ADV)
#include "CardputerKeyboard.h"
#elif defined(HACKADAY_COMMUNICATOR)
#include "HackadayCommunicatorKeyboard.h"
#else
#include "TCA8418Keyboard.h"
#endif

#if defined(ELECROW_ThinkNode_M9)
#include "STC8HKeyboard.h"
#include "graphics/Screen.h"                    // for the global `screen` + FrameFocus
#include "graphics/draw/NotificationRenderer.h" // for resetBanner()
#endif

extern ScanI2C::DeviceAddress cardkb_found;
extern uint8_t kb_model;

#if defined(T_DECK_PRO)
// KB_INT falls when the TCA8418 queues a key event; the poll is only a fallback for an edge that never came
static constexpr uint32_t TCA_FALLBACK_POLL_MS = 1000;
static KbI2cBase *tcaInterruptThread = nullptr;
static uint32_t lastTcaReadMs = 0;
static bool tcaRecheck = false; // INT_STAT showed new events right after the last drain

static void IRAM_ATTR tcaKeyboardInterrupt()
{
    if (tcaInterruptThread) {
        tcaInterruptThread->setIntervalFromNow(0);
        // The main loop may already be asleep until its next scheduled thread: wake it, or the key waits for that
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
        runASAP = true;
    }
}
#endif

static constexpr uint32_t KEYBOARD_LIGHT_TYPING_MS = 3000;

KbI2cBase::KbI2cBase(const char *name)
    : concurrency::OSThread(name),
#if defined(T_DECK_PRO)
      TCAKeyboard(new TDeckProKeyboard())
#elif defined(T_LORA_PAGER)
      TCAKeyboard(new TLoraPagerKeyboard())
#elif defined(M5STACK_CARDPUTER_ADV)
      TCAKeyboard(new CardputerKeyboard())
#elif defined(HACKADAY_COMMUNICATOR)
      TCAKeyboard(new HackadayCommunicatorKeyboard())
#else
      TCAKeyboard(new TCA8418Keyboard())
#endif
{
    this->_originName = name;
}

KbI2cBase::~KbI2cBase() = default;

uint8_t read_from_14004(TwoWire *i2cBus, uint8_t reg, uint8_t *data, uint8_t length)
{
    uint8_t readflag = 0;
    i2cBus->beginTransmission(CARDKB_ADDR);
    i2cBus->write(reg);
    i2cBus->endTransmission(); // stop transmitting
    delay(20);
    i2cBus->requestFrom(CARDKB_ADDR, (int)length);
    int i = 0;
    while (i2cBus->available()) // slave may send less than requested
    {
        data[i++] = i2cBus->read(); // receive a byte as a proper uint8_t
        readflag = 1;
    }
    return readflag;
}

int32_t KbI2cBase::runOnce()
{
    if (!i2cBus) {
        switch (cardkb_found.port) {
        case ScanI2C::WIRE1:
#if WIRE_INTERFACES_COUNT == 2
            LOG_DEBUG("Use I2C Bus 1 (the second one)");
            // resolved via the scanner: WIRE1 may be a bridged bus rather
            // than the local Wire1 (e.g. SenseCAP Indicator)
            i2cBus = ScanI2CTwoWire::fetchI2CBus(cardkb_found);
#if defined(ELECROW_ThinkNode_M9)
            if (cardkb_found.address == TSTC8_KB_V1_ADDR) {
                Stc8HKeyBoard.begin(TSTC8_KB_V1_ADDR, &Wire1);
            }
#endif
            if (cardkb_found.address == BBQ10_KB_ADDR) {
                Q10keyboard.begin(BBQ10_KB_ADDR, i2cBus);
                Q10keyboard.setBacklight(0);
            }
            if (cardkb_found.address == MPR121_KB_ADDR) {
                MPRkeyboard.begin(MPR121_KB_ADDR, i2cBus);
            }
            if (cardkb_found.address == TCA8418_KB_ADDR) {
                TCAKeyboard->begin(TCA8418_KB_ADDR, i2cBus);
            }
            break;
#endif
        case ScanI2C::WIRE:
            LOG_DEBUG("Use I2C Bus 0 (the first one)");
            i2cBus = &Wire;
#if defined(ELECROW_ThinkNode_M9)
            if (cardkb_found.address == TSTC8_KB_V1_ADDR || cardkb_found.address == TSTC8_KB_V2_ADDR) {
                Stc8HKeyBoard.begin(cardkb_found.address, &Wire);
            }
#endif
            if (cardkb_found.address == BBQ10_KB_ADDR) {
                Q10keyboard.begin(BBQ10_KB_ADDR, &Wire);
                Q10keyboard.setBacklight(0);
            }
            if (cardkb_found.address == MPR121_KB_ADDR) {
                MPRkeyboard.begin(MPR121_KB_ADDR, &Wire);
            }
            if (cardkb_found.address == TCA8418_KB_ADDR) {
                TCAKeyboard->begin(TCA8418_KB_ADDR, &Wire);
            }
            break;
        case ScanI2C::NO_I2C:
        default:
            i2cBus = 0;
        }
#if defined(T_DECK_PRO)
        if (i2cBus && cardkb_found.address == TCA8418_KB_ADDR) {
            tcaInterruptThread = this;
            attachInterrupt(digitalPinToInterrupt(KB_INT), tcaKeyboardInterrupt, FALLING);
            lsObserver.observe(&notifyLightSleep);
            lsEndObserver.observe(&notifyLightSleepEnd);
        }
#endif
    }

    switch (kb_model) {
    case 0x11: { // BB Q10
        if (q10Backspace.due())
            notifyDeleteWord();
        int keyCount = Q10keyboard.keyCount();
        while (keyCount--) {
            const BBQ10Keyboard::KeyEvent key = Q10keyboard.keyEvent();
            if (key.key == 0x08 && key.state == BBQ10Keyboard::StateLongPress)
                q10Backspace.press();
            else if (key.key != 0x08 && key.state == BBQ10Keyboard::StatePress)
                q10Backspace.cancel(); // another key ends a hold, in case Backspace's release was lost
            // A hold that repeated has already deleted words: its release must not delete a character as well
            if (key.key == 0x08 && key.state == BBQ10Keyboard::StateRelease && q10Backspace.release())
                continue;
            if ((key.key != 0x00) && (key.state == BBQ10Keyboard::StateRelease)) {
                InputEvent e = {};
                e.inputEvent = INPUT_BROKER_NONE;
                e.source = this->_originName;
                switch (key.key) {
                case 'p': // TAB
                case 't': // TAB as well
                    if (is_sym) {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = 0x09; // TAB Scancode
                        is_sym = false;  // reset sym state after second keypress
                    } else {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 'q': // ESC
                    if (is_sym) {
                        e.inputEvent = INPUT_BROKER_CANCEL;
                        e.kbchar = 0;
                        is_sym = false; // reset sym state after second keypress
                    } else {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 0x08: // Back
                    e.inputEvent = INPUT_BROKER_BACK;
                    e.kbchar = key.key;
                    break;
                case 'e': // sym e
                    if (is_sym) {
                        e.inputEvent = INPUT_BROKER_UP;
                        e.kbchar = INPUT_BROKER_UP;
                        is_sym = false; // reset sym state after second keypress
                    } else {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 'x': // sym x
                    if (is_sym) {
                        e.inputEvent = INPUT_BROKER_DOWN;
                        e.kbchar = 0;
                        is_sym = false; // reset sym state after second keypress
                    } else {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 's': // sym s
                    if (is_sym) {
                        e.inputEvent = INPUT_BROKER_LEFT;
                        e.kbchar = 0x00; // tweak for destSelect
                        is_sym = false;  // reset sym state after second keypress
                    } else {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 'f': // sym f
                    if (is_sym) {
                        e.inputEvent = INPUT_BROKER_RIGHT;
                        e.kbchar = 0x00; // tweak for destSelect
                        is_sym = false;  // reset sym state after second keypress
                    } else {
                        e.inputEvent = INPUT_BROKER_ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 0x13: // Code scanner says the SYM key is 0x13
                    is_sym = !is_sym;
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = is_sym ? INPUT_BROKER_MSG_FN_SYMBOL_ON   // send 0xf1 to tell CannedMessages to display that
                                      : INPUT_BROKER_MSG_FN_SYMBOL_OFF; // the modifier key is active
                    break;
                case 0x0a: // apparently Enter on Q10 is a line feed instead of carriage return
                    e.inputEvent = INPUT_BROKER_SELECT;
                    break;
                case 0x00: // nopress
                    e.inputEvent = INPUT_BROKER_NONE;
                    break;
                default: // all other keys
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = key.key;
                    is_sym = false; // reset sym state after second keypress
                    break;
                }

                if (e.inputEvent != INPUT_BROKER_NONE) {
                    this->notifyObservers(&e);
                    keyboardLightTyped();
                }
            }
        }
        break;
    }
    case 0x37: { // MPR121
        MPRkeyboard.trigger();
        InputEvent e = {};

        while (MPRkeyboard.hasEvent()) {
            char nextEvent = MPRkeyboard.dequeueEvent();
            e.inputEvent = INPUT_BROKER_ANYKEY;
            e.kbchar = 0x00;
            e.source = this->_originName;
            switch (nextEvent) {
            case 0x00: // MPR121_NONE
                e.inputEvent = INPUT_BROKER_NONE;
                e.kbchar = 0x00;
                break;
            case 0x90: // MPR121_REBOOT
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_REBOOT;
                break;
            case 0xb4: // MPR121_LEFT
                e.inputEvent = INPUT_BROKER_LEFT;
                e.kbchar = 0x00;
                break;
            case 0xb5: // MPR121_UP
                e.inputEvent = INPUT_BROKER_UP;
                e.kbchar = 0x00;
                break;
            case 0xb6: // MPR121_DOWN
                e.inputEvent = INPUT_BROKER_DOWN;
                e.kbchar = 0x00;
                break;
            case 0xb7: // MPR121_RIGHT
                e.inputEvent = INPUT_BROKER_RIGHT;
                e.kbchar = 0x00;
                break;
            case 0x1b: // MPR121_ESC
                e.inputEvent = INPUT_BROKER_CANCEL;
                e.kbchar = 0;
                break;
            case 0x08: // MPR121_BSP
                e.inputEvent = INPUT_BROKER_BACK;
                e.kbchar = 0x08;
                break;
            case 0x0d: // MPR121_SELECT
                e.inputEvent = INPUT_BROKER_SELECT;
                e.kbchar = 0x00;
                break;
            default:
                if (nextEvent > 127) {
                    e.inputEvent = INPUT_BROKER_NONE;
                    e.kbchar = 0x00;
                    break;
                }
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = nextEvent;
                break;
            }
            if (e.inputEvent != INPUT_BROKER_NONE) {
                LOG_INPUT("MP121 Notifying: %i Char: %i", e.inputEvent, e.kbchar);
                this->notifyObservers(&e);
            }
        }
        break;
    }
    case 0x84: { // Adafruit TCA8418
        TCAKeyboard->repeatHeldKey();
#if defined(T_DECK_PRO)
        // KB_INT high with nothing queued means no key; the fallback poll reads the FIFO anyway, in case an edge was missed
        const bool recheck = tcaRecheck;
        tcaRecheck = false;
        if (!recheck && ::digitalRead(KB_INT) == HIGH && !TCAKeyboard->hasEvent() &&
            !Throttle::hasElapsed(lastTcaReadMs, TCA_FALLBACK_POLL_MS))
            break;
        lastTcaReadMs = millis();
#endif
        TCAKeyboard->trigger();
        InputEvent e = {};
        while (TCAKeyboard->hasEvent()) {
            char nextEvent = TCAKeyboard->dequeueEvent();
            e.inputEvent = INPUT_BROKER_ANYKEY;
            e.kbchar = 0x00;
            e.source = this->_originName;
            switch (nextEvent) {
            case TCA8418KeyboardBase::NONE:
                e.inputEvent = INPUT_BROKER_NONE;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::REBOOT:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_REBOOT;
                break;
            case TCA8418KeyboardBase::LEFT:
                e.inputEvent = INPUT_BROKER_LEFT;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::UP:
                e.inputEvent = INPUT_BROKER_UP;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::DOWN:
                e.inputEvent = INPUT_BROKER_DOWN;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::RIGHT:
                e.inputEvent = INPUT_BROKER_RIGHT;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::BSP:
                e.inputEvent = INPUT_BROKER_BACK;
                e.kbchar = 0x08;
                break;
            case TCA8418KeyboardBase::DELETE_WORD:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_DELETE_WORD;
                break;
            case TCA8418KeyboardBase::SELECT:
                e.inputEvent = INPUT_BROKER_SELECT;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::ESC:
                e.inputEvent = INPUT_BROKER_CANCEL;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::GPS_TOGGLE:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_GPS_TOGGLE;
                break;
            case TCA8418KeyboardBase::SEND_PING:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_SEND_PING;
                break;
            case TCA8418KeyboardBase::MUTE_TOGGLE:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_MUTE_TOGGLE;
                break;
            case TCA8418KeyboardBase::BT_TOGGLE:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_BLUETOOTH_TOGGLE;
                break;
            case TCA8418KeyboardBase::BL_TOGGLE:
#if defined(T_DECK_PRO) && defined(MESHTASTIC_INCLUDE_INKHUD)
                e.inputEvent = INPUT_BROKER_ALT_PRESS;
#else
                keyboardLightShortcut();
                e.inputEvent = INPUT_BROKER_NONE;
#endif
                break;
            case TCA8418KeyboardBase::TAB:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_TAB;
                break;
            case TCA8418KeyboardBase::FUNCTION_F1:
                e.inputEvent = INPUT_BROKER_FN_F1;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::FUNCTION_F2:
                e.inputEvent = INPUT_BROKER_FN_F2;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::FUNCTION_F3:
                e.inputEvent = INPUT_BROKER_FN_F3;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::FUNCTION_F4:
                e.inputEvent = INPUT_BROKER_FN_F4;
                e.kbchar = 0x00;
                break;
            case TCA8418KeyboardBase::FUNCTION_F5:
                e.inputEvent = INPUT_BROKER_FN_F5;
                e.kbchar = 0x00;
                break;
            default:
                if (nextEvent > 127) {
                    e.inputEvent = INPUT_BROKER_NONE;
                    e.kbchar = 0x00;
                    break;
                }
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = nextEvent;
                break;
            }
            if (e.inputEvent != INPUT_BROKER_NONE) {
                // LOG_DEBUG("TCA8418 Notifying: %i Char: %c", e.inputEvent, e.kbchar);
                this->notifyObservers(&e);
                keyboardLightTyped();
            }
            TCAKeyboard->trigger();
        }
        // The chip keeps KB_INT low until its FIFO is empty, so this must follow the drain; a key that lands meanwhile
        // leaves the line low, and the 20 ms poll below picks it up
        TCAKeyboard->clearInt();
#if defined(T_DECK_PRO)
        // Per the datasheet, read INT_STAT again after clearing: events may have come in during the drain
        tcaRecheck = TCAKeyboard->keyEventPending();
#endif
        break;
    }
    case 0x02: {
        // RAK14004
        uint8_t rDataBuf[8] = {0};
        uint8_t PrintDataBuf = 0;
        if (read_from_14004(i2cBus, 0x01, rDataBuf, 0x04) == 1) {
            for (uint8_t aCount = 0; aCount < 0x04; aCount++) {
                for (uint8_t bCount = 0; bCount < 0x04; bCount++) {
                    if (((rDataBuf[aCount] >> bCount) & 0x01) == 0x01) {
                        PrintDataBuf = aCount * 0x04 + bCount + 1;
                    }
                }
            }
        }
        if (PrintDataBuf != 0) {
            LOG_INPUT("RAK14004 key 0x%x pressed", PrintDataBuf);
            InputEvent e = {};
            e.inputEvent = INPUT_BROKER_MATRIXKEY;
            e.source = this->_originName;
            e.kbchar = PrintDataBuf;
            this->notifyObservers(&e);
        }
        break;
    }
    case 0x00:   // CARDKB
    case 0x10: { // T-DECK

        i2cBus->requestFrom((int)cardkb_found.address, 1);

        if (i2cBus->available()) {
            char c = i2cBus->read();
            InputEvent e = {};
            e.inputEvent = INPUT_BROKER_NONE;
            e.source = this->_originName;
            switch (c) {
            case 0x71: // This is the button q. If modifier and q pressed, it cancels the input
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_CANCEL;
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x74: // letter t. if modifier and t pressed call 'tab'
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = 0x09; // TAB Scancode
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x6d: // letter m. Modifier makes it mute notifications
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = INPUT_BROKER_MSG_MUTE_TOGGLE; // mute notifications
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x6f: // letter o(+). Modifier makes screen increase in brightness
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = INPUT_BROKER_MSG_BRIGHTNESS_UP; // Increase Brightness code
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x69: // letter i(-).  Modifier makes screen decrease in brightness
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = INPUT_BROKER_MSG_BRIGHTNESS_DOWN; // Decrease Brightness code
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x20: // Space. Send network ping like double press does
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = INPUT_BROKER_SEND_PING; // (fn + space)
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x67: // letter g. toggle gps
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = INPUT_BROKER_GPS_TOGGLE;
                    e.kbchar = INPUT_BROKER_GPS_TOGGLE;
                } else {
                    e.inputEvent = INPUT_BROKER_ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x1b: // ESC
                e.inputEvent = INPUT_BROKER_CANCEL;
                break;
            case 0x08: // Back
                e.inputEvent = INPUT_BROKER_BACK;
                e.kbchar = 0;
                break;
            case 0xb5: // Up
                e.inputEvent = INPUT_BROKER_UP;
                e.kbchar = 0;
                break;
            case 0xb6: // Down
                e.inputEvent = INPUT_BROKER_DOWN;
                e.kbchar = 0;
                break;
            case 0xb4: // Left
                e.inputEvent = INPUT_BROKER_LEFT;
                e.kbchar = 0;
                break;
            case 0xb7: // Right
                e.inputEvent = INPUT_BROKER_RIGHT;
                e.kbchar = 0;
                break;
            case 0xc: // Modifier key: 0xc is alt+c (Other options could be: 0xea = shift+mic button or 0x4 shift+$(speaker))
#ifdef MESHTASTIC_INCLUDE_INKHUD
                      // InkHUD has no modifier indicator, and a hidden sym state would swallow the next q/t/m/o/i/g/space
                e.inputEvent = INPUT_BROKER_NONE;
                break;
#endif
                // toggle modifiers button.
                is_sym = !is_sym;
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = is_sym ? INPUT_BROKER_MSG_FN_SYMBOL_ON   // send 0xf1 to tell CannedMessages to display that the
                                  : INPUT_BROKER_MSG_FN_SYMBOL_OFF; // modifier key is active
                break;
            case 0x9e: // fn+g      INPUT_BROKER_GPS_TOGGLE
                e.inputEvent = INPUT_BROKER_GPS_TOGGLE;
                e.kbchar = c;
                break;
            case 0xaf: // fn+space  INPUT_BROKER_SEND_PING
                e.inputEvent = INPUT_BROKER_SEND_PING;
                e.kbchar = c;
                break;
            case 0x9b: // fn+s      INPUT_BROKER_MSG_SHUTDOWN
                e.inputEvent = INPUT_BROKER_SHUTDOWN;
                e.kbchar = c;
                break;

            case 0x90: // fn+r      INPUT_BROKER_MSG_REBOOT
            case 0x91: // fn+t
            case 0xac: // fn+m      INPUT_BROKER_MSG_MUTE_TOGGLE
            case 0xAA: // fn+b      INPUT_BROKER_MSG_BLUETOOTH_TOGGLE
            case 0x8F: // fn+e      INPUT_BROKER_MSG_EMOTE_LIST
                // just pass those unmodified
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = c;
                break;
            case 0x0d: // Enter
                e.inputEvent = INPUT_BROKER_SELECT;
                break;
            case 0x00: // nopress
                e.inputEvent = INPUT_BROKER_NONE;
                break;
            default:           // all other keys
                if (c > 127) { // bogus key value
                    e.inputEvent = INPUT_BROKER_NONE;
                    break;
                }
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = c;
                is_sym = false;
                break;
            }

            if (e.inputEvent != INPUT_BROKER_NONE) {
                this->notifyObservers(&e);
            }
        }
        break;
    }
#if defined(ELECROW_ThinkNode_M9)
    case 0x12: { // STC8H companion-MCU keypad (ThinkNode-M9)
        Stc8HKeyBoard.key_event = false;
        InputEvent e = {};
        e.inputEvent = INPUT_BROKER_NONE;
        e.source = this->_originName;
        uint8_t c = Stc8HKeyBoard.bsp_get_key_value(); // unsigned so the 0x8x/0xbx codes match
        switch (c) {
        case 0x81: // Mute
            e.inputEvent = INPUT_BROKER_ANYKEY;
            e.kbchar = INPUT_BROKER_MSG_MUTE_TOGGLE;
            break;
        case 0x82: // Home
            e.inputEvent = INPUT_BROKER_ANYKEY;
            graphics::NotificationRenderer::resetBanner();
            // TODO(M9): also reset CannedMessage/PresetMessage state once those modules are ported
            if (screen)
                screen->setFrames(graphics::Screen::FOCUS_FAULT);
            break;
        case 0x83: // Time
            e.inputEvent = INPUT_BROKER_ANYKEY;
            graphics::NotificationRenderer::resetBanner();
            // TODO(M9): also reset CannedMessage/PresetMessage state once those modules are ported
            if (screen)
                screen->setFrames(graphics::Screen::FOCUS_CLOCK);
            break;
        case 0x84:
            e.inputEvent = INPUT_BROKER_GPS_TOGGLE;
            Stc8HKeyBoard.switch_flashlight();
            break;
        case 0x85: // FM
            e.inputEvent = INPUT_BROKER_SEND_PING;
            e.kbchar = 0;
            break;
        case 0x86: // FM (long press)
            e.inputEvent = INPUT_BROKER_CANCEL;
            e.kbchar = 0;
            break;
        case 0x87: // Preset
            graphics::NotificationRenderer::resetBanner();
            // TODO(M9): also reset CannedMessage state once that module is ported
            e.inputEvent = INPUT_BROKER_SELECT_LONG;
            e.kbchar = 0;
            break;
        case 0xb5: // Up
            e.inputEvent = INPUT_BROKER_UP;
            e.kbchar = 0;
            break;
        case 0xb4: // Left
            e.inputEvent = INPUT_BROKER_LEFT;
            e.kbchar = 0;
            break;
        case 0xb6: // Down
            e.inputEvent = INPUT_BROKER_DOWN;
            e.kbchar = 0;
            break;
        case 0xb7: // Right
            e.inputEvent = INPUT_BROKER_RIGHT;
            e.kbchar = 0;
            break;
        case 0x20: // Space
            e.inputEvent = INPUT_BROKER_ANYKEY;
            e.kbchar = 0x20;
            break;
        case 0x0d: // Enter
            e.inputEvent = INPUT_BROKER_SELECT;
            e.kbchar = 0;
            break;
        case 0x08: // Del
            e.inputEvent = INPUT_BROKER_BACK;
            e.kbchar = 0;
            break;
        case 0x89: // Del (long press): the editor deletes a word, anything else still sees Back
            e.inputEvent = INPUT_BROKER_BACK;
            e.kbchar = INPUT_BROKER_MSG_DELETE_WORD;
            break;
        case 0x88: // Invalid key value
            e.inputEvent = INPUT_BROKER_ANYKEY;
            e.kbchar = 0;
            break;
        default: // all other keys (printable ASCII)
            if ((c >= 0x20) && (c <= 0x7F)) {
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = c;
            } else {
                e.inputEvent = INPUT_BROKER_NONE;
                e.kbchar = 0;
            }
            break;
        }
        if (e.inputEvent != INPUT_BROKER_NONE) {
            // LOG_DEBUG("STC8H companion-MCU keypad key event: 0x%02x", c);
            this->notifyObservers(&e);
            keyboardLightTyped();
        }

        break;
    }
#endif
    default:
        LOG_WARN("Unknown kb_model 0x%02x", kb_model);
    }
    // A running keyboard-light countdown ends on time, not at the next poll; a held key raises no interrupt, so its
    // repeats need a wake of their own
    const uint32_t dueMs = std::min({updateKeyboardLight(), TCAKeyboard->heldKeyDueMs(), q10Backspace.dueInMs()});
#if defined(T_DECK_PRO)
    // KB_INT wakes the thread; keep draining while the chip still holds keys, otherwise only the fallback poll
    if (tcaRecheck)
        return 0;
    // Set here rather than returned: an edge during runOnce() sets 0 from the ISR, and a returned value would overwrite it
    const uint32_t idle = std::min(TCA_FALLBACK_POLL_MS, dueMs);
    setIntervalFromNow(idle);
    if (::digitalRead(KB_INT) == LOW)
        setIntervalFromNow(std::min<uint32_t>(idle, 20));
    return RUN_SAME;
#elif defined(MESHTASTIC_INCLUDE_INKHUD)
    return std::min<uint32_t>(100, dueMs); // CardKB hands out one key per read, so 300 ms makes typing crawl
#else
    return std::min<uint32_t>(300, dueMs);
#endif
}

void KbI2cBase::notifyDeleteWord()
{
    InputEvent e = {};
    e.inputEvent = INPUT_BROKER_ANYKEY;
    e.kbchar = INPUT_BROKER_MSG_DELETE_WORD;
    e.source = this->_originName;
    this->notifyObservers(&e);
}

#if defined(T_DECK_PRO)
int KbI2cBase::beforeLightSleep(void *unused)
{
    detachInterrupt(digitalPinToInterrupt(KB_INT));
    return 0;
}

int KbI2cBase::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachInterrupt(digitalPinToInterrupt(KB_INT), tcaKeyboardInterrupt, FALLING);
    setIntervalFromNow(0); // a key that woke the board fell before the ISR was back
    return 0;
}
#endif

void KbI2cBase::toggleBacklight(bool on)
{
#if defined(T_DECK_PRO) && defined(MESHTASTIC_INCLUDE_INKHUD)
    TCAKeyboard->setBacklight(on);
#else
    screenOn = on;
    updateKeyboardLight();
#endif
}

bool KbI2cBase::hasKeyboardLight() const
{
#ifdef MESHTASTIC_INCLUDE_INKHUD
    return false; // InkHUD drives the T-Deck Pro's lights itself
#else
    if (!i2cBus)
        return false;
    switch (kb_model) {
    case 0x11: // BB Q10
        return true;
#if defined(T_DECK_PRO) || defined(T_LORA_PAGER)
    case 0x84: // the other TCA8418 keyboards have no light
        return true;
#endif
#if defined(ELECROW_ThinkNode_M9) && defined(KB_LED)
    case 0x12:
        return true;
#endif
    default:
        return false;
    }
#endif
}

void KbI2cBase::driveKeyboardLight(bool on)
{
    switch (kb_model) {
    case 0x11:
        Q10keyboard.setBacklight(on ? 1.0f : 0.0f);
        break;
    case 0x84:
        TCAKeyboard->setBacklight(on);
        break;
#if defined(ELECROW_ThinkNode_M9)
    case 0x12:
        Stc8HKeyBoard.set_keyboard_blight(on);
        break;
#endif
    default:
        break;
    }
}

// Only delivered keys count as typing: a modifier lighting it would make the Alt of Alt+B turn it straight off
void KbI2cBase::keyboardLightTyped()
{
    if (uiconfig.keyboard_backlight != meshtastic_DeviceUIConfig_KeyboardBacklightMode_WHILE_TYPING || !hasKeyboardLight())
        return;
    typingLit = true;
    lastTypingMs = millis();
    updateKeyboardLight();
}

// Alt+B: switch between On and Off and save it; in WHILE_TYPING, light it as a key press would, or turn it off early.
// In On, a light with brightness levels steps through them before turning off.
void KbI2cBase::keyboardLightShortcut()
{
    if (!hasKeyboardLight())
        return;
    switch (uiconfig.keyboard_backlight) {
    case meshtastic_DeviceUIConfig_KeyboardBacklightMode_WHILE_TYPING:
        typingLit = !keyboardLit;
        lastTypingMs = millis();
        break;
    case meshtastic_DeviceUIConfig_KeyboardBacklightMode_ON:
        if (keyboardLit && kb_model == 0x84 && TCAKeyboard->nextBacklightLevel()) {
            driveKeyboardLight(true);
            return;
        }
        uiconfig.keyboard_backlight = meshtastic_DeviceUIConfig_KeyboardBacklightMode_OFF;
        nodeDB->saveProto("/prefs/uiconfig.proto", meshtastic_DeviceUIConfig_size, &meshtastic_DeviceUIConfig_msg, &uiconfig);
        break;
    default:
        uiconfig.keyboard_backlight = meshtastic_DeviceUIConfig_KeyboardBacklightMode_ON;
        nodeDB->saveProto("/prefs/uiconfig.proto", meshtastic_DeviceUIConfig_size, &meshtastic_DeviceUIConfig_msg, &uiconfig);
        break;
    }
    updateKeyboardLight();
}

uint32_t KbI2cBase::updateKeyboardLight()
{
    if (!hasKeyboardLight())
        return UINT32_MAX;

    bool lit = false;
    uint32_t dueMs = UINT32_MAX;
    switch (uiconfig.keyboard_backlight) {
    case meshtastic_DeviceUIConfig_KeyboardBacklightMode_ON:
        lit = screenOn;
        break;
    case meshtastic_DeviceUIConfig_KeyboardBacklightMode_WHILE_TYPING:
        if (typingLit && Throttle::hasElapsed(lastTypingMs, KEYBOARD_LIGHT_TYPING_MS))
            typingLit = false;
        lit = screenOn && typingLit;
        if (typingLit)
            dueMs = Throttle::remainingMs(lastTypingMs, KEYBOARD_LIGHT_TYPING_MS);
        break;
    default:
        break;
    }
    if (lit != keyboardLit) {
        driveKeyboardLight(lit);
        keyboardLit = lit;
    }
    return dueMs;
}
