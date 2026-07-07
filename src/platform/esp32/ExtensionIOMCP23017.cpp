#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_MCP23017)

#include "ExtensionIOMCP23017.h"

ExtensionIOMCP23017 mcpIoExpander;

void ExtensionIOMCP23017::begin(TwoWire &wire, uint8_t addr, int sda, int scl)
{
    if (_begun)
        return;
    _wire = &wire;
    _addr = addr;
    wire.begin(sda, scl);
    _begun = true;
}

bool ExtensionIOMCP23017::readReg(uint8_t reg, uint8_t &val)
{
    if (!_wire || !_begun)
        return false;
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    if (_wire->endTransmission() != 0)
        return false;
    if (_wire->requestFrom((int)_addr, 1) != 1 || !_wire->available())
        return false;
    val = (uint8_t)_wire->read();
    return true;
}

uint8_t ExtensionIOMCP23017::readReg(uint8_t reg)
{
    uint8_t val = 0;
    readReg(reg, val);
    return val;
}

void ExtensionIOMCP23017::writeReg(uint8_t reg, uint8_t val)
{
    if (!_wire || !_begun)
        return;
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
}

void ExtensionIOMCP23017::updateDirectionBit(uint8_t pin, bool asOutput)
{
    uint8_t reg = iodirRegForPin(pin);
    uint8_t v;
    if (!readReg(reg, v)) // skip the write rather than clobber the other 7 pins on a failed read
        return;
    uint8_t mask = bitForPin(pin);
    if (asOutput)
        v &= ~mask; // output: bit 0
    else
        v |= mask; // input: bit 1
    writeReg(reg, v);
}

void ExtensionIOMCP23017::pinMode(uint8_t pin, uint8_t mode)
{
    if (pin > 15)
        return;
    if (mode == OUTPUT) {
        updateDirectionBit(pin, true);
    } else {
        updateDirectionBit(pin, false);
        uint8_t reg = gppuRegForPin(pin);
        uint8_t v;
        if (!readReg(reg, v)) // skip rather than clobber the bank on a failed read
            return;
        if (mode == INPUT_PULLUP)
            v |= bitForPin(pin);
        else
            v &= ~bitForPin(pin);
        writeReg(reg, v);
    }
}

void ExtensionIOMCP23017::digitalWrite(uint8_t pin, uint8_t value)
{
    if (pin > 15)
        return;
    uint8_t reg = olatRegForPin(pin);
    uint8_t v;
    if (!readReg(reg, v)) // skip rather than clobber the bank (e.g. drop LORA_NRST) on a failed read
        return;
    uint8_t mask = bitForPin(pin);
    if (value == LOW)
        v &= ~mask;
    else
        v |= mask;
    writeReg(reg, v);
}

int ExtensionIOMCP23017::digitalRead(uint8_t pin)
{
    if (pin > 15)
        return LOW;
    uint8_t v = readReg(gpioRegForPin(pin));
    return (v & bitForPin(pin)) ? HIGH : LOW;
}

void ExtensionIOMCP23017::enablePinChangeInterrupt(uint8_t pin, bool enable)
{
    if (pin > 15)
        return;
    uint8_t regG = gpintenRegForPin(pin);
    uint8_t v = readReg(regG);
    if (enable)
        v |= bitForPin(pin);
    else
        v &= ~bitForPin(pin);
    writeReg(regG, v);
    // INTCON: 0 = interrupt on pin change from previous value
    uint8_t regI = intconRegForPin(pin);
    v = readReg(regI);
    v &= ~bitForPin(pin);
    writeReg(regI, v);
}

void ExtensionIOMCP23017::clearInterruptLatches()
{
    if (!_begun)
        return;
    (void)readReg(0x10); // INTCAPA - clears INTA condition
    (void)readReg(0x11); // INTCAPB
}

uint8_t ExtensionIOMCP23017::readRegister(uint8_t reg)
{
    return readReg(reg);
}

void mcp23017EarlyInit()
{
    mcpIoExpander.begin(Wire, MCP23017_ADDR, I2C_SDA, I2C_SCL);

#ifdef EXIO_LCD_RST
    mcpIoExpander.pinMode(EXIO_LCD_RST, OUTPUT);
    mcpIoExpander.digitalWrite(EXIO_LCD_RST, LOW);
    delay(10);
    mcpIoExpander.digitalWrite(EXIO_LCD_RST, HIGH);
    delay(20);
#endif

#ifdef EXIO_LORA_NRST
    mcpIoExpander.pinMode(EXIO_LORA_NRST, OUTPUT);
    mcpIoExpander.digitalWrite(EXIO_LORA_NRST, HIGH);
#endif

#ifdef EXIO_LORA_BUSY
    mcpIoExpander.pinMode(EXIO_LORA_BUSY, INPUT);
#endif

#ifdef EXIO_LORA_DIO1
    mcpIoExpander.pinMode(EXIO_LORA_DIO1, INPUT);
#endif

#ifdef EXIO_GPS_WAKE
    mcpIoExpander.pinMode(EXIO_GPS_WAKE, OUTPUT);
    mcpIoExpander.digitalWrite(EXIO_GPS_WAKE, HIGH);
#endif

#ifdef EXIO_IMU_INT1
    mcpIoExpander.pinMode(EXIO_IMU_INT1, INPUT);
#endif

    LOG_INFO("MCP23017 0x%02x: IODIRA=0x%02x IODIRB=0x%02x GPIOA=0x%02x GPIOB=0x%02x", MCP23017_ADDR,
             mcpIoExpander.readRegister(0x00), mcpIoExpander.readRegister(0x01), mcpIoExpander.readRegister(0x12),
             mcpIoExpander.readRegister(0x13));
}

#endif // ARCH_ESP32 && USE_MCP23017
