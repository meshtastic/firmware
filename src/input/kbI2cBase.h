#pragma once

#include "BBQ10Keyboard.h"
#include "InputBroker.h"
#include "MPR121Keyboard.h"
#include "Wire.h"
#include "concurrency/OSThread.h"

#include <memory>
#if defined(T_DECK_PRO)
#include "sleep.h"
#endif

class TCA8418KeyboardBase;

class KbI2cBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit KbI2cBase(const char *name);
    // Out-of-line: TCA8418KeyboardBase is only forward-declared here, so the unique_ptr
    // deleter must be instantiated in the .cpp where the type is complete
    ~KbI2cBase();
    void toggleBacklight(bool on);
    // Whether the attached keyboard has a light that follows uiconfig.keyboard_backlight
    bool hasKeyboardLight() const;

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;

    TwoWire *i2cBus = 0;

    BBQ10Keyboard Q10keyboard;
    MPR121Keyboard MPRkeyboard;
    std::unique_ptr<TCA8418KeyboardBase> TCAKeyboard;
    bool is_sym = false;

#if defined(T_DECK_PRO)
    // Light sleep arms KB_INT as a level wake and leaves that interrupt type behind, so the edge ISR is off across it
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
    CallbackObserver<KbI2cBase, void *> lsObserver = CallbackObserver<KbI2cBase, void *>(this, &KbI2cBase::beforeLightSleep);
    CallbackObserver<KbI2cBase, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<KbI2cBase, esp_sleep_wakeup_cause_t>(this, &KbI2cBase::afterLightSleep);
#endif

    void driveKeyboardLight(bool on);
    void keyboardLightTyped();
    void keyboardLightShortcut();
    // Applies the light's mode; returns ms until it next needs calling (UINT32_MAX: not until something changes)
    uint32_t updateKeyboardLight();

    bool screenOn = true;
    bool typingLit = false; // WHILE_TYPING: lit by a key press (or Alt+B) less than KEYBOARD_LIGHT_TYPING_MS ago
    uint32_t lastTypingMs = 0;
    bool keyboardLit = false; // what the light is driven to
};