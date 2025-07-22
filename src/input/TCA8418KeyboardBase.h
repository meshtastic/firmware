// Based on the MPR121 Keyboard and Adafruit TCA8418 library
#include "configuration.h"
#include <Wire.h>

/**
 * @brief TCA8418KeyboardBase is the base class for TCA8418 keyboard handling.
 * It provides basic functionality for reading key events, managing the keyboard matrix,
 * and handling key states. It is designed to be extended for specific keyboard implementations.
 * It supports both I2C communication and function pointers for custom I2C operations.
 */
class TCA8418KeyboardBase
{
  public:
    enum TCA8418Key : uint8_t {
        NONE = 0x00,
        BSP = 0x08,
        TAB = 0x09,
        SELECT = 0x0d,
        ESC = 0x1b,
        REBOOT = 0x90,
        LEFT = 0xb4,
        UP = 0xb5,
        DOWN = 0xb6,
        RIGHT = 0xb7,
        BT_TOGGLE = 0xAA,
        GPS_TOGGLE = 0x9E,
        MUTE_TOGGLE = 0xAC,
        SEND_PING = 0xAF,
        BL_TOGGLE = 0xAB
    };

    typedef uint8_t (*i2c_com_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

    TCA8418KeyboardBase(uint8_t rows, uint8_t columns);

    virtual void begin(uint8_t addr = TCA8418_KB_ADDR, TwoWire *wire = &Wire);
    virtual void begin(i2c_com_fptr_t r, i2c_com_fptr_t w, uint8_t addr = TCA8418_KB_ADDR);

    virtual void reset(void);
    virtual void trigger(void);

    virtual void setBacklight(bool on);

    // Key events available
    virtual bool hasEvent(void) const;
    virtual char dequeueEvent(void);

  protected:
    enum KeyState { Init, Idle, Held, Busy };

    enum TCA8418Register : uint8_t {
        TCA8418_REG_RESERVED = 0x00,
        TCA8418_REG_CFG = 0x01,
        TCA8418_REG_INT_STAT = 0x02,
        TCA8418_REG_KEY_LCK_EC = 0x03,
        TCA8418_REG_KEY_EVENT_A = 0x04,
        TCA8418_REG_KEY_EVENT_B = 0x05,
        TCA8418_REG_KEY_EVENT_C = 0x06,
        TCA8418_REG_KEY_EVENT_D = 0x07,
        TCA8418_REG_KEY_EVENT_E = 0x08,
        TCA8418_REG_KEY_EVENT_F = 0x09,
        TCA8418_REG_KEY_EVENT_G = 0x0A,
        TCA8418_REG_KEY_EVENT_H = 0x0B,
        TCA8418_REG_KEY_EVENT_I = 0x0C,
        TCA8418_REG_KEY_EVENT_J = 0x0D,
        TCA8418_REG_KP_LCK_TIMER = 0x0E,
        TCA8418_REG_UNLOCK_1 = 0x0F,
        TCA8418_REG_UNLOCK_2 = 0x10,
        TCA8418_REG_GPIO_INT_STAT_1 = 0x11,
        TCA8418_REG_GPIO_INT_STAT_2 = 0x12,
        TCA8418_REG_GPIO_INT_STAT_3 = 0x13,
        TCA8418_REG_GPIO_DAT_STAT_1 = 0x14,
        TCA8418_REG_GPIO_DAT_STAT_2 = 0x15,
        TCA8418_REG_GPIO_DAT_STAT_3 = 0x16,
        TCA8418_REG_GPIO_DAT_OUT_1 = 0x17,
        TCA8418_REG_GPIO_DAT_OUT_2 = 0x18,
        TCA8418_REG_GPIO_DAT_OUT_3 = 0x19,
        TCA8418_REG_GPIO_INT_EN_1 = 0x1A,
        TCA8418_REG_GPIO_INT_EN_2 = 0x1B,
        TCA8418_REG_GPIO_INT_EN_3 = 0x1C,
        TCA8418_REG_KP_GPIO_1 = 0x1D,
        TCA8418_REG_KP_GPIO_2 = 0x1E,
        TCA8418_REG_KP_GPIO_3 = 0x1F,
        TCA8418_REG_GPI_EM_1 = 0x20,
        TCA8418_REG_GPI_EM_2 = 0x21,
        TCA8418_REG_GPI_EM_3 = 0x22,
        TCA8418_REG_GPIO_DIR_1 = 0x23,
        TCA8418_REG_GPIO_DIR_2 = 0x24,
        TCA8418_REG_GPIO_DIR_3 = 0x25,
        TCA8418_REG_GPIO_INT_LVL_1 = 0x26,
        TCA8418_REG_GPIO_INT_LVL_2 = 0x27,
        TCA8418_REG_GPIO_INT_LVL_3 = 0x28,
        TCA8418_REG_DEBOUNCE_DIS_1 = 0x29,
        TCA8418_REG_DEBOUNCE_DIS_2 = 0x2A,
        TCA8418_REG_DEBOUNCE_DIS_3 = 0x2B,
        TCA8418_REG_GPIO_PULL_1 = 0x2C,
        TCA8418_REG_GPIO_PULL_2 = 0x2D,
        TCA8418_REG_GPIO_PULL_3 = 0x2E
    };

    // Pin IDs for matrix rows/columns
    enum TCA8418PinId : uint8_t {
        TCA8418_ROW0, // Pin ID for row 0
        TCA8418_ROW1, // Pin ID for row 1
        TCA8418_ROW2, // Pin ID for row 2
        TCA8418_ROW3, // Pin ID for row 3
        TCA8418_ROW4, // Pin ID for row 4
        TCA8418_ROW5, // Pin ID for row 5
        TCA8418_ROW6, // Pin ID for row 6
        TCA8418_ROW7, // Pin ID for row 7
        TCA8418_COL0, // Pin ID for column 0
        TCA8418_COL1, // Pin ID for column 1
        TCA8418_COL2, // Pin ID for column 2
        TCA8418_COL3, // Pin ID for column 3
        TCA8418_COL4, // Pin ID for column 4
        TCA8418_COL5, // Pin ID for column 5
        TCA8418_COL6, // Pin ID for column 6
        TCA8418_COL7, // Pin ID for column 7
        TCA8418_COL8, // Pin ID for column 8
        TCA8418_COL9  // Pin ID for column 9
    };

    virtual void pressed(uint8_t key);
    virtual void released(void);

    virtual void queueEvent(char);

    virtual ~TCA8418KeyboardBase() {}

  protected:
    // Set the size of the keypad matrix
    // All other rows and columns are set as inputs.
    bool matrix(uint8_t rows, uint8_t columns);

    uint8_t keyCount(void) const;

    // Flush all events in the FIFO buffer + GPIO events.
    uint8_t flush(void);

    // debounce keys.
    void enableDebounce();
    void disableDebounce();

    // enable / disable interrupts for matrix and GPI pins
    void enableInterrupts();
    void disableInterrupts();

    // ignore key events when FIFO buffer is full or not.
    void enableMatrixOverflow();
    void disableMatrixOverflow();

    uint8_t digitalRead(uint8_t pinnum) const;
    bool digitalWrite(uint8_t pinnum, uint8_t level);
    bool pinMode(uint8_t pinnum, uint8_t mode);
    bool pinIRQMode(uint8_t pinnum, uint8_t mode); // MODE  FALLING or RISING
    uint8_t readRegister(uint8_t reg) const;
    void writeRegister(uint8_t reg, uint8_t value);

  protected:
    uint8_t rows;
    uint8_t columns;
    KeyState state;
    String queue;

  private:
    TwoWire *m_wire;
    uint8_t m_addr;
    i2c_com_fptr_t readCallback;
    i2c_com_fptr_t writeCallback;
};
