#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "graphics/Screen.h"
#include "input/InputBroker.h"
#include "concurrency/OSThread.h"
#include <functional>
#include <string>

namespace graphics
{

/**
 * @brief Base class for single-button text input methods
 * 
 * This provides common functionality for input methods that use
 * only a single button for operation.
 */
class SingleButtonInputBase : public concurrency::OSThread, public Observable<const UIFrameEvent *>
{
  public:
    virtual ~SingleButtonInputBase() = default;

    /**
     * @brief Start the input module
     * @param header Header text to display
     * @param initialText Initial text to populate the input field
     * @param durationMs Timeout duration (0 = no timeout)
     * @param callback Callback function to call with the final text
     */
    virtual void start(const char *header, const char *initialText, uint32_t durationMs,
                      std::function<void(const std::string &)> callback);
    
    /**
     * @brief Stop the input module
     * @param callEmptyCallback If true, calls callback with empty string
     */
    virtual void stop(bool callEmptyCallback = false);

    /**
     * @brief Handle input events
     * @param event The input event to handle
     * @return true if the event was consumed
     */
    virtual bool handleInput(const InputEvent &event);
    
    /**
     * @brief Draw the input interface
     * @param display The display to draw on
     * @param state The UI state
     * @param x X offset
     * @param y Y offset
     */
    virtual void draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) = 0;

    /**
     * @brief Check if the module is currently active
     * @return true if active
     */
    bool isActive() const { return active; }

  protected:
    SingleButtonInputBase(const char *threadName);

    int32_t runOnce() override;

    // Common state
    bool active = false;
    std::string headerText;
    std::string inputText;
    std::function<void(const std::string &)> callback;
    
    // Button state
    uint32_t buttonPressTime = 0;
    bool buttonPressed = false;
    bool ignoreRelease = false;
    bool waitForRelease = false;
    
    // Menu state
    bool menuOpen = false;
    int menuSelection = 0;
    bool inputModeMenuOpen = false;
    
    // Auto-shift state (shared by input modules)
    bool shift = false;
    bool autoShift = true;
    
    /**
     * @brief Handle button press event
     * @param now Current time in milliseconds
     */
    virtual void handleButtonPress(uint32_t now);
    
    /**
     * @brief Handle button release event
     * @param now Current time in milliseconds
     * @param duration Duration of the button press in milliseconds
     */
    virtual void handleButtonRelease(uint32_t now, uint32_t duration);
    
    /**
     * @brief Handle button held event (called continuously while button is held)
     * @param now Current time in milliseconds
     * @param duration Duration the button has been held in milliseconds
     */
    virtual void handleButtonHeld(uint32_t now, uint32_t duration);
    
    /**
     * @brief Handle idle state (no button press)
     * @param now Current time in milliseconds
     */
    virtual void handleIdle(uint32_t now);
    
    /**
     * @brief Get the menu items for this input method
     * @param itemCount Output parameter for the number of items
     * @return Array of menu item strings
     */
    virtual const char **getMenuItems(int &itemCount);
    
    /**
     * @brief Get mode-specific menu items (override if needed)
     * @param itemCount Output parameter for the number of items
     * @return Array of menu item strings, or nullptr to use default
     */
    virtual const char **getModeSpecificMenuItems(int &itemCount) { itemCount = 0; return nullptr; }
    
    /**
     * @brief Handle menu selection
     * @param selection The selected menu item index
     */
    virtual void handleMenuSelection(int selection);
    
    /**
     * @brief Handle mode switching from input mode submenu
     * @param modeIndex The mode index (0=Morse, 1=Scan, 2=SpecialCharacters)
     */
    virtual void handleModeSwitch(int modeIndex);
    
    /**
     * @brief Draw the main input interface (not menu)
     * @param display The display to draw on
     * @param x X offset
     * @param y Y offset
     */
    virtual void drawInterface(OLEDDisplay *display, int16_t x, int16_t y) = 0;
    
    /**
     * @brief Draw the menu
     * @param display The display to draw on
     * @param x X offset
     * @param y Y offset
     */
    void drawMenu(OLEDDisplay *display, int16_t x, int16_t y);
    
    /**
     * @brief Get input text with blinking cursor appended
     * @return Display string with cursor if appropriate
     */
    std::string getDisplayTextWithCursor() const;
    
    /**
     * @brief Format display text with scrolling (truncation) if too long
     * @param display The display to measure text width
     * @param text The text to format
     * @return Formatted text with "..." prefix if truncated
     */
    std::string formatDisplayTextWithScrolling(OLEDDisplay *display, const std::string &text) const;
};

} // namespace graphics

#endif
