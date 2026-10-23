#include "SixKeyNavInput.h"

#if defined(EINK_RA01S_GP02)

#include "configuration.h"
#include <nrf_gpio.h> // nrf_gpio_cfg_sense_input (HAL, header-only)
#include <string.h>

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
} NAV_TABLE[5] = {
    {PIN_BUTTON_UP, INPUT_BROKER_UP, "Up"},           {PIN_BUTTON_DOWN, INPUT_BROKER_DOWN, "Down"},
    {PIN_BUTTON_LEFT, INPUT_BROKER_LEFT, "Left"},     {PIN_BUTTON_RIGHT, INPUT_BROKER_RIGHT, "Right"},
    {PIN_BUTTON_ENTER, INPUT_BROKER_SELECT, "Enter"},
};

// ---------------------------------------------------------------------------
SixKeyNavInput::SixKeyNavInput() : concurrency::OSThread("SixKeyNav")
{
    _originName = "SixKeyNav";
    memset(_nav, 0, sizeof(_nav));
    memset(&_fn, 0, sizeof(_fn));
    _fnConsumed = false;
    _fnPressTime = 0;
    onComboUp = nullptr;
    onComboDown = nullptr;
    onComboLeft = nullptr;
    onComboRight = nullptr;
    onComboEnter = nullptr;
    onFnHeld = nullptr;
}

// Forward-declare the GpioteOverride API (defined in GpioteOverride.cpp).
extern "C" {
void registerGpiotePortPin(uint32_t physPin);
void enableGpiotePortCallback(void (*cb)(void));
}

// ---------------------------------------------------------------------------
// Shared PORT-event callback for all 6 button pins.
// Called from GPIOTE_IRQHandler (GpioteOverride.cpp) on any button edge.
// Latches level changes (captureSnapshotFromISR), then wakes the OSThread
// for event dispatch. No timing is done here (thread-side only).
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
// Latches each key's level (pressed) and a sticky release flag. No time is
// read here (millis() is not ISR-safe); all timing happens in runOnce().
// ---------------------------------------------------------------------------
void SixKeyNavInput::captureSnapshotFromISR()
{
    for (int i = 0; i < 5; i++) {
        bool down = !digitalRead(_nav[i].pin); // active-low: LOW = pressed
        if (down != _nav[i].pressed) {
            _nav[i].pressed = down;
            if (!down)
                _nav[i].release = true; // release edge (single-bit, collapse bounce)
        }
    }
    bool fnDown = !digitalRead(_fn.pin);
    if (fnDown != _fn.pressed) {
        _fn.pressed = fnDown;
        if (!fnDown)
            _fn.release = true;
    }
}

// ---------------------------------------------------------------------------
bool SixKeyNavInput::init()
{
    s_instance = this;

    // Configure all 6 pins for GPIOTE PORT event via GPIO SENSE/DETECT:
    //   registerGpiotePortPin() calls nrf_gpio_cfg_sense_input(PULLUP, SENSE_LOW)
    //   which routes through the shared PORT event - zero dedicated GPIOTE channels.
    //   System ON sleep current: ~2.4 µA total vs ~17 µA per dedicated channel.
    // NEVER use attachInterrupt() for these pins: it forces GPIOTE_CONFIG_MODE_Event
    // (dedicated channel per pin, ~17 µA each).
    for (int i = 0; i < 5; i++) {
        _nav[i].pin = NAV_TABLE[i].pin;
        registerGpiotePortPin(g_ADigitalPinMap[_nav[i].pin]);
    }

    _fn.pin = PIN_BUTTON_FN;
    registerGpiotePortPin(g_ADigitalPinMap[_fn.pin]);

    enableGpiotePortCallback(gpiotePortHandler);

    inputBroker->registerSource(this);

    // Start dormant; the ISR will call setIntervalFromNow(0) on the first edge.
    setInterval(INT32_MAX);

    LOG_INFO("SixKeyNavInput: PORT-event init ~2uA (Up=%d Down=%d Left=%d Right=%d Enter=%d Fn=%d)", PIN_BUTTON_UP,
             PIN_BUTTON_DOWN, PIN_BUTTON_LEFT, PIN_BUTTON_RIGHT, PIN_BUTTON_ENTER, PIN_BUTTON_FN);
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
// Fire a kbchar-routed system command (INPUT_BROKER_MSG_* defines).
// inputEvent is left as INPUT_BROKER_NONE; SystemCommandsModule routes on kbchar.
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
// while Fn is held (to service the long-press shutdown timer).
//
// Design: the ISR only latches level changes + a sticky release flag; all
// timing (per-key debounce, Fn long-press) runs here in thread context.
// ---------------------------------------------------------------------------
int32_t SixKeyNavInput::runOnce()
{
    uint32_t now = millis();

    // ── Fn release: reset combo/long-press state ──────────────────────────
    if (_fn.release) {
        _fn.release = false;
        _fnPressTime = 0;
        _fnConsumed = false;
    }

    // ── Fn long-press (4 s shutdown), fires once ──────────────────────────
    if (_fn.pressed) {
        if (_fnPressTime == 0) {
            _fnPressTime = now; // press start (thread-side)
            _fnConsumed = false;
        }
        if (!_fnConsumed && now - _fnPressTime >= SHUTDOWN_MS) {
            LOG_INFO("SixKeyNav: Fn held 4s SHUTDOWN");
            if (onFnHeld)
                onFnHeld();
            _fnConsumed = true;
        }
    }

    // ── Navigation buttons: release edge + per-key debounce ───────────────
    for (int i = 0; i < 5; i++) {
        if (_nav[i].release) {
            _nav[i].release = false;
            if (now - _nav[i].lastChangeMs >= DEBOUNCE_MS) {
                _nav[i].lastChangeMs = now;
                if (_fn.pressed) {
                    // Fn held → combo
                    _fnConsumed = true; // absorb the pending Fn release
                    LOG_INFO("SixKeyNav: Combo Fn+%s", NAV_TABLE[i].name);
                    switch (i) {
                    case 0:
                        if (onComboUp)
                            onComboUp();
                        break;
                    case 1:
                        if (onComboDown)
                            onComboDown();
                        break;
                    case 2:
                        if (onComboLeft)
                            onComboLeft();
                        break;
                    case 3:
                        if (onComboRight)
                            onComboRight();
                        break;
                    case 4:
                        if (onComboEnter)
                            onComboEnter();
                        break;
                    }
                } else {
                    LOG_INFO("SixKeyNav: %s pressed", NAV_TABLE[i].name);
                    fireEvent(NAV_TABLE[i].evt);
                }
            }
        }
    }

    // Activity LED while any key is physically held
#ifdef LED_EINK_ACTIVITY
    bool anyPressed = _fn.pressed;
    for (int i = 0; i < 5; i++)
        anyPressed |= _nav[i].pressed;
    digitalWrite(LED_EINK_ACTIVITY, anyPressed ? LED_STATE_ON : LED_STATE_OFF);
#endif

    // Poll every 10 ms while Fn is held (long-press timer). Nav releases are
    // serviced via ISR wakeup, so we can sleep otherwise.
    return _fn.pressed ? 10 : INT32_MAX;
}

#endif // EINK_RA01S_GP02
