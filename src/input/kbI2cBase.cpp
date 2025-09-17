#include "kbI2cBase.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"

#if defined(T_DECK_PRO)
#include "TDeckProKeyboard.h"
#elif defined(T_LORA_PAGER)
#include "TLoraPagerKeyboard.h"
#else
#include "TCA8418Keyboard.h"
#endif

extern ScanI2C::DeviceAddress cardkb_found;
extern uint8_t kb_model;

KbI2cBase::KbI2cBase(const char *name)
    : concurrency::OSThread(name),
#if defined(T_DECK_PRO)
      TCAKeyboard(*(new TDeckProKeyboard()))
#elif defined(T_LORA_PAGER)
      TCAKeyboard(*(new TLoraPagerKeyboard()))
#else
      TCAKeyboard(*(new TCA8418Keyboard()))
#endif
{
    this->_originName = name;
}

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
            i2cBus = &Wire1;
            if (cardkb_found.address == BBQ10_KB_ADDR) {
                Q10keyboard.begin(BBQ10_KB_ADDR, &Wire1);
                Q10keyboard.setBacklight(0);
            }
            if (cardkb_found.address == MPR121_KB_ADDR) {
                MPRkeyboard.begin(MPR121_KB_ADDR, &Wire1);
            }
            if (cardkb_found.address == TCA8418_KB_ADDR) {
                TCAKeyboard.begin(TCA8418_KB_ADDR, &Wire1);
            }
            break;
#endif
        case ScanI2C::WIRE:
            LOG_DEBUG("Use I2C Bus 0 (the first one)");
            i2cBus = &Wire;
            if (cardkb_found.address == BBQ10_KB_ADDR) {
                Q10keyboard.begin(BBQ10_KB_ADDR, &Wire);
                Q10keyboard.setBacklight(0);
            }
            if (cardkb_found.address == MPR121_KB_ADDR) {
                MPRkeyboard.begin(MPR121_KB_ADDR, &Wire);
            }
            if (cardkb_found.address == TCA8418_KB_ADDR) {
                TCAKeyboard.begin(TCA8418_KB_ADDR, &Wire);
            }
            break;
        case ScanI2C::NO_I2C:
        default:
            i2cBus = 0;
        }
    }

    switch (kb_model) {
    case 0x11: { // BB Q10
        int keyCount = Q10keyboard.keyCount();
        while (keyCount--) {
            const BBQ10Keyboard::KeyEvent key = Q10keyboard.keyEvent();
            if ((key.key != 0x00) && (key.state == BBQ10Keyboard::StateRelease)) {
                InputEvent e;
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
                }
            }
        }
        break;
    }
    case 0x37: { // MPR121
        MPRkeyboard.trigger();
        InputEvent e;

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
                LOG_DEBUG("MP121 Notifying: %i Char: %i", e.inputEvent, e.kbchar);
                this->notifyObservers(&e);
            }
        }
        break;
    }
    case 0x84: { // Adafruit TCA8418
        TCAKeyboard.trigger();
        InputEvent e;
        while (TCAKeyboard.hasEvent()) {
            char nextEvent = TCAKeyboard.dequeueEvent();
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
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_BLUETOOTH_TOGGLE;
                break;
            case TCA8418KeyboardBase::TAB:
                e.inputEvent = INPUT_BROKER_ANYKEY;
                e.kbchar = INPUT_BROKER_MSG_TAB;
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
                LOG_DEBUG("TCA8418 Notifying: %i Char: %c", e.inputEvent, e.kbchar);
                this->notifyObservers(&e);
            }
            TCAKeyboard.trigger();
        }
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
            LOG_DEBUG("RAK14004 key 0x%x pressed", PrintDataBuf);
            InputEvent e;
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
            InputEvent e;
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
                // toggle moddifiers button.
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

            case 0x8b: // fn+del    INPUT_BROKEN_MSG_DISMISS_FRAME
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
    default:
        LOG_WARN("Unknown kb_model 0x%02x", kb_model);
    }
    return 300;
}