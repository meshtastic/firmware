// Based on the BBQ10 Keyboard and Adafruit TCA8418 library

#include "TCA8418Keyboard.h"
#include "configuration.h"

#include <Arduino.h>

// REGISTERS
// #define _TCA8418_REG_RESERVED 0x00
#define _TCA8418_REG_CFG 0x01             // Configuration register
#define _TCA8418_REG_INT_STAT 0x02        // Interrupt status
#define _TCA8418_REG_KEY_LCK_EC 0x03      // Key lock and event counter
#define _TCA8418_REG_KEY_EVENT_A 0x04     // Key event register A
#define _TCA8418_REG_KEY_EVENT_B 0x05     // Key event register B
#define _TCA8418_REG_KEY_EVENT_C 0x06     // Key event register C
#define _TCA8418_REG_KEY_EVENT_D 0x07     // Key event register D
#define _TCA8418_REG_KEY_EVENT_E 0x08     // Key event register E
#define _TCA8418_REG_KEY_EVENT_F 0x09     // Key event register F
#define _TCA8418_REG_KEY_EVENT_G 0x0A     // Key event register G
#define _TCA8418_REG_KEY_EVENT_H 0x0B     // Key event register H
#define _TCA8418_REG_KEY_EVENT_I 0x0C     // Key event register I
#define _TCA8418_REG_KEY_EVENT_J 0x0D     // Key event register J
#define _TCA8418_REG_KP_LCK_TIMER 0x0E    // Keypad lock1 to lock2 timer
#define _TCA8418_REG_UNLOCK_1 0x0F        // Unlock register 1
#define _TCA8418_REG_UNLOCK_2 0x10        // Unlock register 2
#define _TCA8418_REG_GPIO_INT_STAT_1 0x11 // GPIO interrupt status 1
#define _TCA8418_REG_GPIO_INT_STAT_2 0x12 // GPIO interrupt status 2
#define _TCA8418_REG_GPIO_INT_STAT_3 0x13 // GPIO interrupt status 3
#define _TCA8418_REG_GPIO_DAT_STAT_1 0x14 // GPIO data status 1
#define _TCA8418_REG_GPIO_DAT_STAT_2 0x15 // GPIO data status 2
#define _TCA8418_REG_GPIO_DAT_STAT_3 0x16 // GPIO data status 3
#define _TCA8418_REG_GPIO_DAT_OUT_1 0x17  // GPIO data out 1
#define _TCA8418_REG_GPIO_DAT_OUT_2 0x18  // GPIO data out 2
#define _TCA8418_REG_GPIO_DAT_OUT_3 0x19  // GPIO data out 3
#define _TCA8418_REG_GPIO_INT_EN_1 0x1A   // GPIO interrupt enable 1
#define _TCA8418_REG_GPIO_INT_EN_2 0x1B   // GPIO interrupt enable 2
#define _TCA8418_REG_GPIO_INT_EN_3 0x1C   // GPIO interrupt enable 3
#define _TCA8418_REG_KP_GPIO_1 0x1D       // Keypad/GPIO select 1
#define _TCA8418_REG_KP_GPIO_2 0x1E       // Keypad/GPIO select 2
#define _TCA8418_REG_KP_GPIO_3 0x1F       // Keypad/GPIO select 3
#define _TCA8418_REG_GPI_EM_1 0x20        // GPI event mode 1
#define _TCA8418_REG_GPI_EM_2 0x21        // GPI event mode 2
#define _TCA8418_REG_GPI_EM_3 0x22        // GPI event mode 3
#define _TCA8418_REG_GPIO_DIR_1 0x23      // GPIO data direction 1
#define _TCA8418_REG_GPIO_DIR_2 0x24      // GPIO data direction 2
#define _TCA8418_REG_GPIO_DIR_3 0x25      // GPIO data direction 3
#define _TCA8418_REG_GPIO_INT_LVL_1 0x26  // GPIO edge/level detect 1
#define _TCA8418_REG_GPIO_INT_LVL_2 0x27  // GPIO edge/level detect 2
#define _TCA8418_REG_GPIO_INT_LVL_3 0x28  // GPIO edge/level detect 3
#define _TCA8418_REG_DEBOUNCE_DIS_1 0x29  // Debounce disable 1
#define _TCA8418_REG_DEBOUNCE_DIS_2 0x2A  // Debounce disable 2
#define _TCA8418_REG_DEBOUNCE_DIS_3 0x2B  // Debounce disable 3
#define _TCA8418_REG_GPIO_PULL_1 0x2C     // GPIO pull-up disable 1
#define _TCA8418_REG_GPIO_PULL_2 0x2D     // GPIO pull-up disable 2
#define _TCA8418_REG_GPIO_PULL_3 0x2E     // GPIO pull-up disable 3
// #define _TCA8418_REG_RESERVED 0x2F

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

// Pin IDs for matrix rows/columns
enum {
    _TCA8418_ROW0, // Pin ID for row 0
    _TCA8418_ROW1, // Pin ID for row 1
    _TCA8418_ROW2, // Pin ID for row 2
    _TCA8418_ROW3, // Pin ID for row 3
    _TCA8418_ROW4, // Pin ID for row 4
    _TCA8418_ROW5, // Pin ID for row 5
    _TCA8418_ROW6, // Pin ID for row 6
    _TCA8418_ROW7, // Pin ID for row 7
    _TCA8418_COL0, // Pin ID for column 0
    _TCA8418_COL1, // Pin ID for column 1
    _TCA8418_COL2, // Pin ID for column 2
    _TCA8418_COL3, // Pin ID for column 3
    _TCA8418_COL4, // Pin ID for column 4
    _TCA8418_COL5, // Pin ID for column 5
    _TCA8418_COL6, // Pin ID for column 6
    _TCA8418_COL7, // Pin ID for column 7
    _TCA8418_COL8, // Pin ID for column 8
    _TCA8418_COL9  // Pin ID for column 9
};

#define _TCA8418_ROWS 8
#define _TCA8418_COLS 10

TCA8418Keyboard::TCA8418Keyboard() : m_wire(nullptr), m_addr(0), readCallback(nullptr), writeCallback(nullptr) {}

void TCA8418Keyboard::begin(uint8_t addr, TwoWire *wire)
{
    m_addr = addr;
    m_wire = wire;

    m_wire->begin();

    reset();
}

void TCA8418Keyboard::begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr)
{
    m_addr = addr;
    m_wire = nullptr;
    writeCallback = w;
    readCallback = r;
    reset();
}

void TCA8418Keyboard::reset()
{
    LOG_DEBUG("TCA8418 Reset");
    //  GPIO
    //  set default all GIO pins to INPUT
    writeRegister(_TCA8418_REG_GPIO_DIR_1, 0x00);
    writeRegister(_TCA8418_REG_GPIO_DIR_2, 0x00);
    writeRegister(_TCA8418_REG_GPIO_DIR_3, 0x00);

    //  add all pins to key events
    writeRegister(_TCA8418_REG_GPI_EM_1, 0xFF);
    writeRegister(_TCA8418_REG_GPI_EM_2, 0xFF);
    writeRegister(_TCA8418_REG_GPI_EM_3, 0xFF);

    //  set all pins to FALLING interrupts
    writeRegister(_TCA8418_REG_GPIO_INT_LVL_1, 0x00);
    writeRegister(_TCA8418_REG_GPIO_INT_LVL_2, 0x00);
    writeRegister(_TCA8418_REG_GPIO_INT_LVL_3, 0x00);

    //  add all pins to interrupts
    writeRegister(_TCA8418_REG_GPIO_INT_EN_1, 0xFF);
    writeRegister(_TCA8418_REG_GPIO_INT_EN_2, 0xFF);
    writeRegister(_TCA8418_REG_GPIO_INT_EN_3, 0xFF);

    matrix(_TCA8418_ROWS, _TCA8418_COLS);
    enableDebounce();
    flush();
}

bool TCA8418Keyboard::matrix(uint8_t rows, uint8_t columns)
{
    if ((rows > 8) || (columns > 10))
        return false;

    //  Skip zero size matrix
    if ((rows != 0) && (columns != 0)) {
        // Setup the keypad matrix.
        uint8_t mask = 0x00;
        for (int r = 0; r < rows; r++) {
            mask <<= 1;
            mask |= 1;
        }
        writeRegister(_TCA8418_REG_KP_GPIO_1, mask);

        mask = 0x00;
        for (int c = 0; c < columns && c < 8; c++) {
            mask <<= 1;
            mask |= 1;
        }
        writeRegister(_TCA8418_REG_KP_GPIO_2, mask);

        if (columns > 8) {
            if (columns == 9)
                mask = 0x01;
            else
                mask = 0x03;
            writeRegister(_TCA8418_REG_KP_GPIO_3, mask);
        }
    }

    return true;
}

uint8_t TCA8418Keyboard::keyCount() const
{
    uint8_t eventCount = readRegister(_TCA8418_REG_KEY_LCK_EC);
    eventCount &= 0x0F; //  lower 4 bits only
    return eventCount;
}

TCA8418Keyboard::KeyEvent TCA8418Keyboard::keyEvent() const
{
    KeyEvent event = {.key = '\0', .state = Idle};
    if (keyCount() == 0)
        return event;

    uint8_t k = readRegister(_TCA8418_REG_KEY_EVENT_A);
    event.key = k & 0x7F;
    if (k & 0x80) {
        event.state = TCA8418Keyboard::Press;
    } else {
        event.state = TCA8418Keyboard::Release;
    }
    return event;
}

uint8_t TCA8418Keyboard::flush()
{
    //  Flush key events
    uint8_t count = 0;
    while (readRegister(_TCA8418_REG_KEY_EVENT_A) != 0)
        count++;
    //  Flush gpio events
    readRegister(_TCA8418_REG_GPIO_INT_STAT_1);
    readRegister(_TCA8418_REG_GPIO_INT_STAT_2);
    readRegister(_TCA8418_REG_GPIO_INT_STAT_3);
    //  Clear INT_STAT register
    writeRegister(_TCA8418_REG_INT_STAT, 3);
    return count;
}

uint8_t TCA8418Keyboard::digitalRead(uint8_t pinnum) const
{
    if (pinnum > _TCA8418_COL9)
        return 0xFF;

    uint8_t reg = _TCA8418_REG_GPIO_DAT_STAT_1 + pinnum / 8;
    uint8_t mask = (1 << (pinnum % 8));

    // Level  0 = low  other = high
    uint8_t value = readRegister(reg);
    if (value & mask)
        return HIGH;
    return LOW;
}

bool TCA8418Keyboard::digitalWrite(uint8_t pinnum, uint8_t level)
{
    if (pinnum > _TCA8418_COL9)
        return false;

    uint8_t reg = _TCA8418_REG_GPIO_DAT_OUT_1 + pinnum / 8;
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

bool TCA8418Keyboard::pinMode(uint8_t pinnum, uint8_t mode)
{
    if (pinnum > _TCA8418_COL9)
        return false;
    // if (mode > INPUT_PULLUP) return false; ?s

    uint8_t idx = pinnum / 8;
    uint8_t reg = _TCA8418_REG_GPIO_DIR_1 + idx;
    uint8_t mask = (1 << (pinnum % 8));

    // Mode  0 = input   1 = output
    uint8_t value = readRegister(reg);
    if (mode == OUTPUT)
        value |= mask;
    else
        value &= ~mask;
    writeRegister(reg, value);

    // Pullup  0 = enabled   1 = disabled
    reg = _TCA8418_REG_GPIO_PULL_1 + idx;
    value = readRegister(reg);
    if (mode == INPUT_PULLUP)
        value &= ~mask;
    else
        value |= mask;
    writeRegister(reg, value);

    return true;
}

bool TCA8418Keyboard::pinIRQMode(uint8_t pinnum, uint8_t mode)
{
    if (pinnum > _TCA8418_COL9)
        return false;
    if ((mode != RISING) && (mode != FALLING))
        return false;

    //  Mode  0 = falling   1 = rising
    uint8_t idx = pinnum / 8;
    uint8_t reg = _TCA8418_REG_GPIO_INT_LVL_1 + idx;
    uint8_t mask = (1 << (pinnum % 8));

    uint8_t value = readRegister(reg);
    if (mode == RISING)
        value |= mask;
    else
        value &= ~mask;
    writeRegister(reg, value);

    // Enable interrupt
    reg = _TCA8418_REG_GPIO_INT_EN_1 + idx;
    value = readRegister(reg);
    value |= mask;
    writeRegister(reg, value);

    return true;
}

void TCA8418Keyboard::enableInterrupts()
{
    uint8_t value = readRegister(_TCA8418_REG_CFG);
    value |= (_TCA8418_REG_CFG_GPI_IEN | _TCA8418_REG_CFG_KE_IEN);
    writeRegister(_TCA8418_REG_CFG, value);
};

void TCA8418Keyboard::disableInterrupts()
{
    uint8_t value = readRegister(_TCA8418_REG_CFG);
    value &= ~(_TCA8418_REG_CFG_GPI_IEN | _TCA8418_REG_CFG_KE_IEN);
    writeRegister(_TCA8418_REG_CFG, value);
};

void TCA8418Keyboard::enableMatrixOverflow()
{
    uint8_t value = readRegister(_TCA8418_REG_CFG);
    value |= _TCA8418_REG_CFG_OVR_FLOW_M;
    writeRegister(_TCA8418_REG_CFG, value);
};

void TCA8418Keyboard::disableMatrixOverflow()
{
    uint8_t value = readRegister(_TCA8418_REG_CFG);
    value &= ~_TCA8418_REG_CFG_OVR_FLOW_M;
    writeRegister(_TCA8418_REG_CFG, value);
};

void TCA8418Keyboard::enableDebounce()
{
    writeRegister(_TCA8418_REG_DEBOUNCE_DIS_1, 0x00);
    writeRegister(_TCA8418_REG_DEBOUNCE_DIS_2, 0x00);
    writeRegister(_TCA8418_REG_DEBOUNCE_DIS_3, 0x00);
}

void TCA8418Keyboard::disableDebounce()
{
    writeRegister(_TCA8418_REG_DEBOUNCE_DIS_1, 0xFF);
    writeRegister(_TCA8418_REG_DEBOUNCE_DIS_2, 0xFF);
    writeRegister(_TCA8418_REG_DEBOUNCE_DIS_3, 0xFF);
}

uint8_t TCA8418Keyboard::readRegister(uint8_t reg) const
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

void TCA8418Keyboard::writeRegister(uint8_t reg, uint8_t value)
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
