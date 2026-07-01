#pragma once
#ifndef _STC8H_KEYBOARD_H_
#define _STC8H_KEYBOARD_H_

#include "configuration.h"
#include "kbI2cBase.h"
#include <Wire.h>
#if defined(ELECROW_ThinkNode_M9)

// Registers exposed by the STC8H companion MCU over I2C.
#define STC8_REG_ADDR_BATTERY 0x01
#define STC8_REG_ADDR_MATRIX_KEY 0x05
#define STC8_REG_ADDR_STATE 0x06

class STC8HKeyboard
{
  public:
    STC8HKeyboard(){};

    void begin(uint8_t addr, TwoWire *wire);

    void set_sleep_status(void);

    uint16_t bsp_get_battery_voltage();

    bool is_key_event();

    bool is_Keyboard_begin();

    bool is_key_state();

    uint8_t bsp_get_key_value();

    void set_keyboard_blight(bool state);

    void switch_flashlight();

    uint8_t readRegister(uint8_t reg);

    bool key_event;

  private:
    void writeRegister(uint8_t reg, uint8_t val);

    uint8_t _I2C_addr = TSTC8_KB_ADDR;

    TwoWire *_pWire = &Wire;

    bool Keyboard_state = false;
};

extern STC8HKeyboard Stc8HKeyBoard;

#endif
#endif
