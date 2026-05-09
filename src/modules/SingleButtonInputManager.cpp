#include "modules/SingleButtonInputManager.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "input/SingleButtonGridKeyboard.h"
#include "input/SingleButtonMorse.h"
#include "input/SingleButtonSpecialCharacter.h"
#include "graphics/Screen.h"
#include "mesh/NodeDB.h"
#include "FSCommon.h"
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
        SingleButtonMorse::instance().start(header, initialText, durationMs, callback);
    } else if (currentMode == MODE_GRID_KEYBOARD) {
        SingleButtonGridKeyboard::instance().start(header, initialText, durationMs, callback);
    } else {
        SingleButtonSpecialCharacter::instance().start(header, initialText, durationMs, callback);
    }
}

void SingleButtonInputManager::stop(bool callEmptyCallback)
{
    // Stop whichever module might be active
    if (SingleButtonMorse::instance().isActive()) {
        SingleButtonMorse::instance().stop(callEmptyCallback);
    }
    if (SingleButtonSpecialCharacter::instance().isActive()) {
        SingleButtonSpecialCharacter::instance().stop(callEmptyCallback);
    }
    if (SingleButtonGridKeyboard::instance().isActive()) {
        SingleButtonGridKeyboard::instance().stop(callEmptyCallback);
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
    if (SingleButtonMorse::instance().isActive()) {
        return &SingleButtonMorse::instance();
    }
    if (SingleButtonSpecialCharacter::instance().isActive()) {
        return &SingleButtonSpecialCharacter::instance();
    }
    if (SingleButtonGridKeyboard::instance().isActive()) {
        return &SingleButtonGridKeyboard::instance();
    }
    return nullptr;
}

bool SingleButtonInputManager::isActive() const
{
        return SingleButtonMorse::instance().isActive() || SingleButtonSpecialCharacter::instance().isActive() ||
            SingleButtonGridKeyboard::instance().isActive();
}

void SingleButtonInputManager::loadPreference()
{
    // Load from file, default to Grid Keyboard mode
    currentMode = MODE_GRID_KEYBOARD;
    
    if (FSBegin()) {
        if (FSCom.exists("/prefs/single_button_mode")) {
            File file = FSCom.open("/prefs/single_button_mode", FILE_O_READ);
            if (file) {
                int savedMode = file.read();
                file.close();
                
                // Validate the mode value
                if (savedMode >= MODE_MORSE && savedMode <= MODE_SPECIAL_CHARACTERS) {
                    currentMode = static_cast<InputMode>(savedMode);
                    LOG_DEBUG("Loaded single button input mode: %d", savedMode);
                }
            }
        }
    }
}

void SingleButtonInputManager::savePreference()
{
    // Save mode preference to file
    if (FSBegin()) {
        FSCom.mkdir("/prefs");
        File file = FSCom.open("/prefs/single_button_mode", FILE_O_WRITE);
        if (file) {
            file.write(static_cast<uint8_t>(currentMode));
            file.close();
            LOG_DEBUG("Saved single button input mode: %d", currentMode);
        } else {
            LOG_ERROR("Failed to save single button input mode");
        }
    }
}

} // namespace graphics

#endif
