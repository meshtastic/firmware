#include "PCF8574Button.h"
#include "configuration.h"

#if defined(HAS_PCF8574_BUTTON)

#include <Wire.h>
#include "input/InputBroker.h"
extern bool osk_found;

using namespace concurrency;

PCF8574ButtonThread *pcf8574Button = nullptr;


#ifndef PCF8574_ADDR
#define PCF8574_ADDR 0x20  //  (A2=A1=A0=0)
#endif

#ifndef PCF8574_INT_PIN
#define PCF8574_INT_PIN -1  
#endif

#ifndef PCF8574_BUTTON_MAP
#define PCF8574_BUTTON_MAP { \
    INPUT_BROKER_SELECT,      /* P0: 确定 */ \
    INPUT_BROKER_DOWN,        /* P1: 下 */ \
    INPUT_BROKER_UP,          /* P2: 上 */ \
    INPUT_BROKER_LEFT,        /* P3: 左 */ \
    INPUT_BROKER_RIGHT,       /* P4: 右 */ \
    INPUT_BROKER_SELECT,      /* P5: 确定 */ \
    INPUT_BROKER_CANCEL,      /* P6: 取消 */ \
    INPUT_BROKER_NONE,        /* P7: 未使用 */ \
}
#endif

static const input_broker_event buttonEventMap[8] = PCF8574_BUTTON_MAP;

namespace {
constexpr uint32_t PCF8574_IMMEDIATE_DEBOUNCE_MS = 50;
constexpr uint32_t PCF8574_SELECT_LONG_PRESS_TIME_MS = 1000;
constexpr uint32_t PCF8574_UPDOWN_LONG_PRESS_TIME_MS = 300;
constexpr uint32_t PCF8574_UPDOWN_LONG_REPEAT_INTERVAL_MS = 300;
constexpr uint32_t PCF8574_NAV_REPEAT_DELAY_MS = 300;
constexpr uint32_t PCF8574_NAV_REPEAT_INTERVAL_MS = 150;

bool isImmediateButton(input_broker_event event)
{
    switch (event) {
    case INPUT_BROKER_UP:
    case INPUT_BROKER_DOWN:
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_RIGHT:
        return true;
    default:
        return false;
    }
}

bool isNavigationRepeatButton(input_broker_event event)
{
    switch (event) {
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_RIGHT:
        return true;
    default:
        return false;
    }
}

input_broker_event getLongPressEvent(uint8_t pin, input_broker_event event)
{
    if (pin == 0 || pin == 5) {
        return INPUT_BROKER_SELECT_LONG;
    }
    if (event == INPUT_BROKER_UP) {
        return INPUT_BROKER_UP_LONG;
    }
    if (event == INPUT_BROKER_DOWN) {
        return INPUT_BROKER_DOWN_LONG;
    }
    return INPUT_BROKER_NONE;
}

uint32_t getLongPressTimeMs(input_broker_event longPressEvent)
{
    switch (longPressEvent) {
    case INPUT_BROKER_SELECT_LONG:
        return PCF8574_SELECT_LONG_PRESS_TIME_MS;
    case INPUT_BROKER_UP_LONG:
    case INPUT_BROKER_DOWN_LONG:
        return PCF8574_UPDOWN_LONG_PRESS_TIME_MS;
    default:
        return 0;
    }
}

bool isRepeatableLongPressEvent(input_broker_event longPressEvent)
{
    switch (longPressEvent) {
    case INPUT_BROKER_UP_LONG:
    case INPUT_BROKER_DOWN_LONG:
        return true;
    default:
        return false;
    }
}
} // namespace

PCF8574ButtonThread::PCF8574ButtonThread(const char *name) : OSThread(name), lastState(0xFF), initialized(false)
{
    _originName = name;
    if (inputBroker)
        inputBroker->registerSource(this);
    
    for (int i = 0; i < 8; i++) {
        buttonStates[i].pressed = false;
        buttonStates[i].pressStartTime = 0;
        buttonStates[i].lastRepeatTime = 0;
        buttonStates[i].shortPressTriggered = false;
        buttonStates[i].longPressTriggered = false;
    }
    
    osk_found = true;
    // I2C初始化将在第一次runOnce中进行
    LOG_INFO("PCF8574 Button thread created");
}

uint8_t PCF8574ButtonThread::readPCF8574()
{
    Wire.requestFrom((uint8_t)PCF8574_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;  
}

void PCF8574ButtonThread::writePCF8574(uint8_t value)
{
    Wire.beginTransmission(PCF8574_ADDR);
    Wire.write(value);
    Wire.endTransmission();
}

int32_t PCF8574ButtonThread::runOnce()
{
    if (!initialized) {
        Wire.beginTransmission(PCF8574_ADDR);
        uint8_t error = Wire.endTransmission();
        
        if (error != 0) {
            LOG_WARN("PCF8574 not found at 0x%02X, error: %d", PCF8574_ADDR, error);
            return 1000; 
        }
        
        writePCF8574(0xFF);
        
#if PCF8574_INT_PIN >= 0
        pinMode(PCF8574_INT_PIN, INPUT_PULLUP);
        LOG_DEBUG("PCF8574 INT pin configured on GPIO %d", PCF8574_INT_PIN);
#endif
        
        uint8_t testRead = readPCF8574();
        LOG_DEBUG("PCF8574 initial read: 0x%02X\n", testRead);
        
        initialized = true;
        LOG_INFO("PCF8574 Button initialized at address 0x%02X", PCF8574_ADDR);
    }
    
    bool trackingPress = false;

    for (const auto &buttonState : buttonStates) {
        if (buttonState.pressed) {
            trackingPress = true;
            break;
        }
    }

#if PCF8574_INT_PIN >= 0
    // Only trust the interrupt line while idle. Once a key is down we must
    // keep polling, otherwise the PCF8574 INT line returns high and long-press
    // timing never reaches the threshold.
    if (!trackingPress && digitalRead(PCF8574_INT_PIN) == HIGH) {
        return 100;
    }
#endif

    uint8_t currentState = readPCF8574();
    bool anyPressedNow = false;
    uint32_t now = millis();
    
    for (int pin = 0; pin < 8; pin++) {
        if (buttonEventMap[pin] == INPUT_BROKER_NONE)
            continue;
            
        bool currentlyPressed = !((currentState >> pin) & 0x01);
        bool wasPressed = buttonStates[pin].pressed;
        input_broker_event buttonEvent = buttonEventMap[pin];
        input_broker_event longPressEvent = getLongPressEvent(pin, buttonEvent);
        uint32_t longPressTimeMs = getLongPressTimeMs(longPressEvent);
        uint32_t pressDuration = wasPressed ? (now - buttonStates[pin].pressStartTime) : 0;

        anyPressedNow |= currentlyPressed;
        
        if (currentlyPressed && !wasPressed) {
            buttonStates[pin].pressed = true;
            buttonStates[pin].pressStartTime = now;
            buttonStates[pin].lastRepeatTime = 0;
            buttonStates[pin].shortPressTriggered = false;
            buttonStates[pin].longPressTriggered = false;
            LOG_DEBUG("PCF8574 Button P%d pressed", pin);
        }
        else if (currentlyPressed && wasPressed && isImmediateButton(buttonEvent) && !buttonStates[pin].shortPressTriggered &&
                 pressDuration >= PCF8574_IMMEDIATE_DEBOUNCE_MS) {
            InputEvent evt;
            evt.source = "PCF8574Button";
            evt.inputEvent = buttonEvent;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
            buttonStates[pin].lastRepeatTime = now;
            buttonStates[pin].shortPressTriggered = true;
            this->notifyObservers(&evt);
            LOG_DEBUG("PCF8574 Button P%d immediate press -> event %d", pin, evt.inputEvent);
        }
        else if (currentlyPressed && wasPressed && isNavigationRepeatButton(buttonEvent) && buttonStates[pin].shortPressTriggered &&
                 pressDuration >= PCF8574_NAV_REPEAT_DELAY_MS &&
                 (buttonStates[pin].lastRepeatTime == 0 ||
                  (now - buttonStates[pin].lastRepeatTime) >= PCF8574_NAV_REPEAT_INTERVAL_MS)) {
            InputEvent evt;
            evt.source = "PCF8574Button";
            evt.inputEvent = buttonEvent;
            evt.kbchar = 0;
            evt.touchX = 0;
            evt.touchY = 0;
            buttonStates[pin].lastRepeatTime = now;
            this->notifyObservers(&evt);
            LOG_DEBUG("PCF8574 Button P%d repeat press -> event %d", pin, evt.inputEvent);
        }
        else if (!currentlyPressed && wasPressed) {
            buttonStates[pin].pressed = false;
            
            if (buttonStates[pin].longPressTriggered) {
                buttonStates[pin].lastRepeatTime = 0;
                buttonStates[pin].shortPressTriggered = false;
                buttonStates[pin].longPressTriggered = false;
                LOG_DEBUG("PCF8574 Button P%d released (long press)", pin);
                continue;
            }

            if (!buttonStates[pin].shortPressTriggered && (longPressTimeMs == 0 || pressDuration < longPressTimeMs)) {
                InputEvent evt;
                evt.source = "PCF8574Button";
                evt.inputEvent = buttonEvent;
                evt.kbchar = 0;
                evt.touchX = 0;
                evt.touchY = 0;
                this->notifyObservers(&evt);
                LOG_DEBUG("PCF8574 Button P%d short press -> event %d", pin, evt.inputEvent);
            }

            buttonStates[pin].lastRepeatTime = 0;
            buttonStates[pin].shortPressTriggered = false;
        }

        if (buttonStates[pin].pressed && longPressEvent != INPUT_BROKER_NONE && longPressTimeMs > 0 &&
            pressDuration >= longPressTimeMs) {
            bool shouldEmitLongPress = !buttonStates[pin].longPressTriggered;

            if (!shouldEmitLongPress && isRepeatableLongPressEvent(longPressEvent) &&
                (buttonStates[pin].lastRepeatTime == 0 ||
                 (now - buttonStates[pin].lastRepeatTime) >= PCF8574_UPDOWN_LONG_REPEAT_INTERVAL_MS)) {
                shouldEmitLongPress = true;
            }

            if (shouldEmitLongPress) {
                buttonStates[pin].longPressTriggered = true;
                buttonStates[pin].lastRepeatTime = now;

                InputEvent evt;
                evt.source = "PCF8574Button";
                evt.inputEvent = longPressEvent;
                evt.kbchar = 0;
                evt.touchX = 0;
                evt.touchY = 0;
                this->notifyObservers(&evt);
                LOG_DEBUG("PCF8574 Button P%d long press -> event %d\n", pin, evt.inputEvent);
            }
        }
    }
    
    lastState = currentState;

#if PCF8574_INT_PIN >= 0
    return anyPressedNow ? 50 : 100;
#else
    return 50;
#endif
}

#endif
