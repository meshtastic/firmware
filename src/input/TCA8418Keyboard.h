// Based on the BBQ10 Keyboard and Adafruit TCA8418 library

#include "configuration.h"
#include <Wire.h>

class TCA8418Keyboard
{
  public:
    typedef uint8_t (*i2c_com_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

    enum KeyState { Idle = 0, Release, Press };

    struct KeyEvent {
        char key;
        KeyState state;
    };

    TCA8418Keyboard();

    void begin(uint8_t addr = TCA8418_KB_ADDR, TwoWire *wire = &Wire);
    void begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr = TCA8418_KB_ADDR);

    void reset(void);
    //  Configure the size of the keypad.
    //  All other rows and columns are set as inputs.
    bool matrix(uint8_t rows, uint8_t columns);

    //  Flush all events in the FIFO buffer + GPIO events.
    uint8_t flush(void);

    //  Key events available in the internal FIFO buffer.
    uint8_t keyCount(void) const;
    // Read key event from internal FIFO buffer
    KeyEvent keyEvent(void) const;

    uint8_t digitalRead(uint8_t pinnum) const;
    bool digitalWrite(uint8_t pinnum, uint8_t level);
    bool pinMode(uint8_t pinnum, uint8_t mode);
    bool pinIRQMode(uint8_t pinnum, uint8_t mode); // MODE  FALLING or RISING

    //  enable / disable interrupts for matrix and GPI pins
    void enableInterrupts();
    void disableInterrupts();

    //  ignore key events when FIFO buffer is full or not.
    void enableMatrixOverflow();
    void disableMatrixOverflow();

    //  debounce keys.
    void enableDebounce();
    void disableDebounce();

    uint8_t readRegister(uint8_t reg) const;
    void writeRegister(uint8_t reg, uint8_t value);

  private:
    TwoWire *m_wire;
    uint8_t m_addr;
    i2c_com_fptr_t readCallback;
    i2c_com_fptr_t writeCallback;
};
