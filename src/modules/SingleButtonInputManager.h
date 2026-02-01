#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"
#include <functional>
#include <string>

namespace graphics
{

/**
 * @brief Manager for single-button input methods
 * 
 * Handles switching between input modes and persists user preference.
 */
class SingleButtonInputManager
{
  public:
    enum InputMode {
        MODE_MORSE = 0,
        MODE_GRID_KEYBOARD = 1,
        MODE_SPECIAL_CHARACTERS = 2
    };

    static SingleButtonInputManager &instance();

    /**
     * @brief Start text input with the currently selected mode
     * @param header Header text to display
     * @param initialText Initial text to populate the input field
     * @param durationMs Timeout duration (0 = no timeout)
     * @param callback Callback function to call with the final text
     */
    void start(const char *header, const char *initialText, uint32_t durationMs,
               std::function<void(const std::string &)> callback);
    
    /**
     * @brief Stop the current input module
     * @param callEmptyCallback If true, calls callback with empty string
     */
    void stop(bool callEmptyCallback = false);

    /**
     * @brief Get the current input mode
     * @return The current input mode
     */
    InputMode getCurrentMode() const { return currentMode; }
    
    /**
     * @brief Set the input mode and persist the preference
     * @param mode The input mode to set
     */
    void setMode(InputMode mode);
    
    /**
     * @brief Toggle between input modes and persist the preference
     */
    void toggleMode();
    
    /**
     * @brief Get the currently active input module
     * @return Pointer to the active module, or nullptr if none active
     */
    SingleButtonInputBase *getActiveModule();
    
    /**
     * @brief Check if an input module is currently active
     * @return true if active
     */
    bool isActive() const;

  private:
    SingleButtonInputManager();
    ~SingleButtonInputManager() = default;

    InputMode currentMode = MODE_MORSE;
    
    /**
     * @brief Load the saved input mode preference
     */
    void loadPreference();
    
    /**
     * @brief Save the current input mode preference
     */
    void savePreference();
};

} // namespace graphics

#endif
