#include "STC8HKeyboard.h"

#if defined(ELECROW_ThinkNode_M9)
#include "cardKbI2cImpl.h"

#include "configuration.h"

// ---------------------------------------------------------------------------
// STC8H companion-MCU keypad driver (ThinkNode-M9).
//
// The original STC8HKeyboard.cpp was lost from the reference source tree, so
// this was recovered from the linked reference firmware.elf (the .o was an LTO
// object with no machine code; the final ELF had the real inlined bodies).
//
// How the hardware works:
//   - The STC8H raises KB_INT (rising edge, idle-low) when a key is pressed. The ISR
//     latches key_event; is_key_event() just returns that flag.
//   - The pressed key code is read over I2C from register 0x05.
//   - is_key_state() polls KB_INT directly to keep the backlight lit while a
//     key is held.
//   - Battery voltage lives in registers 0x01..0x04, little-endian.
//   - Sleep is requested by writing 0x01 to the STATE register (0x06).
//   - The keypad backlight (KB_LED) and torch (PIN_LED) are plain host GPIOs,
//     not I2C commands.
// ---------------------------------------------------------------------------

STC8HKeyboard Stc8HKeyBoard;

// ISR latched on each KB_INT rising edge (a key was pressed).
static void has_key_event()
{
    Stc8HKeyBoard.key_event = true;
    if (cardKbI2cImpl) {
        cardKbI2cImpl->setIntervalFromNow(0);
        // runASAP = true;
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
    }
}

void STC8HKeyboard::writeRegister(uint8_t reg, uint8_t val)
{
    _pWire->beginTransmission(_I2C_addr);
    _pWire->write(reg);
    _pWire->write(val);
    _pWire->endTransmission();
}

uint8_t STC8HKeyboard::readRegister(uint8_t reg)
{
    _pWire->beginTransmission(_I2C_addr);
    _pWire->write(reg);
    if (_pWire->endTransmission(false) != 0)
        return 0xFF;
    if (_pWire->requestFrom(_I2C_addr, (uint8_t)1) != 1)
        return 0xFF;
    return _pWire->read();
}

void STC8HKeyboard::begin(uint8_t addr, TwoWire *wire)
{
    LOG_DEBUG("STC8HKeyboard::begin() addr=0x%02x", addr);
    _I2C_addr = addr;
    _pWire = wire;
    pinMode(KB_INT, INPUT);
#ifdef KB_LED
    pinMode(KB_LED, OUTPUT);
#endif
#ifdef PIN_LED
    pinMode(PIN_LED, OUTPUT);
#endif
    attachInterrupt(KB_INT, has_key_event, RISING);
    _pWire->begin();
    Keyboard_state = true;
#ifdef ARCH_ESP32
    // Detach/reattach the key interrupt around ESP32 light sleep
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif
}

bool STC8HKeyboard::is_Keyboard_begin()
{
    return Keyboard_state;
}

// A key is currently active (KB_INT held); used to wake the keypad backlight.
bool STC8HKeyboard::is_key_state()
{
    return digitalRead(KB_INT);
}

// A key-press interrupt has been latched since the flag was last cleared.
bool STC8HKeyboard::is_key_event()
{
    return key_event;
}

uint8_t STC8HKeyboard::bsp_get_key_value()
{
    return readRegister(0x01);
}

// Battery millivolts: registers 0x01..0x04 read little-endian, low 16 bits.
uint16_t STC8HKeyboard::bsp_get_battery_voltage()
{
    if (!Keyboard_state)
        return 0;
    uint32_t voltage = 0;
    for (uint8_t i = 0; i < 4; i++)
        voltage |= (uint32_t)readRegister(STC8_REG_ADDR_BATTERY + i) << (i * 8);
    return voltage > 0xFFFF ? 0xFFFF : (uint16_t)voltage;
}

void STC8HKeyboard::set_keyboard_blight(bool state)
{
#ifdef KB_LED
    digitalWrite(KB_LED, state);
#else
    (void)state; // KB_LED pin not defined for this board
#endif
}

void STC8HKeyboard::switch_flashlight()
{
#ifdef PIN_LED
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
#endif
    // else: torch pin unresolved on this board (old board used PIN_LED 13,
    // which the current variant assigns to BATTERY_PIN) -- see variant.h.
}

void STC8HKeyboard::set_sleep_status(void)
{
    writeRegister(STC8_REG_ADDR_STATE, 0x01);
    _pWire->end();
}

#ifdef ARCH_ESP32
// Detach the key interrupt before ESP32 light sleep, so it can't fire while asleep.
int STC8HKeyboard::beforeLightSleep(void *unused)
{
    detachInterrupt(KB_INT);
    return 0; // Indicates success
}

// Reattach the key interrupt after waking from light sleep.
int STC8HKeyboard::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    attachInterrupt(KB_INT, has_key_event, RISING);
    return 0; // Indicates success
}
#endif

#endif // ELECROW_ThinkNode_M9
