#include "peMatrixBase.h"

#include "configuration.h"
#include "detect/ScanI2C.h"

extern ScanI2C::DeviceAddress cardkb_found;
extern uint8_t kb_model;

I2CKeyPad keyPad(cardkb_found.address);

PeMatrixBase::PeMatrixBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

int32_t PeMatrixBase::runOnce()
{
    if (kb_model != 0x12) {
        // Input device is not detected.
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do port setup
        firstTime = 0;
        if (!keyPad.begin()) {
            LOG_ERROR("Failed to initialize I2C keypad\n");
            return disable();
        }
        keyPad.loadKeyMap(keymap);
    } else {
        if (keyPad.isPressed()) {
            key = keyPad.getChar();
            // debounce
            if (key != prevkey) {
                if (key != 0) {
                    LOG_DEBUG("Key 0x%x pressed\n", key);
                    // reset shift now that we have a keypress
                    InputEvent e;
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                    e.source = this->_originName;
                    switch (key) {
                    case 0x1b: // ESC
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                        break;
                    case 0x08: // Back
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                        e.kbchar = key;
                        break;
                    case 0xb5: // Up
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
                        break;
                    case 0xb6: // Down
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
                        break;
                    case 0xb4: // Left
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
                        e.kbchar = key;
                        break;
                    case 0xb7: // Right
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                        e.kbchar = key;
                        break;
                    case 0x0d: // Enter
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
                        break;
                    case 0x00: // nopress
                        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                        break;
                    default: // all other keys
                        e.inputEvent = ANYKEY;
                        e.kbchar = key;
                        break;
                    }
                    if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                        this->notifyObservers(&e);
                    }
                }
                prevkey = key;
            }
        }
    }
    return 100; // Keyscan every 100msec to avoid key bounce
}
