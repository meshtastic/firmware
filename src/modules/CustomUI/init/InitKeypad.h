#pragma once

#include "InitBase.h"
#include <Keypad.h>
#include <Keypad.h>

// Forward declaration
class LGFX;

/**
 * Keypad initializer for 4x4 matrix keypad
 * Only handles hardware initialization - CustomUIModule handles input logic
 * Features:
 * - Configurable key mapping
 * - Debounce handling
 */
class InitKeypad : public InitBase {
public:
    InitKeypad();
    virtual ~InitKeypad();
    
    // InitBase interface
    virtual bool init() override;
    void update() { /* No update needed - initialization only */ }
    virtual void cleanup() override;
    virtual const char* getName() const override { return "Keypad"; }
    virtual bool isReady() const override { return (keypad != nullptr); }
    
    // Keypad access for CustomUIModule
    Keypad* getKeypad() { return keypad; }
    
    // Static configuration access
    static const byte ROWS = 4;
    static const byte COLS = 4;
    static char keys[ROWS][COLS];
    static byte rowPins[ROWS];
    static byte colPins[COLS];

private:
    Keypad* keypad;
    bool initialized;
};