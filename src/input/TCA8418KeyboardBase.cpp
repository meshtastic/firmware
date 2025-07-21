// Based on the MPR121 Keyboard and Adafruit TCA8418 library

#include "TCA8418KeyboardBase.h"
#include "configuration.h"

#include <Arduino.h>

// FIELDS CONFIG REGISTER  1
#define _TCA8418_REG_CFG_AI 0x80           // Auto-increment for read/write
#define _TCA8418_REG_CFG_GPI_E_CGF 0x40    // Event mode config
#define _TCA8418_REG_CFG_OVR_FLOW_M 0x20   // Overflow mode enable
#define _TCA8418_REG_CFG_INT_CFG 0x10      // Interrupt config
#define _TCA8418_REG_CFG_OVR_FLOW_IEN 0x08 // Overflow interrupt enable
#define _TCA8418_REG_CFG_K_LCK_IEN 0x04    // Keypad lock interrupt enable
#define _TCA8418_REG_CFG_GPI_IEN 0x02      // GPI interrupt enable
#define _TCA8418_REG_CFG_KE_IEN 0x01       // Key events interrupt enable

// FIELDS INT_STAT REGISTER  2
#define _TCA8418_REG_STAT_CAD_INT 0x10      // Ctrl-alt-del seq status
#define _TCA8418_REG_STAT_OVR_FLOW_INT 0x08 // Overflow interrupt status
#define _TCA8418_REG_STAT_K_LCK_INT 0x04    // Key lock interrupt status
#define _TCA8418_REG_STAT_GPI_INT 0x02      // GPI interrupt status
#define _TCA8418_REG_STAT_K_INT 0x01        // Key events interrupt status

// FIELDS  KEY_LCK_EC REGISTER 3
#define _TCA8418_REG_LCK_EC_K_LCK_EN 0x40 // Key lock enable
#define _TCA8418_REG_LCK_EC_LCK_2 0x20    // Keypad lock status 2
#define _TCA8418_REG_LCK_EC_LCK_1 0x10    // Keypad lock status 1
#define _TCA8418_REG_LCK_EC_KLEC_3 0x08   // Key event count bit 3
#define _TCA8418_REG_LCK_EC_KLEC_2 0x04   // Key event count bit 2
#define _TCA8418_REG_LCK_EC_KLEC_1 0x02   // Key event count bit 1
#define _TCA8418_REG_LCK_EC_KLEC_0 0x01   // Key event count bit 0

TCA8418KeyboardBase::TCA8418KeyboardBase(uint8_t rows, uint8_t columns)
    : rows(rows), columns(columns), state(Init), queue(""), m_wire(nullptr), m_addr(0), readCallback(nullptr),
      writeCallback(nullptr)
{
}

void TCA8418KeyboardBase::begin(uint8_t addr, TwoWire *wire)
{
    m_addr = addr;
    m_wire = wire;
    m_wire->begin();
    reset();
}

void TCA8418KeyboardBase::begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr)
{
    m_addr = addr;
    m_wire = nullptr;
    writeCallback = w;
    readCallback = r;
    reset();
}

void TCA8418KeyboardBase::reset()
{
    LOG_DEBUG("TCA8418 Reset");
    //  GPIO
    //  set default all GIO pins to INPUT
    writeRegister(TCA8418_REG_GPIO_DIR_1, 0x00);
    writeRegister(TCA8418_REG_GPIO_DIR_2, 0x00);
    writeRegister(TCA8418_REG_GPIO_DIR_3, 0x00);

    //  add all pins to key events
    writeRegister(TCA8418_REG_GPI_EM_1, 0xFF);
    writeRegister(TCA8418_REG_GPI_EM_2, 0xFF);
    writeRegister(TCA8418_REG_GPI_EM_3, 0xFF);

    //  set all pins to FALLING interrupts
    writeRegister(TCA8418_REG_GPIO_INT_LVL_1, 0x00);
    writeRegister(TCA8418_REG_GPIO_INT_LVL_2, 0x00);
    writeRegister(TCA8418_REG_GPIO_INT_LVL_3, 0x00);

    //  add all pins to interrupts
    writeRegister(TCA8418_REG_GPIO_INT_EN_1, 0xFF);
    writeRegister(TCA8418_REG_GPIO_INT_EN_2, 0xFF);
    writeRegister(TCA8418_REG_GPIO_INT_EN_3, 0xFF);

    // Set keyboard matrix size
    matrix(rows, columns);
    enableDebounce();
    flush();
    state = Idle;
}

bool TCA8418KeyboardBase::matrix(uint8_t rows, uint8_t columns)
{
    if (rows < 1 || rows > 8 || columns < 1 || columns > 10)
        return false;

    // Setup the keypad matrix.
    uint8_t mask = 0x00;
    for (int r = 0; r < rows; r++) {
        mask <<= 1;
        mask |= 1;
    }
    writeRegister(TCA8418_REG_KP_GPIO_1, mask);

    mask = 0x00;
    for (int c = 0; c < columns && c < 8; c++) {
        mask <<= 1;
        mask |= 1;
    }
    writeRegister(TCA8418_REG_KP_GPIO_2, mask);

    if (columns > 8) {
        if (columns == 9)
            mask = 0x01;
        else
            mask = 0x03;
        writeRegister(TCA8418_REG_KP_GPIO_3, mask);
    }

    return true;
}

uint8_t TCA8418KeyboardBase::keyCount() const
{
    uint8_t eventCount = readRegister(TCA8418_REG_KEY_LCK_EC);
    eventCount &= 0x0F; //  lower 4 bits only
    return eventCount;
}

bool TCA8418KeyboardBase::hasEvent() const
{
    return queue.length() > 0;
}

void TCA8418KeyboardBase::queueEvent(char next)
{
    if (next == NONE) {
        return;
    }
    queue.concat(next);
}

char TCA8418KeyboardBase::dequeueEvent()
{
    if (queue.length() < 1) {
        return NONE;
    }
    char next = queue.charAt(0);
    queue.remove(0, 1);
    return next;
}

void TCA8418KeyboardBase::trigger()
{
    if (keyCount() == 0) {
        return;
    }
    if (state != Init) {
        // Read the key register
        uint8_t k = readRegister(TCA8418_REG_KEY_EVENT_A);
        uint8_t key = k & 0x7F;
        if (k & 0x80) {
            if (state == Idle)
                pressed(key);
            return;
        } else {
            if (state == Held) {
                released();
            }
            state = Idle;
            return;
        }
    } else {
        reset();
    }
}

void TCA8418KeyboardBase::pressed(uint8_t key)
{
    // must be defined in derived class
    LOG_ERROR("pressed() not implemented in derived class");
}

void TCA8418KeyboardBase::released()
{
    // must be defined in derived class
    LOG_ERROR("released() not implemented in derived class");
}

uint8_t TCA8418KeyboardBase::flush()
{
    // Flush key events
    uint8_t count = 0;
    while (readRegister(TCA8418_REG_KEY_EVENT_A) != 0)
        count++;

    // Flush gpio events
    readRegister(TCA8418_REG_GPIO_INT_STAT_1);
    readRegister(TCA8418_REG_GPIO_INT_STAT_2);
    readRegister(TCA8418_REG_GPIO_INT_STAT_3);

    // Clear INT_STAT register
    writeRegister(TCA8418_REG_INT_STAT, 3);
    return count;
}

uint8_t TCA8418KeyboardBase::digitalRead(uint8_t pinnum) const
{
    if (pinnum > TCA8418_COL9)
        return 0xFF;

    uint8_t reg = TCA8418_REG_GPIO_DAT_STAT_1 + pinnum / 8;
    uint8_t mask = (1 << (pinnum % 8));

    // Level  0 = low  other = high
    uint8_t value = readRegister(reg);
    if (value & mask)
        return HIGH;
    return LOW;
}

bool TCA8418KeyboardBase::digitalWrite(uint8_t pinnum, uint8_t level)
{
    if (pinnum > TCA8418_COL9)
        return false;

    uint8_t reg = TCA8418_REG_GPIO_DAT_OUT_1 + pinnum / 8;
    uint8_t mask = (1 << (pinnum % 8));

    // Level  0 = low  other = high
    uint8_t value = readRegister(reg);
    if (level == LOW)
        value &= ~mask;
    else
        value |= mask;
    writeRegister(reg, value);
    return true;
}

bool TCA8418KeyboardBase::pinMode(uint8_t pinnum, uint8_t mode)
{
    if (pinnum > TCA8418_COL9)
        return false;

    uint8_t idx = pinnum / 8;
    uint8_t reg = TCA8418_REG_GPIO_DIR_1 + idx;
    uint8_t mask = (1 << (pinnum % 8));

    // Mode  0 = input   1 = output
    uint8_t value = readRegister(reg);
    if (mode == OUTPUT)
        value |= mask;
    else
        value &= ~mask;
    writeRegister(reg, value);

    // Pullup  0 = enabled   1 = disabled
    reg = TCA8418_REG_GPIO_PULL_1 + idx;
    value = readRegister(reg);
    if (mode == INPUT_PULLUP)
        value &= ~mask;
    else
        value |= mask;
    writeRegister(reg, value);

    return true;
}

bool TCA8418KeyboardBase::pinIRQMode(uint8_t pinnum, uint8_t mode)
{
    if (pinnum > TCA8418_COL9)
        return false;
    if ((mode != RISING) && (mode != FALLING))
        return false;

    //  Mode  0 = falling   1 = rising
    uint8_t idx = pinnum / 8;
    uint8_t reg = TCA8418_REG_GPIO_INT_LVL_1 + idx;
    uint8_t mask = (1 << (pinnum % 8));

    uint8_t value = readRegister(reg);
    if (mode == RISING)
        value |= mask;
    else
        value &= ~mask;
    writeRegister(reg, value);

    // Enable interrupt
    reg = TCA8418_REG_GPIO_INT_EN_1 + idx;
    value = readRegister(reg);
    value |= mask;
    writeRegister(reg, value);

    return true;
}

void TCA8418KeyboardBase::enableInterrupts()
{
    uint8_t value = readRegister(TCA8418_REG_CFG);
    value |= (_TCA8418_REG_CFG_GPI_IEN | _TCA8418_REG_CFG_KE_IEN);
    writeRegister(TCA8418_REG_CFG, value);
};

void TCA8418KeyboardBase::disableInterrupts()
{
    uint8_t value = readRegister(TCA8418_REG_CFG);
    value &= ~(_TCA8418_REG_CFG_GPI_IEN | _TCA8418_REG_CFG_KE_IEN);
    writeRegister(TCA8418_REG_CFG, value);
};

void TCA8418KeyboardBase::enableMatrixOverflow()
{
    uint8_t value = readRegister(TCA8418_REG_CFG);
    value |= _TCA8418_REG_CFG_OVR_FLOW_M;
    writeRegister(TCA8418_REG_CFG, value);
};

void TCA8418KeyboardBase::disableMatrixOverflow()
{
    uint8_t value = readRegister(TCA8418_REG_CFG);
    value &= ~_TCA8418_REG_CFG_OVR_FLOW_M;
    writeRegister(TCA8418_REG_CFG, value);
};

void TCA8418KeyboardBase::enableDebounce()
{
    writeRegister(TCA8418_REG_DEBOUNCE_DIS_1, 0x00);
    writeRegister(TCA8418_REG_DEBOUNCE_DIS_2, 0x00);
    writeRegister(TCA8418_REG_DEBOUNCE_DIS_3, 0x00);
}

void TCA8418KeyboardBase::disableDebounce()
{
    writeRegister(TCA8418_REG_DEBOUNCE_DIS_1, 0xFF);
    writeRegister(TCA8418_REG_DEBOUNCE_DIS_2, 0xFF);
    writeRegister(TCA8418_REG_DEBOUNCE_DIS_3, 0xFF);
}

void TCA8418KeyboardBase::setBacklight(bool on) {}

uint8_t TCA8418KeyboardBase::readRegister(uint8_t reg) const
{
    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(reg);
        m_wire->endTransmission();

        m_wire->requestFrom(m_addr, (uint8_t)1);
        if (m_wire->available() < 1)
            return 0;

        return m_wire->read();
    }
    if (readCallback) {
        uint8_t data;
        readCallback(m_addr, reg, &data, 1);
        return data;
    }
    return 0;
}

void TCA8418KeyboardBase::writeRegister(uint8_t reg, uint8_t value)
{
    uint8_t data[2];
    data[0] = reg;
    data[1] = value;

    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(data, sizeof(uint8_t) * 2);
        m_wire->endTransmission();
    }
    if (writeCallback) {
        writeCallback(m_addr, data[0], &(data[1]), 1);
    }
}