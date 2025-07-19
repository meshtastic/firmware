#include "cardKbI2cImpl.h"
#include "InputBroker.h"
#include "detect/ScanI2CTwoWire.h"
#include "main.h"

CardKbI2cImpl *cardKbI2cImpl;

CardKbI2cImpl::CardKbI2cImpl() : KbI2cBase("cardKB") {}

void CardKbI2cImpl::init()
{
#if !MESHTASTIC_EXCLUDE_I2C && !defined(ARCH_PORTDUINO) && !defined(I2C_NO_RESCAN)
    if (cardkb_found.address == 0x00) {
        LOG_DEBUG("Rescan for I2C keyboard");
        uint8_t i2caddr_scan[] = {CARDKB_ADDR, TDECK_KB_ADDR, BBQ10_KB_ADDR, MPR121_KB_ADDR, XPOWERS_AXP192_AXP2101_ADDRESS};
        uint8_t i2caddr_asize = 5;
        auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());

#if WIRE_INTERFACES_COUNT == 2
        i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1, i2caddr_scan, i2caddr_asize);
#endif
        i2cScanner->scanPort(ScanI2C::I2CPort::WIRE, i2caddr_scan, i2caddr_asize);
        auto kb_info = i2cScanner->firstKeyboard();

        if (kb_info.type != ScanI2C::DeviceType::NONE) {
            cardkb_found = kb_info.address;
            switch (kb_info.type) {
            case ScanI2C::DeviceType::RAK14004:
                kb_model = 0x02;
                break;
            case ScanI2C::DeviceType::CARDKB:
                kb_model = 0x00;
                break;
            case ScanI2C::DeviceType::TDECKKB:
                // assign an arbitrary value to distinguish from other models
                kb_model = 0x10;
                break;
            case ScanI2C::DeviceType::BBQ10KB:
                // assign an arbitrary value to distinguish from other models
                kb_model = 0x11;
                break;
            case ScanI2C::DeviceType::MPR121KB:
                // assign an arbitrary value to distinguish from other models
                kb_model = 0x37;
                break;
            case ScanI2C::DeviceType::TCA8418KB:
                // assign an arbitrary value to distinguish from other models
                kb_model = 0x84;
                break;
            default:
                // use this as default since it's also just zero
                LOG_WARN("kb_info.type is unknown(0x%02x), setting kb_model=0x00", kb_info.type);
                kb_model = 0x00;
            }
        }
        if (cardkb_found.address == 0x00) {
            disable();
            return;
        } else {
            LOG_DEBUG("Keyboard Type: 0x%02x Model: 0x%02x Address: 0x%02x", kb_info.type, kb_model, cardkb_found.address);
        }
    }
#else
    if (cardkb_found.address == 0x00) {
        disable();
        return;
    }
#endif
    inputBroker->registerSource(this);
    kb_found = true;
}