#include "kbI2cBase.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"

extern ScanI2C::DeviceAddress cardkb_found;
extern uint8_t kb_model;

KbI2cBase::KbI2cBase(const char *name) : concurrency::OSThread(name)
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
#ifdef I2C_SDA1
            LOG_DEBUG("Using I2C Bus 1 (the second one)\n");
            i2cBus = &Wire1;
            if (cardkb_found.address == BBQ10_KB_ADDR) {
                Q10keyboard.begin(BBQ10_KB_ADDR, &Wire1);
                Q10keyboard.setBacklight(0);
            }
            break;
#endif
        case ScanI2C::WIRE:
            LOG_DEBUG("Using I2C Bus 0 (the first one)\n");
            i2cBus = &Wire;
            if (cardkb_found.address == BBQ10_KB_ADDR) {
                Q10keyboard.begin(BBQ10_KB_ADDR, &Wire);
                Q10keyboard.setBacklight(0);
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
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                e.source = this->_originName;
                switch (key.key) {
                case 'p': // TAB
                case 't': // TAB as well
                    if (is_sym) {
                        e.inputEvent = ANYKEY;
                        e.kbchar = 0x09; // TAB Scancode
                        is_sym = false;  // reset sym state after second keypress
                    } else {
                        e.inputEvent = ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 'q': // ESC
                    if (is_sym) {
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                        e.kbchar = 0x1b;
                        is_sym = false; // reset sym state after second keypress
                    } else {
                        e.inputEvent = ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 0x08: // Back
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                    e.kbchar = key.key;
                    break;
                case 'e': // sym e
                    if (is_sym) {
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
                        e.kbchar = 0xb5;
                        is_sym = false; // reset sym state after second keypress
                    } else {
                        e.inputEvent = ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 'x': // sym x
                    if (is_sym) {
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
                        e.kbchar = 0xb6;
                        is_sym = false; // reset sym state after second keypress
                    } else {
                        e.inputEvent = ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 's': // sym s
                    if (is_sym) {
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
                        e.kbchar = 0x00; // tweak for destSelect
                        is_sym = false;  // reset sym state after second keypress
                    } else {
                        e.inputEvent = ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 'f': // sym f
                    if (is_sym) {
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                        e.kbchar = 0x00; // tweak for destSelect
                        is_sym = false;  // reset sym state after second keypress
                    } else {
                        e.inputEvent = ANYKEY;
                        e.kbchar = key.key;
                    }
                    break;
                case 0x13: // Code scanner says the SYM key is 0x13
                    is_sym = !is_sym;
                    e.inputEvent = ANYKEY;
                    e.kbchar =
                        is_sym ? 0xf1 : 0xf2; // send 0xf1 to tell CannedMessages to display that the modifier key is active
                    break;
                case 0x0a: // apparently Enter on Q10 is a line feed instead of carriage return
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
                    break;
                case 0x00: // nopress
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                    break;
                default: // all other keys
                    e.inputEvent = ANYKEY;
                    e.kbchar = key.key;
                    is_sym = false; // reset sym state after second keypress
                    break;
                }

                if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                    this->notifyObservers(&e);
                }
            }
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
            LOG_DEBUG("RAK14004 key 0x%x pressed\n", PrintDataBuf);
            InputEvent e;
            e.inputEvent = MATRIXKEY;
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
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
            e.source = this->_originName;
            switch (c) {
            case 0x71: // This is the button q. If modifier and q pressed, it cancels the input
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x74: // letter t. if modifier and t pressed call 'tab'
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0x09; // TAB Scancode
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x6d: // letter m. Modifier makes it mute notifications
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0xac; // mute notifications
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x6f: // letter o(+). Modifier makes screen increase in brightness
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0x11; // Increase Brightness code
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x69: // letter i(-).  Modifier makes screen decrease in brightness
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0x12; // Decrease Brightness code
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x20: // Space. Send network ping like double press does
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0xaf; // (fn + space)
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x67: // letter g. toggle gps
                if (is_sym) {
                    is_sym = false;
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0x9e;
                } else {
                    e.inputEvent = ANYKEY;
                    e.kbchar = c;
                }
                break;
            case 0x1b: // ESC
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                break;
            case 0x08: // Back
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                e.kbchar = c;
                break;
            case 0xb5: // Up
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
                e.kbchar = 0xb5;
                break;
            case 0xb6: // Down
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
                e.kbchar = 0xb6;
                break;
            case 0xb4: // Left
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
                e.kbchar = 0xb4;
                break;
            case 0xb7: // Right
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                e.kbchar = 0xb7;
                break;
            case 0xc: // Modifier key: 0xc is alt+c (Other options could be: 0xea = shift+mic button or 0x4 shift+$(speaker))
                // toggle moddifiers button.
                is_sym = !is_sym;
                e.inputEvent = ANYKEY;
                e.kbchar = is_sym ? 0xf1 : 0xf2; // send 0xf1 to tell CannedMessages to display that the modifier key is active
                break;
            case 0x90: // fn+r
            case 0x91: // fn+t
            case 0x9b: // fn+s
            case 0xac: // fn+m
            case 0x9e: // fn+g
            case 0xaf: // fn+space
                // just pass those unmodified
                e.inputEvent = ANYKEY;
                e.kbchar = c;
                break;
            case 0x0d: // Enter
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
                break;
            case 0x00: // nopress
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                break;
            default:           // all other keys
                if (c > 127) { // bogus key value
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                    break;
                }
                e.inputEvent = ANYKEY;
                e.kbchar = c;
                is_sym = false;
                break;
            }

            if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                this->notifyObservers(&e);
            }
        }
        break;
    }
    default:
        LOG_WARN("Unknown kb_model 0x%02x\n", kb_model);
    }
    return 300;
}