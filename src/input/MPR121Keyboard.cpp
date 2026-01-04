// Based on the BBQ10 Keyboard

#include "MPR121Keyboard.h"
#include "configuration.h"
#include <Arduino.h>

#define _MPR121_REG_KEY 0x5a

#define _MPR121_REG_TOUCH_STATUS 0x00
#define _MPR121_REG_ELECTRODE_FILTERED_DATA
#define _MPR121_REG_BASELINE_VALUE 0x1E

// Baseline filters
#define _MPR121_REG_MAX_HALF_DELTA_RISING 0x2B
#define _MPR121_REG_NOISE_HALF_DELTA_RISING 0x2C
#define _MPR121_REG_NOISE_COUNT_LIMIT_RISING 0x2D
#define _MPR121_REG_FILTER_DELAY_COUNT_RISING 0x2E
#define _MPR121_REG_MAX_HALF_DELTA_FALLING 0x2F
#define _MPR121_REG_NOISE_HALF_DELTA_FALLING 0x30
#define _MPR121_REG_NOISE_COUNT_LIMIT_FALLING 0x31
#define _MPR121_REG_FILTER_DELAY_COUNT_FALLING 0x32
#define _MPR121_REG_NOISE_HALF_DELTA_TOUCHED 0x33
#define _MPR121_REG_NOISE_COUNT_LIMIT_TOUCHED 0x34
#define _MPR121_REG_FILTER_DELAY_COUNT_TOUCHED 0x35

#define _MPR121_REG_TOUCH_THRESHOLD 0x41   // First input, +2 for subsequent
#define _MPR121_REG_RELEASE_THRESHOLD 0x42 // First input, +2 for subsequent
#define _MPR121_REG_DEBOUNCE 0x5B
#define _MPR121_REG_CONFIG1 0x5C
#define _MPR121_REG_CONFIG2 0x5D
#define _MPR121_REG_ELECTRODE_CONFIG 0x5E
#define _MPR121_REG_AUTOCONF_CTRL0 0x7B
#define _MPR121_REG_AUTOCONF_CTRL1 0x7C
#define _MPR121_REG_SOFT_RESET 0x80

#define _KEY_MASK 0x0FFF // Key mask for the first 12 bits
#define _NUM_KEYS 12

#define ECR_CALIBRATION_TRACK_FROM_ZERO (0 << 6)
#define ECR_CALIBRATION_LOCK (1 << 6)
#define ECR_CALIBRATION_TRACK_FROM_PARTIAL_FILTER (2 << 6) // Recommended Typical Mode
#define ECR_CALIBRATION_TRACK_FROM_FULL_FILTER (3 << 6)
#define ECR_PROXIMITY_DETECTION_OFF (0 << 0) // Not using proximity detection
#define ECR_TOUCH_DETECTION_12CH (12 << 0)   // Using all 12 channels

#define MPR121_NONE 0x00
#define MPR121_REBOOT 0x90
#define MPR121_LEFT 0xb4
#define MPR121_UP 0xb5
#define MPR121_DOWN 0xb6
#define MPR121_RIGHT 0xb7
#define MPR121_ESC 0x1b
#define MPR121_BSP 0x08
#define MPR121_SELECT 0x0d

#define MPR121_FN_ON 0xf1
#define MPR121_FN_OFF 0xf2

#define LONG_PRESS_THRESHOLD 2000
#define MULTI_TAP_THRESHOLD 2000

uint8_t TapMod[12] = {1, 2, 1, 13, 7, 7, 7, 7, 7, 9, 7, 9}; // Num chars per key, Modulus for rotating through characters

unsigned char MPR121_TapMap[12][13] = {{MPR121_BSP},
                                       {'0', ' '},
                                       {MPR121_SELECT},
                                       {'1', '.', ',', '?', '!', ':', ';', '-', '_', '\\', '/', '(', ')'},
                                       {'2', 'a', 'b', 'c', 'A', 'B', 'C'},
                                       {'3', 'd', 'e', 'f', 'D', 'E', 'F'},
                                       {'4', 'g', 'h', 'i', 'G', 'H', 'I'},
                                       {'5', 'j', 'k', 'l', 'J', 'K', 'L'},
                                       {'6', 'm', 'n', 'o', 'M', 'N', 'O'},
                                       {'7', 'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S'},
                                       {'8', 't', 'u', 'v', 'T', 'U', 'V'},
                                       {'9', 'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z'}};

unsigned char MPR121_LongPressMap[12] = {MPR121_ESC,  ' ',         MPR121_NONE,  MPR121_NONE, MPR121_UP,   MPR121_NONE,
                                         MPR121_LEFT, MPR121_NONE, MPR121_RIGHT, MPR121_NONE, MPR121_DOWN, MPR121_NONE};

// Translation map from left to right, top to bottom layout to a more convenient layout to manufacture, matching the
// https://www.amazon.com.au/Capacitive-Sensitive-Sensitivity-Replacement-Traditional/dp/B0CTJD5KW9/ref=pd_ci_mcx_mh_mcx_views_0_title?th=1
/*uint8_t MPR121_KeyMap[12] = {
    9, 6, 3, 0,
    10, 7, 4, 1,
    11, 8, 5, 2
};*/
// Rotated Layout
uint8_t MPR121_KeyMap[12] = {2, 5, 8, 11, 1, 4, 7, 10, 0, 3, 6, 9};

MPR121Keyboard::MPR121Keyboard() : m_wire(nullptr), m_addr(0), readCallback(nullptr), writeCallback(nullptr)
{
    // LOG_DEBUG("MPR121 @ %02x", m_addr);
    state = Init;
    last_key = -1;
    last_tap = 0L;
    char_idx = 0;
    queue = "";
}

void MPR121Keyboard::begin(uint8_t addr, TwoWire *wire)
{
    m_addr = addr;
    m_wire = wire;

    m_wire->begin();

    reset();
}

void MPR121Keyboard::begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr)
{
    m_addr = addr;
    m_wire = nullptr;
    writeCallback = w;
    readCallback = r;
    reset();
}

void MPR121Keyboard::reset()
{
    LOG_DEBUG("MPR121 Reset");
    // Trigger a MPR121 Soft Reset
    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(_MPR121_REG_SOFT_RESET);
        m_wire->endTransmission();
    }
    if (writeCallback) {
        uint8_t data = 0;
        writeCallback(m_addr, _MPR121_REG_SOFT_RESET, &data, 0);
    }
    delay(100);
    // Reset Electrode Configuration to 0x00, Stop Mode
    writeRegister(_MPR121_REG_ELECTRODE_CONFIG, 0x00);
    delay(100);

    LOG_DEBUG("MPR121 Configuring");
    // Set touch release thresholds
    for (uint8_t i = 0; i < 12; i++) {
        // Set touch threshold
        writeRegister(_MPR121_REG_TOUCH_THRESHOLD + (i * 2), 10);
        delay(20);
        // Set release threshold
        writeRegister(_MPR121_REG_RELEASE_THRESHOLD + (i * 2), 5);
        delay(20);
    }
    // Configure filtering and baseline registers
    writeRegister(_MPR121_REG_MAX_HALF_DELTA_RISING, 0x05);
    delay(20);
    writeRegister(_MPR121_REG_MAX_HALF_DELTA_FALLING, 0x01);
    delay(20);
    writeRegister(_MPR121_REG_NOISE_HALF_DELTA_RISING, 0x01);
    delay(20);
    writeRegister(_MPR121_REG_NOISE_HALF_DELTA_FALLING, 0x05);
    delay(20);
    writeRegister(_MPR121_REG_NOISE_HALF_DELTA_TOUCHED, 0x00);
    delay(20);
    writeRegister(_MPR121_REG_NOISE_COUNT_LIMIT_RISING, 0x05);
    delay(20);
    writeRegister(_MPR121_REG_NOISE_COUNT_LIMIT_FALLING, 0x01);
    delay(20);
    writeRegister(_MPR121_REG_NOISE_COUNT_LIMIT_TOUCHED, 0x00);
    delay(20);
    writeRegister(_MPR121_REG_FILTER_DELAY_COUNT_RISING, 0x00);
    delay(20);
    writeRegister(_MPR121_REG_FILTER_DELAY_COUNT_FALLING, 0x00);
    delay(20);
    writeRegister(_MPR121_REG_FILTER_DELAY_COUNT_TOUCHED, 0x00);
    delay(20);
    writeRegister(_MPR121_REG_AUTOCONF_CTRL0, 0x04); // Auto-config enable
    delay(20);
    writeRegister(_MPR121_REG_AUTOCONF_CTRL1, 0x00); // Ensure no auto-config interrupt
    delay(20);
    writeRegister(_MPR121_REG_DEBOUNCE, 0x02);
    delay(20);
    writeRegister(_MPR121_REG_CONFIG1, 0x20);
    delay(20);
    writeRegister(_MPR121_REG_CONFIG2, 0x21);
    delay(20);
    // Enter run mode by Seting partial filter calibration tracking, disable proximity detection, enable 12 channels
    writeRegister(_MPR121_REG_ELECTRODE_CONFIG,
                  ECR_CALIBRATION_TRACK_FROM_FULL_FILTER | ECR_PROXIMITY_DETECTION_OFF | ECR_TOUCH_DETECTION_12CH);
    delay(100);
    LOG_DEBUG("MPR121 Run");
    state = Idle;
}

void MPR121Keyboard::attachInterrupt(uint8_t pin, void (*func)(void)) const
{
    pinMode(pin, INPUT_PULLUP);
    ::attachInterrupt(digitalPinToInterrupt(pin), func, RISING);
}

void MPR121Keyboard::detachInterrupt(uint8_t pin) const
{
    ::detachInterrupt(pin);
}

uint8_t MPR121Keyboard::status() const
{
    return readRegister16(_MPR121_REG_KEY);
}

uint8_t MPR121Keyboard::keyCount() const
{
    // Read the key register
    uint16_t keyRegister = readRegister16(_MPR121_REG_KEY);
    return keyCount(keyRegister);
}

uint8_t MPR121Keyboard::keyCount(uint16_t value) const
{
    // Mask the first 12 bits
    uint16_t buttonState = value & _KEY_MASK;

    // Count how many bits are set to 1 (i.e., how many buttons are pressed)
    uint8_t numButtonsPressed = 0;
    for (uint8_t i = 0; i < 12; ++i) {
        if (buttonState & (1 << i)) {
            numButtonsPressed++;
        }
    }

    return numButtonsPressed;
}

bool MPR121Keyboard::hasEvent()
{
    return queue.length() > 0;
}

void MPR121Keyboard::queueEvent(char next)
{
    if (next == MPR121_NONE) {
        return;
    }
    queue.concat(next);
}

char MPR121Keyboard::dequeueEvent()
{
    if (queue.length() < 1) {
        return MPR121_NONE;
    }
    char next = queue.charAt(0);
    queue.remove(0, 1);
    return next;
}

void MPR121Keyboard::trigger()
{
    // Intended to fire in response to an interrupt from the MPR121 or a longpress callback
    // Only functional if not in Init state
    if (state != Init) {
        // Read the key register
        uint16_t keyRegister = readRegister16(_MPR121_REG_KEY);
        uint8_t keysPressed = keyCount(keyRegister);
        if (keysPressed == 0) {
            // No buttons pressed
            if (state == Held)
                released();
            state = Idle;
            return;
        }
        if (keysPressed == 1) {
            // No buttons pressed
            if (state == Held || state == HeldLong)
                held(keyRegister);
            if (state == Idle)
                pressed(keyRegister);
            return;
        }
        if (keysPressed > 1) {
            // Multipress
            state = Busy;
            return;
        }
    } else {
        reset();
    }
}

void MPR121Keyboard::pressed(uint16_t keyRegister)
{
    if (state == Init || state == Busy) {
        return;
    }
    if (keyCount(keyRegister) != 1) {
        LOG_DEBUG("Multipress");
        return;
    } else {
        LOG_DEBUG("Pressed");
    }
    uint16_t buttonState = keyRegister & _KEY_MASK;
    uint8_t next_pin = 0;
    for (uint8_t i = 0; i < 12; ++i) {
        if (buttonState & (1 << i)) {
            next_pin = i;
        }
    }
    uint8_t next_key = MPR121_KeyMap[next_pin];
    LOG_DEBUG("MPR121 Pin: %i Key: %i", next_pin, next_key);
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

void MPR121Keyboard::held(uint16_t keyRegister)
{
    if (state == Init || state == Busy) {
        return;
    }
    if (keyCount(keyRegister) != 1) {
        return;
    }
    LOG_DEBUG("Held");
    uint16_t buttonState = keyRegister & _KEY_MASK;
    uint8_t next_key = 0;
    for (uint8_t i = 0; i < 12; ++i) {
        if (buttonState & (1 << i)) {
            next_key = MPR121_KeyMap[i];
        }
    }
    uint32_t now = millis();
    int32_t held_interval = now - last_tap;
    if (held_interval < 0 || next_key != last_key) {
        // long running, millis has overflowed, or a key has been switched quickly...
        last_tap = 0;
        state = Busy;
        return;
    }
    if (held_interval > LONG_PRESS_THRESHOLD) {
        // Set state to heldlong, send a longpress, and reset the timer...
        state = HeldLong; // heldlong will allow this function to still fire, but prevent a "release"
        queueEvent(MPR121_LongPressMap[last_key]);
        last_tap = now;
        LOG_DEBUG("Long Press Key: %i Map: %i", last_key, MPR121_LongPressMap[last_key]);
    }
    return;
}

void MPR121Keyboard::released()
{
    if (state != Held) {
        return;
    }
    // would clear longpress callback... later.
    if (last_key < 0 || last_key > _NUM_KEYS) { // reset to idle if last_key out of bounds
        last_key = -1;
        state = Idle;
        return;
    }
    LOG_DEBUG("Released");
    if (char_idx > 0 && TapMod[last_key] > 1) {
        queueEvent(MPR121_BSP);
        LOG_DEBUG("Multi Press, Backspace");
    }
    queueEvent(MPR121_TapMap[last_key][(char_idx % TapMod[last_key])]);
    LOG_DEBUG("Key Press: %i Index:%i if %i Map: %i", last_key, char_idx, TapMod[last_key],
              MPR121_TapMap[last_key][(char_idx % TapMod[last_key])]);
}

uint8_t MPR121Keyboard::readRegister8(uint8_t reg) const
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

uint16_t MPR121Keyboard::readRegister16(uint8_t reg) const
{
    uint8_t data[2] = {0};
    // uint8_t low = 0, high = 0;
    if (m_wire) {
        m_wire->beginTransmission(m_addr);
        m_wire->write(reg);
        m_wire->endTransmission();

        m_wire->requestFrom(m_addr, (uint8_t)2);
        if (m_wire->available() < 2)
            return 0;
        data[0] = m_wire->read();
        data[1] = m_wire->read();
    }
    if (readCallback) {
        readCallback(m_addr, reg, data, 2);
    }
    return (data[1] << 8) | data[0];
}

void MPR121Keyboard::writeRegister(uint8_t reg, uint8_t value)
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