#include "SixKeyNavInput.h"
#include "GpioteOverride.h"
#include "NavCombos.h"

#if defined(Nodara)

#include "configuration.h"
#include <nrf_gpio.h> // nrf_gpio_cfg_sense_input (HAL, header-only)

SixKeyNavInput *sixKeyNavInput = nullptr;

// File-local singleton pointer used by the static ISR.
static SixKeyNavInput *s_instance = nullptr;

// ---------------------------------------------------------------------------
// Navigation button table: Up / Down / Left / Right / Enter
// Indexed 0-4 to match _nav[].
// ---------------------------------------------------------------------------
static const struct {
    uint8_t pin;
    input_broker_event evt;
    const char *name;
} NAV_TABLE[BTN_NUM_MAX] = {
    {PIN_BUTTON_UP, INPUT_BROKER_UP, "Up"},
    {PIN_BUTTON_DOWN, INPUT_BROKER_DOWN, "Down"},
    {PIN_BUTTON_LEFT, INPUT_BROKER_LEFT, "Left"},
    {PIN_BUTTON_RIGHT, INPUT_BROKER_RIGHT, "Right"},
    {PIN_BUTTON_A, INPUT_BROKER_SELECT, "Enter"},
    {PIN_BUTTON_B, INPUT_BROKER_CANCEL, "Cancel"}
};

// ---------------------------------------------------------------------------
SixKeyNavInput::SixKeyNavInput() : concurrency::OSThread("SixKeyNav")
{
    _originName = "SixKeyNav";
}

// ---------------------------------------------------------------------------
// Shared PORT-event callback for all 6 button pins.
// ---------------------------------------------------------------------------
static void gpiotePortHandler()
{
    if (!s_instance)
        return;
    s_instance->captureSnapshotFromISR();
    s_instance->setIntervalFromNow(0);
    runASAP = true;
    BaseType_t higherWake = 0;
    concurrency::mainDelay.interruptFromISR(&higherWake);
}

// ---------------------------------------------------------------------------
// Called from the ISR (gpiotePortHandler) with interrupts masked.
// ---------------------------------------------------------------------------
void SixKeyNavInput::captureSnapshotFromISR()
{
    for (int i = 0; i < BTN_NUM_MAX; i++) {
        if (digitalRead(NAV_TABLE[i].pin) == PIN_BTN_ACTIVE) {
            if (!btn_state[i].press) {
                btn_state[i].press = true;
            }
        } else {
            if (btn_state[i].press) {
                btn_state[i].press = false;
                btn_state[i].release = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
bool SixKeyNavInput::init()
{
    s_instance = this;
    memset(btn_state, 0, sizeof(btn_state));
    lastTime = 0;

    for (int i = 0; i < BTN_NUM_MAX; i++) {
        registerGpiotePortPin(NAV_TABLE[i].pin);
    }

    enableGpiotePortCallback(gpiotePortHandler);

    inputBroker->registerSource(this);

    // Start dormant; the ISR will call setIntervalFromNow(0) on the first edge.
    setInterval(INT32_MAX);

    LOG_INFO("SixKeyNavInput: PORT-event init");
    return true;
}

// ---------------------------------------------------------------------------
void SixKeyNavInput::fireEvent(input_broker_event evt)
{
    InputEvent e = {};
    e.source = _originName;
    e.inputEvent = evt;
    this->notifyObservers(&e);
}

// ---------------------------------------------------------------------------
void SixKeyNavInput::fireKbEvent(uint8_t kbchar)
{
    InputEvent e = {};
    e.source = _originName;
    e.inputEvent = INPUT_BROKER_NONE;
    e.kbchar = kbchar;
    this->notifyObservers(&e);
}

// ---------------------------------------------------------------------------
// Called by the OSThread scheduler after an ISR wakes us, or periodically
// ---------------------------------------------------------------------------
int32_t SixKeyNavInput::runOnce()
{
    uint32_t now = millis();

    for (int i = 0; i < BTN_NUM_MAX; i++) {
        if (btn_state[i].release) {
            btn_state[i].release = false;
            if (now > lastTime + DEBOUNCE_MS) {
                lastTime = now;

                if (btn_state[BTN_NUM_MAX-1].press) {
                    LOG_INFO("Fn + %s combo", NAV_TABLE[i].name);
                    btn_state[BTN_NUM_MAX-1].press = false;
                    btn_state[BTN_NUM_MAX-1].release = false;
                    switch (NAV_TABLE[i].evt) {
                        case INPUT_BROKER_UP: navComboUp();break;
                        case INPUT_BROKER_DOWN: navComboDown();break;
                        case INPUT_BROKER_LEFT: navComboLeft();break;
                        case INPUT_BROKER_RIGHT: navComboRight();break;
                        case INPUT_BROKER_SELECT: navComboEnter();break;
                        default: break;
                    }
                } else {
                    LOG_INFO("%s BTN release", NAV_TABLE[i].name);
                    fireEvent(NAV_TABLE[i].evt);
                }
            } else {
                LOG_INFO("%s BTN skip", NAV_TABLE[i].name);
            }
        }
    }

    bool ret = false;
    for (int i = 0; i < BTN_NUM_MAX; i++) {
        if (now <= lastTime + DEBOUNCE_MS) {
            ret = true;
            break;
        }
    }

    return (ret ? DEBOUNCE_MS/2 : INT32_MAX);
}

#endif // Nodara
