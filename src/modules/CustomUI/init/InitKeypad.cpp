#include "InitKeypad.h"
#include <Keypad.h>
#include <Arduino.h>

// Add missing logging
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

// Static keypad configuration (horizontally mirrored)
char InitKeypad::keys[InitKeypad::ROWS][InitKeypad::COLS] = {
    {'D', '#', '0', '*'},
    {'C', '9', '8', '7'},
    {'B', '6', '5', '4'},
    {'A', '3', '2', '1'}
};


// byte InitKeypad::rowPins[InitKeypad::ROWS] = {26, 21, 20, 19};
// byte InitKeypad::colPins[InitKeypad::COLS] = {48, 47, 33, 34};

byte InitKeypad::rowPins[InitKeypad::ROWS] = {48, 47, 33, 34};
byte InitKeypad::colPins[InitKeypad::COLS] = {26, 21, 20, 19};

InitKeypad::InitKeypad() 
    : keypad(nullptr), 
      initialized(false) {
    LOG_INFO("🔧 InitKeypad: Constructor");
}

InitKeypad::~InitKeypad() {
    cleanup();
}

bool InitKeypad::init() {
    LOG_INFO("🔧 InitKeypad: Initializing 4x4 matrix keypad...");
    
    // Create keypad instance
    keypad = new Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
    keypad->setDebounceTime(50); // 50ms debounce
    
    initialized = true;
    LOG_INFO("🔧 InitKeypad: Keypad initialized with 50ms debounce");
    return true;
}

void InitKeypad::cleanup() {
    LOG_INFO("🔧 InitKeypad: Cleaning up keypad...");
    
    if (keypad) {
        // Safe cleanup without virtual destructor warning
        keypad = nullptr; // Just nullify pointer, let destructor handle cleanup
        initialized = false;
        LOG_INFO("🔧 InitKeypad: Keypad cleaned up");
    }
}