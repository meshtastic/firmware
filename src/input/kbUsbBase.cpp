#include "kbUsbBase.h"
#include "configuration.h"

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2

KbUsbBase::KbUsbBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

int32_t KbUsbBase::runOnce()
{
    if (firstTime) {
        // This is the first time the OSThread library has called this function, so init the USB HID routines
        begin();
        firstTime = 0;
    } else {

        task();
    }
    return 100;
}

void KbUsbBase::onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier)
{

    if (ascii != 0) {
        LOG_DEBUG("Key 0x%x Code 0x%x Mod 0x%x pressed\n", ascii, keycode, modifier);
        // reset shift now that we have a keypress
        InputEvent e;
        e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
        e.source = this->_originName;
        switch (ascii) {
        case 0x1b: // ESC
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
            break;
        case 0x08: // Back
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
            e.kbchar = ascii;
            break;
        case 0xb5: // Up
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
            break;
        case 0xb6: // Down
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
            break;
        case 0xb4: // Left
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
            e.kbchar = ascii;
            break;
        case 0xb7: // Right
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
            e.kbchar = ascii;
            break;
        case 0x0d: // Enter
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
            break;
        case 0x00: // nopress
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
            break;
        default: // all other keys
            e.inputEvent = ANYKEY;
            e.kbchar = ascii;
            break;
        }
        if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
            this->notifyObservers(&e);
        }
    }
}

#endif