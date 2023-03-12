#include "kbI2cBase.h"

#include "configuration.h"
#include "detect/ScanI2C.h"

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

// Unused for now - flagging it off
#if 0
void write_to_14004(const TwoWire * i2cBus, uint8_t reg, uint8_t data)
{
    i2cBus->beginTransmission(CARDKB_ADDR);
    i2cBus->write(reg);
    i2cBus->write(data);
    i2cBus->endTransmission(); // stop transmitting
}
#endif

int32_t KbI2cBase::runOnce()
{
    if (cardkb_found.address != CARDKB_ADDR) {
        // Input device is not detected.
        return INT32_MAX;
    }

    if (!i2cBus) {
        switch (cardkb_found.port) {
        case ScanI2C::WIRE1:
#ifdef I2C_SDA1
            LOG_DEBUG("Using I2C Bus 1 (the second one)\n");
            i2cBus = &Wire1;
            break;
#endif
        case ScanI2C::WIRE:
            LOG_DEBUG("Using I2C Bus 0 (the first one)\n");
            i2cBus = &Wire;
            break;
        case ScanI2C::NO_I2C:
        default:
            i2cBus = 0;
        }
    }

    if (kb_model == 0x02) {
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
    } else {
        // m5 cardkb
        i2cBus->requestFrom(CARDKB_ADDR, 1);

        while (i2cBus->available()) {
            char c = i2cBus->read();
            InputEvent e;
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
            e.source = this->_originName;
            switch (c) {
            case 0x1b: // ESC
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                break;
            case 0x08: // Back
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                e.kbchar = c;
                break;
            case 0xb5: // Up
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
                break;
            case 0xb6: // Down
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
                break;
            case 0xb4: // Left
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
                e.kbchar = c;
                break;
            case 0xb7: // Right
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                e.kbchar = c;
                break;
            case 0x0d: // Enter
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
                break;
            case 0x00: // nopress
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                break;
            default: // all other keys
                e.inputEvent = ANYKEY;
                e.kbchar = c;
                break;
            }

            if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                this->notifyObservers(&e);
            }
        }
    }
    return 500;
}
