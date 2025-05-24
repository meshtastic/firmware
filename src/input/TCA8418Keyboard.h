// Based on the MPR121 Keyboard and Adafruit TCA8418 library
#include "configuration.h"
#include <Wire.h>

#define _TCA8418_NONE 0x00
#define _TCA8418_REBOOT 0x90
#define _TCA8418_LEFT 0xb4
#define _TCA8418_UP 0xb5
#define _TCA8418_DOWN 0xb6
#define _TCA8418_RIGHT 0xb7
#define _TCA8418_ESC 0x1b
#define _TCA8418_BSP 0x08
#define _TCA8418_SELECT 0x0d

class TCA8418Keyboard
{
  public:
    typedef uint8_t (*i2c_com_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

    enum KeyState { Init = 0, Idle, Held, Busy };

    KeyState state;
    int8_t last_key;
    int8_t next_key;
    bool should_backspace;
    uint32_t last_tap;
    uint8_t char_idx;
    int32_t tap_interval;
    bool backlight_on;

    String queue;

    TCA8418Keyboard();

    void begin(uint8_t addr = XPOWERS_AXP192_AXP2101_ADDRESS, TwoWire *wire = &Wire);
    void begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr = XPOWERS_AXP192_AXP2101_ADDRESS);

    void reset(void);
    // Configure the size of the keypad.
    // All other rows and columns are set as inputs.
    bool matrix(uint8_t rows, uint8_t columns);

    // Flush all events in the FIFO buffer + GPIO events.
    uint8_t flush(void);

    // Key events available in the internal FIFO buffer.
    uint8_t keyCount(void) const;

    void trigger(void);
    void pressed(uint8_t key);
    void released(void);
    bool hasEvent(void);
    char dequeueEvent(void);
    void queueEvent(char);

    uint8_t digitalRead(uint8_t pinnum) const;
    bool digitalWrite(uint8_t pinnum, uint8_t level);
    bool pinMode(uint8_t pinnum, uint8_t mode);
    bool pinIRQMode(uint8_t pinnum, uint8_t mode); // MODE  FALLING or RISING

    // enable / disable interrupts for matrix and GPI pins
    void enableInterrupts();
    void disableInterrupts();

    // ignore key events when FIFO buffer is full or not.
    void enableMatrixOverflow();
    void disableMatrixOverflow();

    // debounce keys.
    void enableDebounce();
    void disableDebounce();

    void setBacklight(bool on);

    uint8_t readRegister(uint8_t reg) const;
    void writeRegister(uint8_t reg, uint8_t value);

  private:
    TwoWire *m_wire;
    uint8_t m_addr;
    i2c_com_fptr_t readCallback;
    i2c_com_fptr_t writeCallback;
};
