#include "kbMatrixBase.h"
#include "configuration.h"

#ifdef INPUTBROKER_MATRIX_TYPE

const byte keys_cols[] = KEYS_COLS;
const byte keys_rows[] = KEYS_ROWS;

#if INPUTBROKER_MATRIX_TYPE == 1

unsigned char KeyMap[3][sizeof(keys_rows)][sizeof(keys_cols)] = {{{' ', '.', 'm', 'n', 'b', 0xb6},
                                                                  {0x0d, 'l', 'k', 'j', 'h', 0xb4},
                                                                  {'p', 'o', 'i', 'u', 'y', 0xb5},
                                                                  {0x08, 'z', 'x', 'c', 'v', 0xb7},
                                                                  {'a', 's', 'd', 'f', 'g', 0x09},
                                                                  {'q', 'w', 'e', 'r', 't', 0x1a}},
                                                                 {// SHIFT
                                                                  {':', ';', 'M', 'N', 'B', 0xb6},
                                                                  {0x0d, 'L', 'K', 'J', 'H', 0xb4},
                                                                  {'P', 'O', 'I', 'U', 'Y', 0xb5},
                                                                  {0x08, 'Z', 'X', 'C', 'V', 0xb7},
                                                                  {'A', 'S', 'D', 'F', 'G', 0x09},
                                                                  {'Q', 'W', 'E', 'R', 'T', 0x1a}},
                                                                 {// SHIFT-SHIFT
                                                                  {'_', ',', '>', '<', '"', '{'},
                                                                  {'~', '-', '*', '&', '+', '['},
                                                                  {'0', '9', '8', '7', '6', '}'},
                                                                  {'=', '(', ')', '?', '/', ']'},
                                                                  {'!', '@', '#', '$', '%', '\\'},
                                                                  {'1', '2', '3', '4', '5', 0x1a}}};
#endif

KbMatrixBase::KbMatrixBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

int32_t KbMatrixBase::runOnce()
{
    if (!INPUTBROKER_MATRIX_TYPE) {
        // Input device is not requested.
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do port setup
        firstTime = 0;
        for (byte i = 0; i < sizeof(keys_rows); i++) {
            pinMode(keys_rows[i], OUTPUT);
            digitalWrite(keys_rows[i], HIGH);
        }
        for (byte i = 0; i < sizeof(keys_cols); i++) {
            pinMode(keys_cols[i], INPUT_PULLUP);
        }
    }

    key = 0;

    if (INPUTBROKER_MATRIX_TYPE == 1) {
        // scan for keypresses
        for (byte i = 0; i < sizeof(keys_rows); i++) {
            digitalWrite(keys_rows[i], LOW);
            for (byte j = 0; j < sizeof(keys_cols); j++) {
                if (digitalRead(keys_cols[j]) == LOW) {
                    key = KeyMap[shift][i][j];
                }
            }
            digitalWrite(keys_rows[i], HIGH);
        }
        // debounce
        if (key != prevkey) {
            if (key != 0) {
                LOG_DEBUG("Key 0x%x pressed\n", key);
                // reset shift now that we have a keypress
                InputEvent e;
                e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                e.source = this->_originName;
                switch (key) {
                case 0x1b: // ESC
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL;
                    break;
                case 0x08: // Back
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK;
                    e.kbchar = key;
                    break;
                case 0xb5: // Up
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
                    break;
                case 0xb6: // Down
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
                    break;
                case 0xb4: // Left
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT;
                    e.kbchar = key;
                    break;
                case 0xb7: // Right
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT;
                    e.kbchar = key;
                    break;
                case 0x0d: // Enter
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
                    break;
                case 0x00: // nopress
                    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
                    break;
                case 0x1a: // Shift
                    shift++;
                    if (shift > 2) {
                        shift = 0;
                    }
                    break;
                default: // all other keys
                    e.inputEvent = ANYKEY;
                    e.kbchar = key;
                    break;
                }
                if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
                    this->notifyObservers(&e);
                }
            }
            prevkey = key;
        }

    } else {
        LOG_WARN("Unknown kb_model 0x%02x\n", INPUTBROKER_MATRIX_TYPE);
        return disable();
    }
    return 50; // Keyscan every 50msec to avoid key bounce
}

#endif // INPUTBROKER_MATRIX_TYPE