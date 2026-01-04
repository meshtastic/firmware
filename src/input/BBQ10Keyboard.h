// Based on arturo182 arduino_bbq10kbd library https://github.com/arturo182/arduino_bbq10kbd

#include "configuration.h"
#include <Wire.h>

#define KEY_MOD_ALT (0x1A)
#define KEY_MOD_SHL (0x1B)
#define KEY_MOD_SHR (0x1C)
#define KEY_MOD_SYM (0x1D)

class BBQ10Keyboard
{
  public:
    typedef uint8_t (*i2c_com_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

    enum KeyState { StateIdle = 0, StatePress, StateLongPress, StateRelease };

    struct KeyEvent {
        char key;
        KeyState state;
    };

    BBQ10Keyboard();

    void begin(uint8_t addr = BBQ10_KB_ADDR, TwoWire *wire = &Wire);

    void begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr = BBQ10_KB_ADDR);

    void reset(void);

    void attachInterrupt(uint8_t pin, void (*func)(void)) const;
    void detachInterrupt(uint8_t pin) const;
    void clearInterruptStatus(void);

    uint8_t status(void) const;
    uint8_t keyCount(void) const;
    KeyEvent keyEvent(void) const;

    float backlight() const;
    void setBacklight(float value);

    uint8_t readRegister8(uint8_t reg) const;
    uint16_t readRegister16(uint8_t reg) const;
    void writeRegister(uint8_t reg, uint8_t value);

  private:
    TwoWire *m_wire;
    uint8_t m_addr;
    i2c_com_fptr_t readCallback;
    i2c_com_fptr_t writeCallback;
};
