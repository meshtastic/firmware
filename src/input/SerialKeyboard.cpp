#include "SerialKeyboard.h"
#include "configuration.h"
#include <Throttle.h>

#ifdef INPUTBROKER_SERIAL_TYPE
#define CANNED_MESSAGE_MODULE_ENABLE 1 // in case it's not set in the variant file

#if INPUTBROKER_SERIAL_TYPE == 1 // It's a Chatter
// 3 SHIFT level (lower case, upper case, numbers), up to 4 repeated presses, button number
unsigned char KeyMap[3][4][10] = {{{'.', 'a', 'd', 'g', 'j', 'm', 'p', 't', 'w', ' '},
                                   {',', 'b', 'e', 'h', 'k', 'n', 'q', 'u', 'x', ' '},
                                   {'?', 'c', 'f', 'i', 'l', 'o', 'r', 'v', 'y', ' '},
                                   {'1', '2', '3', '4', '5', '6', 's', '8', 'z', ' '}}, // low case
                                  {{'!', 'A', 'D', 'G', 'J', 'M', 'P', 'T', 'W', ' '},
                                   {'+', 'B', 'E', 'H', 'K', 'N', 'Q', 'U', 'X', ' '},
                                   {'-', 'C', 'F', 'I', 'L', 'O', 'R', 'V', 'Y', ' '},
                                   {'1', '2', '3', '4', '5', '6', 'S', '8', 'Z', ' '}}, // upper case
                                  {{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
                                   {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
                                   {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
                                   {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}}}; // numbers

#endif

SerialKeyboard::SerialKeyboard(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

void SerialKeyboard::erase()
{
    InputEvent e;
    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
    e.kbchar = 0x08;
    e.source = this->_originName;
    this->notifyObservers(&e);
}

int32_t SerialKeyboard::runOnce()
{
    if (!INPUTBROKER_SERIAL_TYPE) {
        // Input device is not requested.
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do port setup
        firstTime = 0;
        pinMode(KB_LOAD, OUTPUT);
        pinMode(KB_CLK, OUTPUT);
        pinMode(KB_DATA, INPUT);
        digitalWrite(KB_LOAD, HIGH);
        digitalWrite(KB_CLK, LOW);
        prevKeys = 0b1111111111111111;
        LOG_DEBUG("Serial Keyboard setup");
    }

    if (INPUTBROKER_SERIAL_TYPE == 1) { // Chatter V1.0 & V2.0 keypads
        // scan for keypresses
        // Write pulse to load pin
        digitalWrite(KB_LOAD, LOW);
        delayMicroseconds(5);
        digitalWrite(KB_LOAD, HIGH);
        delayMicroseconds(5);

        // Get data from 74HC165
        byte shiftRegister1 = shiftIn(KB_DATA, KB_CLK, LSBFIRST);
        byte shiftRegister2 = shiftIn(KB_DATA, KB_CLK, LSBFIRST);

        keys = (shiftRegister1 << 8) + shiftRegister2;

        // Print to serial monitor
        // Serial.print (shiftRegister1, BIN);
        // Serial.print ("X");
        // Serial.println (shiftRegister2, BIN);

        if (!Throttle::isWithinTimespanMs(lastPressTime, 500)) {
            quickPress = 0;
        }

        if (keys < prevKeys) { // a new key has been pressed (and not released), doesn't works for multiple presses at once but
                               // shouldn't be a limitation
            InputEvent e;
            e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
            e.source = this->_originName;
            // SELECT OR SEND OR CANCEL EVENT
            if (!(shiftRegister2 & (1 << 3))) {
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
            } else if (!(shiftRegister2 & (1 << 2))) {
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                e.kbchar = INPUT_BROKER_MSG_RIGHT;
            } else if (!(shiftRegister2 & (1 << 1))) {
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
            } else if (!(shiftRegister2 & (1 << 0))) {
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
            }

            // TEXT INPUT EVENT
            else if (!(shiftRegister1 & (1 << 4))) {
                keyPressed = 0;
            } else if (!(shiftRegister1 & (1 << 3))) {
                keyPressed = 1;
            } else if (!(shiftRegister2 & (1 << 4))) {
                keyPressed = 2;
            } else if (!(shiftRegister1 & (1 << 5))) {
                keyPressed = 3;
            } else if (!(shiftRegister1 & (1 << 2))) {
                keyPressed = 4;
            } else if (!(shiftRegister2 & (1 << 5))) {
                keyPressed = 5;
            } else if (!(shiftRegister1 & (1 << 6))) {
                keyPressed = 6;
            } else if (!(shiftRegister1 & (1 << 1))) {
                keyPressed = 7;
            } else if (!(shiftRegister2 & (1 << 6))) {
                keyPressed = 8;
            } else if (!(shiftRegister1 & (1 << 0))) {
                keyPressed = 9;
            }
            // BACKSPACE or TAB
            else if (!(shiftRegister1 & (1 << 7))) {
                if (shift == 0 || shift == 2) { // BACKSPACE
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                    e.kbchar = 0x08;
                } else { // shift = 1 => TAB
                    e.inputEvent = ANYKEY;
                    e.kbchar = 0x09;
                }
            }
            // SHIFT
            else if (!(shiftRegister2 & (1 << 7))) {
                keyPressed = 10;
            }

            if (keyPressed < 11) {
                if (keyPressed == lastKeyPressed && millis() - lastPressTime < 500) {
                    quickPress += 1;
                    if (quickPress > 3) {
                        quickPress = 0;
                    }
                }
                if (keyPressed != lastKeyPressed) {
                    quickPress = 0;
                }
                if (keyPressed < 10) { // if it's a letter
                    if (keyPressed == lastKeyPressed && millis() - lastPressTime < 500) {
                        erase();
                    }
                    e.inputEvent = ANYKEY;
                    e.kbchar = char(KeyMap[shift][quickPress][keyPressed]);
                } else { // then it's shift
                    shift += 1;
                    if (shift > 2) {
                        shift = 0;
                    }
                }
                lastPressTime = millis();
                lastKeyPressed = keyPressed;
                keyPressed = 13;
            }

            if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                this->notifyObservers(&e);
            }
        }
        prevKeys = keys;
    }
    return 50;
}

#endif // INPUTBROKER_SERIAL_TYPE