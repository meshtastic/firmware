#pragma once

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "Observer.h"
#include "input/InputBroker.h"
#include "input/kbI2cBase.h"
#include "modules/SystemCommandsModule.h"

namespace NicheGraphics::InkHUD
{
class InkHUD;
class AlignKeyboardApplet;
} // namespace NicheGraphics::InkHUD

namespace NicheGraphics::Inputs
{

// Feeds an M5 CardKB found on I2C into InkHUD.
// Arrow keys go through InkHUD::navUp() etc, so they follow the screen rotation.
// The CardKB keeps its own alignment, separate from the joystick's, trained with the
// AlignKeyboard applet the first time the keyboard is seen
class CardKbInput : public Observer<const InputEvent *>
{
  public:
    static CardKbInput *getInstance();

    // Call before InkHUD::begin(), so the AlignKeyboard applet gets created
    // Does nothing unless a CardKB was detected by the boot I2C scan
    void detect();

    // Call after InkHUD::begin()
    void begin();

  protected:
    int onNotify(const InputEvent *event) override;

  private:
    struct Saved {
        uint8_t alignment = 0; // Multiples of 90 degrees clockwise, as InkHUD joystick alignment
        bool aligned = false;  // Set once AlignKeyboard has been completed (or skipped)
    };

    CardKbInput() = default;

    bool keyboardTrainingActive();
    bool joystickTrainingActive();
    bool startTrainingIfNeeded();
    void navigate(void (InkHUD::InkHUD::*direction)());
    void trainingClosed();
    InkHUD::AlignKeyboardApplet *alignKeyboardApplet();

    bool navigating = false;  // Inside navigate(): the alignment is only final once it returns
    bool savePending = false; // Training closed during navigate(): save once the alignment is stored
    bool detected = false;
    KbI2cBase *keyboard = nullptr;
    SystemCommandsModule systemCommands;
    Saved saved;
};

} // namespace NicheGraphics::Inputs

#endif
