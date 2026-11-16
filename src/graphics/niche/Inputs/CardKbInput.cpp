#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./CardKbInput.h"

#include "configuration.h"
#include "graphics/niche/InkHUD/Applets/System/AlignKeyboard/AlignKeyboardApplet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/Utils/FlashData.h"
#include "main.h"

using namespace NicheGraphics;

Inputs::CardKbInput *Inputs::CardKbInput::getInstance()
{
    static CardKbInput instance;
    return &instance;
}

void Inputs::CardKbInput::detect()
{
    // kb_model 0x00 is the CardKB protocol; other keyboards need their own mapping
    if (!kb_found || kb_model != 0x00 || cardkb_found.address != CARDKB_ADDR)
        return;

    detected = true;
    InkHUD::InkHUD::getInstance()->physicalKeyboard = true;
}

void Inputs::CardKbInput::begin()
{
    if (!detected || keyboard)
        return;

    FlashData<Saved>::load(&saved, "cardkb");

    keyboard = new KbI2cBase("CardKB");
    observe(keyboard);
    LOG_INFO("CardKB attached to InkHUD");

    if (InkHUD::AlignKeyboardApplet *training = alignKeyboardApplet())
        training->onClosed = [this]() { trainingClosed(); };

    startTrainingIfNeeded();
}

bool Inputs::CardKbInput::keyboardTrainingActive()
{
    InkHUD::SystemApplet *applet = InkHUD::InkHUD::getInstance()->getSystemApplet("AlignKeyboard");
    return applet && applet->isForeground();
}

bool Inputs::CardKbInput::joystickTrainingActive()
{
    InkHUD::SystemApplet *applet = InkHUD::InkHUD::getInstance()->getSystemApplet("AlignStick");
    return applet && applet->isForeground();
}

// First time this keyboard is seen: have the user point its arrows at the screen.
// Waits for the joystick's own training to finish first, so the two screens never overlap
bool Inputs::CardKbInput::startTrainingIfNeeded()
{
    if (saved.aligned || keyboardTrainingActive() || joystickTrainingActive())
        return false;

    InkHUD::InkHUD::getInstance()->openAlignKeyboard();
    return true;
}

InkHUD::AlignKeyboardApplet *Inputs::CardKbInput::alignKeyboardApplet()
{
    return static_cast<InkHUD::AlignKeyboardApplet *>(InkHUD::InkHUD::getInstance()->getSystemApplet("AlignKeyboard"));
}

// AlignKeyboard was dismissed: a direction was pressed, or the training was skipped
void Inputs::CardKbInput::trainingClosed()
{
    saved.aligned = true;

    // A direction in progress still has to store the alignment it produces: see navigate()
    if (navigating)
        savePending = true;
    else
        FlashData<Saved>::save(&saved, "cardkb");
}

// Run an InkHUD joystick direction using this keyboard's alignment instead of the joystick's
void Inputs::CardKbInput::navigate(void (InkHUD::InkHUD::*direction)())
{
    InkHUD::InkHUD *hud = InkHUD::InkHUD::getInstance();

    // On the joystick's own training screen the keyboard simply stands in for the joystick
    if (joystickTrainingActive()) {
        (hud->*direction)();
        return;
    }

    const uint8_t joystickAlignment = hud->persistence->settings.joystick.alignment;
    InkHUD::AlignKeyboardApplet *training = alignKeyboardApplet();

    struct NavigatingScope {
        bool &flag;
        explicit NavigatingScope(bool &f) : flag(f) { flag = true; }
        ~NavigatingScope() { flag = false; }
    };
    NavigatingScope scope(navigating);
    if (training)
        training->keyboardInput = true;

    hud->persistence->settings.joystick.alignment = saved.alignment;
    (hud->*direction)(); // AlignKeyboard adjusts the alignment from within this call
    saved.alignment = hud->persistence->settings.joystick.alignment;
    hud->persistence->settings.joystick.alignment = joystickAlignment;

    if (training)
        training->keyboardInput = false;

    if (savePending) {
        savePending = false;
        FlashData<Saved>::save(&saved, "cardkb");
    }
}

int Inputs::CardKbInput::onNotify(const InputEvent *event)
{
    InkHUD::InkHUD *hud = InkHUD::InkHUD::getInstance();

    // This key only brings up the training screen
    if (startTrainingIfNeeded())
        return 0;

    bool typing = false;
    for (InkHUD::SystemApplet *applet : hud->systemApplets)
        typing |= applet->handleFreeText;

    switch (event->inputEvent) {
    case INPUT_BROKER_UP:
        navigate(&InkHUD::InkHUD::navUp);
        break;
    case INPUT_BROKER_DOWN:
        navigate(&InkHUD::InkHUD::navDown);
        break;
    case INPUT_BROKER_LEFT:
        navigate(&InkHUD::InkHUD::navLeft);
        break;
    case INPUT_BROKER_RIGHT:
        navigate(&InkHUD::InkHUD::navRight);
        break;
    case INPUT_BROKER_SELECT:
        if (typing)
            hud->freeTextDone();
        else
            hud->shortpress(); // Same as a joystick center click
        break;
    case INPUT_BROKER_BACK:
    case INPUT_BROKER_CANCEL:
        if (keyboardTrainingActive() || joystickTrainingActive()) { // No long press on a keyboard: this skips the training
            hud->exitLong();
        } else if (typing) {
            if (event->inputEvent == INPUT_BROKER_BACK)
                hud->freeText('\b');
            else
                hud->freeTextCancel();
        } else
            hud->exitShort();
        break;
    case INPUT_BROKER_GPS_TOGGLE: // Fn+G
    case INPUT_BROKER_SEND_PING:  // Fn+Space
    case INPUT_BROKER_SHUTDOWN:   // Fn+S
        systemCommands.handleInputEvent(event);
        break;
    case INPUT_BROKER_ANYKEY:
        // Fn+M mute, Fn+R reboot, Fn+B Bluetooth: same shortcuts as BaseUI
        if (event->kbchar == INPUT_BROKER_MSG_MUTE_TOGGLE || event->kbchar == INPUT_BROKER_MSG_REBOOT ||
            event->kbchar == INPUT_BROKER_MSG_BLUETOOTH_TOGGLE)
            systemCommands.handleInputEvent(event);
        else if (typing && event->kbchar >= 0x20 && event->kbchar < 0x7F)
            hud->freeText(event->kbchar);
        break;
    default:
        break;
    }
    return 0;
}

#endif
