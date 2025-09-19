#if defined(T_LORA_PAGER)

#include "TLoraPagerKeyboard.h"
#include "main.h"

#ifndef LEDC_BACKLIGHT_CHANNEL
#define LEDC_BACKLIGHT_CHANNEL 4
#endif

#ifndef LEDC_BACKLIGHT_BIT_WIDTH
#define LEDC_BACKLIGHT_BIT_WIDTH 8
#endif

#ifndef LEDC_BACKLIGHT_FREQ
#define LEDC_BACKLIGHT_FREQ 1000 // Hz
#endif

#define _TCA8418_COLS 10
#define _TCA8418_ROWS 4
#define _TCA8418_NUM_KEYS 31

using Key = TCA8418KeyboardBase::TCA8418Key;

constexpr uint8_t modifierRightShiftKey = 29 - 1; // keynum -1
constexpr uint8_t modifierRightShift = 0b0001;
constexpr uint8_t modifierSymKey = 21 - 1;
constexpr uint8_t modifierSym = 0b0010;

// Num chars per key, Modulus for rotating through characters
static const uint8_t TLoraPagerTapMod[_TCA8418_NUM_KEYS] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                                            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

static const uint8_t TLoraPagerTapMap[_TCA8418_NUM_KEYS][3] = {
    {'q', 'Q', '1'},
    {'w', 'W', '2'},
    {'e', 'E', '3'},
    {'r', 'R', '4'},
    {'t', 'T', '5'},
    {'y', 'Y', '6'},
    {'u', 'U', '7'},
    {'i', 'I', '8'},
    {'o', 'O', '9'},
    {'p', 'P', '0'},
    {'a', 'A', '*'},
    {'s', 'S', '/'},
    {'d', 'D', '+'},
    {'f', 'F', '-'},
    {'g', 'G', '='},
    {'h', 'H', ':'},
    {'j', 'J', '\''},
    {'k', 'K', '"'},
    {'l', 'L', '@'},
    {Key::SELECT, 0x00, Key::TAB},
    {0x00, 0x00, 0x00},
    {'z', 'Z', '_'},
    {'x', 'X', '$'},
    {'c', 'C', ';'},
    {'v', 'V', '?'},
    {'b', 'B', '!'},
    {'n', 'N', ','},
    {'m', 'M', '.'},
    {0x00, 0x00, 0x00},
    {Key::BSP, 0x00, Key::ESC},
    {' ', 0x00, Key::BL_TOGGLE},
};

static bool TLoraPagerHeldMap[_TCA8418_NUM_KEYS] = {};

TLoraPagerKeyboard::TLoraPagerKeyboard()
    : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), modifierFlag(0), pressedKeysCount(0), onlyOneModifierPressed(false),
      persistedPreviousModifier(false)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ledcAttach(KB_BL_PIN, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
#else
    ledcSetup(LEDC_BACKLIGHT_CHANNEL, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
    ledcAttachPin(KB_BL_PIN, LEDC_BACKLIGHT_CHANNEL);
#endif
    reset();
}

void TLoraPagerKeyboard::reset(void)
{
    TCA8418KeyboardBase::reset();
    pinMode(KB_BL_PIN, OUTPUT);
    digitalWrite(KB_BL_PIN, LOW);
    setBacklight(false);
}

void TLoraPagerKeyboard::setBacklight(bool on)
{
    toggleBacklight(!on);
}

int8_t TLoraPagerKeyboard::keyToIndex(uint8_t key)
{
    uint8_t key_index = 0;
    int row = (key - 1) / 10;
    int col = (key - 1) % 10;

    if (row >= _TCA8418_ROWS || col >= _TCA8418_COLS) {
        return -1; // Invalid key
    }

    key_index = row * _TCA8418_COLS + col;
    return key_index;
}

void TLoraPagerKeyboard::pressed(uint8_t key)
{
    if (config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_ALL_ENABLED ||
        config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_SYSTEM_ONLY) {
        hapticFeedback();
    }

    int8_t key_index = keyToIndex(key);
    if (key_index < 0)
        return;

    if (TLoraPagerHeldMap[key_index]) {
        return;
    }

    TLoraPagerHeldMap[key_index] = true;
    pressedKeysCount++;

    uint8_t key_modifier = keyToModifierFlag(key_index);
    if (key_modifier && pressedKeysCount == 1) {
        onlyOneModifierPressed = true;
    } else {
        onlyOneModifierPressed = false;
    }
    modifierFlag |= key_modifier;
}

void TLoraPagerKeyboard::released(uint8_t key)
{
    int8_t key_index = keyToIndex(key);
    if (key_index < 0)
        return;

    if (!TLoraPagerHeldMap[key_index]) {
        return;
    }

    if (TLoraPagerTapMap[key_index][modifierFlag % TLoraPagerTapMod[key_index]] == Key::BL_TOGGLE) {
        toggleBacklight();
    } else {
        queueEvent(TLoraPagerTapMap[key_index][modifierFlag % TLoraPagerTapMod[key_index]]);
    }

    TLoraPagerHeldMap[key_index] = false;
    pressedKeysCount--;

    if (onlyOneModifierPressed) {
        onlyOneModifierPressed = false;
        if (persistedPreviousModifier) {
            modifierFlag = 0;
        }
        persistedPreviousModifier = !persistedPreviousModifier;
    } else if (persistedPreviousModifier && pressedKeysCount == 0) {
        modifierFlag = 0;
        persistedPreviousModifier = false;
    } else {
        modifierFlag &= ~keyToModifierFlag(key_index);
    }
}

void TLoraPagerKeyboard::hapticFeedback()
{
    drv.setWaveform(0, 14); // strong buzz 100%
    drv.setWaveform(1, 0);  // end waveform
    drv.go();
}

// toggle brightness of the backlight in three steps
void TLoraPagerKeyboard::toggleBacklight(bool off)
{
    static uint32_t brightness = 0;
    if (off) {
        brightness = 0;
    } else {
        if (brightness == 0) {
            brightness = 40;
        } else if (brightness == 40) {
            brightness = 127;
        } else if (brightness >= 127) {
            brightness = 0;
        }
    }
    LOG_DEBUG("Toggle backlight: %d", brightness);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ledcWrite(KB_BL_PIN, brightness);
#else
    ledcWrite(LEDC_BACKLIGHT_CHANNEL, brightness);
#endif
}

uint8_t TLoraPagerKeyboard::keyToModifierFlag(uint8_t key)
{
    if (key == modifierRightShiftKey) {
        return modifierRightShift;
    } else if (key == modifierSymKey) {
        return modifierSym;
    }
    return 0;
}

bool TLoraPagerKeyboard::isModifierKey(uint8_t key)
{
    return keyToModifierFlag(key) != 0;
}

#endif