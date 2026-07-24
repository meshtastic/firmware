#include "MCP23017Keyboard.h"

// Registers
#define _MCP23017_IODIRA   0x00
#define _MCP23017_IODIRB   0x01
#define _MCP23017_IPOLA    0x02
#define _MCP23017_IPOLB    0x03
#define _MCP23017_GPINTENA 0x04
#define _MCP23017_GPINTENB 0x05
#define _MCP23017_DEFVALA  0x06
#define _MCP23017_DEFVALB  0x07
#define _MCP23017_INTCONA  0x08
#define _MCP23017_INTCONB  0x09
#define _MCP23017_IOCONA   0x0A
#define _MCP23017_IOCONB   0x0B
#define _MCP23017_GPPUA    0x0C
#define _MCP23017_GPPUB    0x0D
#define _MCP23017_INTFA    0x0E
#define _MCP23017_INTFB    0x0F
#define _MCP23017_INTCAPA  0x10
#define _MCP23017_INTCAPB  0x11
#define _MCP23017_GPIOA    0x12
#define _MCP23017_GPIOB    0x13

#define _KEY_MASK 0x7FFF
#define _NUM_KEYS 15

// Internal codes
#define KB_NONE   0x00
#define KB_LEFT   0xb4
#define KB_UP     0xb5
#define KB_DOWN   0xb6
#define KB_RIGHT  0xb7
#define KB_ESC    0x1b
#define KB_BSP    0x08
#define KB_SELECT 0x0d

// Default keymap 
#ifndef CUSTOM_MCP23017_MAP 

#define MCP23017_KEYMAP_0   0
#define MCP23017_KEYMAP_1   1
#define MCP23017_KEYMAP_2   2
#define MCP23017_KEYMAP_3   3
#define MCP23017_KEYMAP_4   4
#define MCP23017_KEYMAP_5   5
#define MCP23017_KEYMAP_6   6
#define MCP23017_KEYMAP_7   7
#define MCP23017_KEYMAP_8   8
#define MCP23017_KEYMAP_9   9
#define MCP23017_KEYMAP_10  10
#define MCP23017_KEYMAP_11  11
#define MCP23017_KEYMAP_12  12
#define MCP23017_KEYMAP_13  13
#define MCP23017_KEYMAP_14  14
#define MCP23017_KEYMAP_15  15

#define LONG_PRESS_THRESHOLD 1000
#define MULTI_TAP_THRESHOLD  2000

#endif


 // Num chars per key
uint8_t MCP23017_TapMod[15] = {1, 6, 6, 6, 6, 6, 8, 6, 8, 1, 1, 1, 1, 1, 1};

// Tap Map
static const unsigned char MCP23017_TapMap[15][13] = {
    {' '},                                           // 0: K_1
    {'a', 'b', 'c', 'A', 'B', 'C'},                  // 1: K_2
    {'d', 'e', 'f', 'D', 'E', 'F'},                  // 2: K_3
    {'g', 'h', 'i', 'G', 'H', 'I'},                  // 3: K_4
    {'j', 'k', 'l', 'J', 'K', 'L'},                  // 4: K_5
    {'m', 'n', 'o', 'M', 'N', 'O'},                  // 5: K_6
    {'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S'},        // 6: K_7
    {'t', 'u', 'v', 'T', 'U', 'V'},                  // 7: K_8
    {'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z'},        // 8: K_9
    {'*'},                                           // 9: K_10
    {' '},                                           // 10: K_11
    {KB_BSP},                                        // 11: K_12
    {KB_LEFT},                                       // 12: K_13
    {KB_SELECT},                                     // 13: K_14
    {KB_RIGHT}                                       // 14: K_15
};

// Long Press map
static const unsigned char MCP23017_LongPressMap[15] = {
    '1',            // 0:  K_1  
    '2',            // 1:  K_2  
    '3',            // 2:  K_3  
    '4',            // 3:  K_4  
    '5',            // 4:  K_5  
    '6',            // 5:  K_6  
    '7',            // 6:  K_7  
    '8',            // 7:  K_8  
    '9',            // 8:  K_9  
    '*' ,           // 9:  K_10
    '0',            // 10: K_11
    '#',            // 11: K_12
    KB_UP,          // 12: K_13
    KB_ESC,         // 13: K_14
    KB_DOWN         // 14: K_15
};

// Bit position to logical index translation (0-14)
uint8_t MCP23017_KeyMap[16] = {
    MCP23017_KEYMAP_0, MCP23017_KEYMAP_1, MCP23017_KEYMAP_2, MCP23017_KEYMAP_3,
    MCP23017_KEYMAP_4, MCP23017_KEYMAP_5, MCP23017_KEYMAP_6, MCP23017_KEYMAP_7,
    MCP23017_KEYMAP_8, MCP23017_KEYMAP_9, MCP23017_KEYMAP_10, MCP23017_KEYMAP_11,
    MCP23017_KEYMAP_12, MCP23017_KEYMAP_13, MCP23017_KEYMAP_14, MCP23017_KEYMAP_15
};

MCP23017Keyboard::MCP23017Keyboard() : m_wire(nullptr), m_addr(0), readCallback(nullptr), writeCallback(nullptr) {
    state = Init;
    last_key = UINT8_MAX;
    last_tap = 0L;
    char_idx = 0;
    queue = "";
}

void MCP23017Keyboard::begin(uint8_t addr, TwoWire *wire) {
    m_addr = addr;
    m_wire = wire;
    m_wire->begin();
    reset();
}

void MCP23017Keyboard::begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr) {
    m_addr = addr;
    m_wire = nullptr;
    writeCallback = w;
    readCallback = r;
    reset();
}

void MCP23017Keyboard::reset() {
     LOG_DEBUG("MCP23017 Reset\n");
    // Configure I/O
    writeRegister(_MCP23017_IODIRA, 0xFF); // All set as inputs
    writeRegister(_MCP23017_IODIRB, 0xFF);

    writeRegister(_MCP23017_IPOLA, 0xFF);
    writeRegister(_MCP23017_IPOLB, 0xFF);
    
    // Enable internal pull-ups
    writeRegister(_MCP23017_GPPUA, 0xFF);
    writeRegister(_MCP23017_GPPUB, 0xFF);

    // Configure interrupts: Trigger on any change
    writeRegister(_MCP23017_INTCONA, 0x00);
    writeRegister(_MCP23017_INTCONB, 0x00);
    
    // Enable interrupts for each pin
    writeRegister(_MCP23017_GPINTENA, 0xFF);
    writeRegister(_MCP23017_GPINTENB, 0x7F);

    // IOCON: Non-mirrored, standard configuration
    writeRegister(_MCP23017_IOCONA, 0x00);

    // Initial clear
    readRegister16(_MCP23017_INTCAPA);
    readRegister16(_MCP23017_GPIOA);

    state = Idle;
}

void MCP23017Keyboard::attachInterrupt(uint8_t pin, void (*func)(void)) const {
    pinMode(pin, INPUT_PULLUP);
    ::attachInterrupt(digitalPinToInterrupt(pin), func, FALLING); 
}

void MCP23017Keyboard::detachInterrupt(uint8_t pin) const {
    ::detachInterrupt(pin);
}

uint16_t MCP23017Keyboard::status() const {
    return readRegister16(_MCP23017_GPIOA);
}

uint8_t MCP23017Keyboard::keyCount() const {
    uint16_t keyRegister = readRegister16(_MCP23017_GPIOA);
    return keyCount(keyRegister);
}

uint8_t MCP23017Keyboard::keyCount(uint16_t value) const {
    uint16_t buttonState = value & _KEY_MASK;
    uint8_t numButtonsPressed = 0;
    for (uint8_t i = 0; i < 15; ++i) {
        if (buttonState & (1 << i)) {
            numButtonsPressed++;
        }
    }
    return numButtonsPressed;
}

bool MCP23017Keyboard::hasEvent() {
    return queue.length() > 0;
}

void MCP23017Keyboard::queueEvent(char next) {
    if (next == KB_NONE) return;
    queue.concat(next);
}

char MCP23017Keyboard::dequeueEvent() {
    if (queue.length() < 1) return KB_NONE;
    char next = queue.charAt(0);
    queue.remove(0, 1);
    return next;
}

void MCP23017Keyboard::trigger() {
    // Called periodically to poll the keyboard state
    if (state != Init) {
        uint16_t keyRegister = readRegister16(_MCP23017_GPIOA); // Read the state of all 16 pins (GPIO A & B)
        readRegister16(_MCP23017_INTCAPA); // Clear INT latch
        uint8_t keysPressed = keyCount(keyRegister); //Get key pressed

        if (keysPressed == 0) { // No button pressed
            if (state == Held) 
            {
                released();
            }
            state = Idle;
            return;
        }
        if (keysPressed == 1) { // Button pressed
            if (state == Held || state == HeldLong) {
                held(keyRegister);
            }
            if (state == Idle) {
                pressed(keyRegister);
            }
            return;
        }
        if (keysPressed > 1) { // Multibutton pressed
            state = Busy;
            return;
        }
    } else {
        reset();
    }
}

void MCP23017Keyboard::pressed(uint16_t keyRegister) {
    if (state == Init || state == Busy) return;
    if (keyCount(keyRegister) != 1) {
            return;
        } 

    uint16_t buttonState = keyRegister & _KEY_MASK;
    uint8_t next_pin = 0;
    for (uint8_t i = 0; i < 15; ++i) {
        if (buttonState & (1 << i)) {
            next_pin = i;
            break;
        }
    }
    
    uint8_t next_key = MCP23017_KeyMap[next_pin];
    uint32_t now = millis();
    int32_t tap_interval = now - last_tap;
    
    if (tap_interval < 0) {
        // long running, millis has overflowed.
        last_tap = 0;
        state = Busy;
        return;
    }
    
    if (next_key != last_key || tap_interval > MULTI_TAP_THRESHOLD) {
        char_idx = 0;
    } else {
        char_idx += 1;
    }
    
    last_key = next_key;
    last_tap = now;
    state = Held;
    return;
}

void MCP23017Keyboard::held(uint16_t keyRegister) {
    if (state == Init || state == Busy) return;
    if (keyCount(keyRegister) != 1) return;

    LOG_DEBUG("Held");
    uint16_t buttonState = keyRegister & _KEY_MASK;
    uint8_t next_pin = 0;
    for (uint8_t i = 0; i < 15; ++i) {
        if (buttonState & (1 << i)) {
            next_pin = i;
            break;
        }
    }
    
    uint8_t next_key = MCP23017_KeyMap[next_pin];
    uint32_t now = millis();
    int32_t held_interval = now - last_tap;
    
    if (held_interval < 0 || next_key != last_key) {
        last_tap = 0;
        state = Busy;
        return;
    }
    
    if (held_interval > LONG_PRESS_THRESHOLD) {
        state = HeldLong;
        queueEvent(MCP23017_LongPressMap[last_key]);
        last_tap = now;    
    }
}

void MCP23017Keyboard::released() {

    if (state != Held) return;

    // Check valid index
    if (last_key >= _NUM_KEYS) {
        last_key = UINT8_MAX;
        state = Idle;
        return;
    }
    
    // Multitap - clear old character
    if (char_idx > 0 && MCP23017_TapMod[last_key] > 1) {
        queueEvent(KB_BSP);
        LOG_DEBUG("Multi Press, Backspace");
    }

    // Send new character
    queueEvent(MCP23017_TapMap[last_key][(char_idx % MCP23017_TapMod[last_key])]);
}

uint8_t MCP23017Keyboard::readRegister8(uint8_t reg) const {
    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(reg);
        m_wire->endTransmission();
        m_wire->requestFrom(m_addr, (uint8_t)1);
        if (m_wire->available() < 1) return 0;
        return m_wire->read();
    }
    if (readCallback) {
        uint8_t data;
        readCallback(m_addr, reg, &data, 1);
        return data;
    }
    return 0;
}

uint16_t MCP23017Keyboard::readRegister16(uint8_t reg) const {
    uint8_t data[2] = {0};
    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(reg);
        m_wire->endTransmission();
        m_wire->requestFrom(m_addr, (uint8_t)2);
        if (m_wire->available() < 2) return 0;
        data[0] = m_wire->read();
        data[1] = m_wire->read();
    } else if (readCallback) {
        readCallback(m_addr, reg, data, 2);
    }
    return (data[1] << 8) | data[0];
}

void MCP23017Keyboard::writeRegister(uint8_t reg, uint8_t value) {
    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(reg);
        m_wire->write(value);
        m_wire->endTransmission();
    } else if (writeCallback) {
        writeCallback(m_addr, reg, &value, 1);
    }
}