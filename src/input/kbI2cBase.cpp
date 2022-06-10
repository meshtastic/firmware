#include "kbI2cBase.h"
#include "configuration.h"
#include <Wire.h>

KbI2cBase::KbI2cBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

int32_t KbI2cBase::runOnce()
{
    InputEvent e;
    e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_NONE;
    e.source = this->_originName;

    Wire.requestFrom(CARDKB_ADDR, 1);

    while (Wire.available()) {
        char c = Wire.read();
        switch (c) {
        case 0x1b: // ESC
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_CANCEL;
            break;
        case 0x08: // Back
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_BACK;
            break;
        case 0xb5: // Up
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_UP;
            break;
        case 0xb6: // Down
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_DOWN;
            break;
        case 0xb4: // Left
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_LEFT;
            break;
        case 0xb7: // Right
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_RIGHT;
            break;
        case 0x0d: // Enter
            e.inputEvent = ModuleConfig_CannedMessageConfig_InputEventChar_KEY_SELECT;
            break;
        }
    }

    if (e.inputEvent != ModuleConfig_CannedMessageConfig_InputEventChar_KEY_NONE) {
        this->notifyObservers(&e);
    }
    return 500;
}
