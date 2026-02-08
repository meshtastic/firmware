#include "modules/SingleButtonInputManager.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/MorseInputModule.h"
#include "modules/SpecialCharacterInputModule.h"
#include "modules/GridKeyboardInputModule.h"
#include "graphics/Screen.h"
#include "mesh/NodeDB.h"
#include <Arduino.h>

extern graphics::Screen *screen;
extern meshtastic_DeviceUIConfig uiconfig;

namespace graphics
{

SingleButtonInputManager &SingleButtonInputManager::instance()
{
    static SingleButtonInputManager inst;
    return inst;
}

SingleButtonInputManager::SingleButtonInputManager()
{
    loadPreference();
}

void SingleButtonInputManager::start(const char *header, const char *initialText, uint32_t durationMs,
                                   std::function<void(const std::string &)> callback)
{
    // Stop any currently active module first
    stop(false);
    
    // Start the appropriate module based on current mode
    if (currentMode == MODE_MORSE) {
        MorseInputModule::instance().start(header, initialText, durationMs, callback);
    } else if (currentMode == MODE_GRID_KEYBOARD) {
        GridKeyboardInputModule::instance().start(header, initialText, durationMs, callback);
    } else {
        SpecialCharacterInputModule::instance().start(header, initialText, durationMs, callback);
    }
}

void SingleButtonInputManager::stop(bool callEmptyCallback)
{
    // Stop whichever module might be active
    if (MorseInputModule::instance().isActive()) {
        MorseInputModule::instance().stop(callEmptyCallback);
    }
    if (SpecialCharacterInputModule::instance().isActive()) {
        SpecialCharacterInputModule::instance().stop(callEmptyCallback);
    }
    if (GridKeyboardInputModule::instance().isActive()) {
        GridKeyboardInputModule::instance().stop(callEmptyCallback);
    }
}

void SingleButtonInputManager::setMode(InputMode mode)
{
    if (currentMode != mode) {
        currentMode = mode;
        savePreference();
    }
}

void SingleButtonInputManager::toggleMode()
{
    if (currentMode == MODE_MORSE) {
        setMode(MODE_GRID_KEYBOARD);
    } else if (currentMode == MODE_GRID_KEYBOARD) {
        setMode(MODE_SPECIAL_CHARACTERS);
    } else {
        setMode(MODE_MORSE);
    }
}

SingleButtonInputBase *SingleButtonInputManager::getActiveModule()
{
    if (MorseInputModule::instance().isActive()) {
        return &MorseInputModule::instance();
    }
    if (SpecialCharacterInputModule::instance().isActive()) {
        return &SpecialCharacterInputModule::instance();
    }
    if (GridKeyboardInputModule::instance().isActive()) {
        return &GridKeyboardInputModule::instance();
    }
    return nullptr;
}

bool SingleButtonInputManager::isActive() const
{
    return MorseInputModule::instance().isActive() || 
           SpecialCharacterInputModule::instance().isActive() ||
           GridKeyboardInputModule::instance().isActive();
}

void SingleButtonInputManager::loadPreference()
{
    // Default to Morse mode
    // TODO: Add single_button_input_mode field to meshtastic_DeviceUIConfig protobuf to persist this setting
    currentMode = MODE_MORSE;
}

void SingleButtonInputManager::savePreference()
{
    // Mode persistence not yet implemented
    // TODO: Add single_button_input_mode field to meshtastic_DeviceUIConfig protobuf to persist this setting
}

} // namespace graphics

#endif
