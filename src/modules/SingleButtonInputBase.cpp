#include "modules/SingleButtonInputBase.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputManager.h"
#include "graphics/SharedUIDisplay.h"
#include "input/ButtonThread.h"
#include <Arduino.h>

extern ButtonThread *UserButtonThread;
extern graphics::Screen *screen;

namespace graphics
{

SingleButtonInputBase::SingleButtonInputBase(const char *threadName) : concurrency::OSThread(threadName) {}

void SingleButtonInputBase::start(const char *header, const char *initialText, uint32_t durationMs,
                                std::function<void(const std::string &)> cb)
{
    active = true;
    headerText = header ? header : "Input";
    inputText = initialText ? initialText : "";
    callback = cb;
    buttonPressed = false;
    ignoreRelease = false;
    menuOpen = false;
    menuSelection = 0;
    waitForRelease = true;
    
    // Initialize auto-shift state
    autoShift = true;
    if (inputText.empty()) {
        shift = true;
    } else {
        char last = inputText.back();
        if (last == '.' || last == '!' || last == '?') {
            shift = true;
        } else {
            shift = false;
        }
    }
    
    setIntervalFromNow(20);
}

void SingleButtonInputBase::stop(bool callEmptyCallback)
{
    active = false;
    if (callEmptyCallback && callback) {
        callback("");
    }
    callback = nullptr;
}

bool SingleButtonInputBase::handleInput(const InputEvent &event)
{
    if (!active)
        return false;

    // Consume button events to prevent other handlers from seeing them
    if (event.inputEvent == INPUT_BROKER_USER_PRESS || event.inputEvent == INPUT_BROKER_SELECT ||
        event.inputEvent == INPUT_BROKER_SELECT_LONG) {
        return true;
    }

    return false;
}

int32_t SingleButtonInputBase::runOnce()
{
    if (!active)
        return 100;

    if (!UserButtonThread)
        return 100;

    bool pressed = UserButtonThread->isHeld();

    if (waitForRelease) {
        if (!pressed) {
            waitForRelease = false;
        }
        return 20;
    }

    uint32_t now = millis();

    if (pressed && !buttonPressed) {
        // Press started
        buttonPressed = true;
        buttonPressTime = now;
        if (screen)
            screen->onPress();
        handleButtonPress(now);
    } else if (!pressed && buttonPressed) {
        // Press released
        buttonPressed = false;
        if (screen)
            screen->onPress();
        if (!ignoreRelease) {
            uint32_t duration = now - buttonPressTime;
            handleButtonRelease(now, duration);
        }
        ignoreRelease = false;
    } else if (pressed && buttonPressed) {
        // Button is being held
        uint32_t duration = now - buttonPressTime;
        handleButtonHeld(now, duration);
    } else {
        // Idle
        handleIdle(now);
    }

    return 20;
}

void SingleButtonInputBase::handleButtonPress(uint32_t now)
{
    // Default implementation - subclasses can override
}

void SingleButtonInputBase::handleButtonRelease(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        // Menu navigation - Short press cycles through items
        int itemCount = 0;
        getMenuItems(itemCount);
        menuSelection = (menuSelection + 1) % itemCount;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        return;
    }
    
    // Subclasses handle their specific input logic
}

void SingleButtonInputBase::handleButtonHeld(uint32_t now, uint32_t duration)
{
    if (menuOpen) {
        if (duration > 500) { // Long press -> Select
            handleMenuSelection(menuSelection);
            ignoreRelease = true;
            waitForRelease = true;

            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            return;
        }
    } else if (duration > 1500) {
        // Open menu
        menuOpen = true;
        menuSelection = 0;
        ignoreRelease = true;
        waitForRelease = true;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
    }
}

void SingleButtonInputBase::handleIdle(uint32_t now)
{
    // Default implementation - subclasses can override
}

void SingleButtonInputBase::handleModeSwitch(int modeIndex)
{
    // Save current state
    std::string savedText = inputText;
    auto savedCallback = callback;
    std::string savedHeader = headerText;
    
    // Stop this module without calling callback
    stop(false);
    
    // Switch mode based on index
    if (modeIndex == 0) {
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_MORSE);
    } else if (modeIndex == 1) {
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_GRID_KEYBOARD);
    } else if (modeIndex == 2) {
        SingleButtonInputManager::instance().setMode(SingleButtonInputManager::MODE_SPECIAL_CHARACTERS);
    }
    
    // Start the new module with saved state
    SingleButtonInputManager::instance().start(savedHeader.c_str(), savedText.c_str(), 0, savedCallback);
}

void SingleButtonInputBase::handleMenuSelection(int selection)
{
    // Handle input mode submenu
    if (inputModeMenuOpen) {
        switch (selection) {
        case 0: // Back
            inputModeMenuOpen = false;
            menuSelection = 0;
            break;
            
        case 1: // Morse Code
            inputModeMenuOpen = false;
            menuOpen = false;
            handleModeSwitch(0); // Pass 0 for Morse
            break;
            
        case 2: // Grid Keyboard
            inputModeMenuOpen = false;
            menuOpen = false;
            handleModeSwitch(1); // Pass 1 for Grid Keyboard
            break;
            
        case 3: // Special Characters
            inputModeMenuOpen = false;
            menuOpen = false;
            handleModeSwitch(2); // Pass 2 for Special Characters
            break;
        }
        return;
    }
    
    // Main menu
    switch (selection) {
    case 0: // Back
        menuOpen = false;
        break;
        
    case 1: // Input Mode submenu
        inputModeMenuOpen = true;
        menuSelection = 0;
        break;

    case 2: // Backspace
        if (!inputText.empty()) {
            inputText.pop_back();
        }
        menuOpen = false;
        break;
        
    case 3: // Remove Word
        {
            // Find the last space and remove everything after it
            size_t lastSpace = inputText.find_last_of(' ');
            if (lastSpace != std::string::npos) {
                inputText = inputText.substr(0, lastSpace);
            } else {
                // No space found, clear entire text
                inputText.clear();
            }
            menuOpen = false;
        }
        break;
        
    case 4: // Shift
        shift = !shift;
        menuOpen = false;
        break;
        
    case 5: // Send
        if (callback)
            callback(inputText);
        stop();
        break;
        
    case 6: // Exit
        stop(true);
        break;
    }
}

void SingleButtonInputBase::drawMenu(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    display->drawString(x, y, "Input Menu");
    display->drawLine(x, y + 12, x + display->getWidth(), y + 12);

    int itemCount = 0;
    const char **items = getMenuItems(itemCount);

    // Calculate visible items based on screen height
    int itemHeight = 12;
    int headerHeight = 14;
    int availableHeight = display->getHeight() - y - headerHeight;
    int visibleItems = availableHeight / itemHeight;
    if (visibleItems < 1)
        visibleItems = 1;

    // Scroll logic
    int startItem = 0;
    if (menuSelection >= visibleItems) {
        startItem = menuSelection - visibleItems + 1;
    }
    int endItem = startItem + visibleItems;
    if (endItem > itemCount)
        endItem = itemCount;

    int currentY = y + headerHeight;

    for (int i = startItem; i < endItem; ++i) {
        if (i == menuSelection) {
            display->fillRect(x, currentY, display->getWidth(), itemHeight);
            display->setColor(BLACK);
            display->drawString(x + 2, currentY, items[i]);
            display->setColor(WHITE);
        } else {
            display->drawString(x + 2, currentY, items[i]);
        }
        currentY += itemHeight;
    }

    // Draw scrollbar if needed
    if (itemCount > visibleItems) {
        int scrollBarHeight = availableHeight;
        int scrollBarWidth = 4;
        int scrollBarX = display->getWidth() - scrollBarWidth;
        int scrollBarY = y + headerHeight;

        int indicatorHeight = scrollBarHeight * visibleItems / itemCount;
        int indicatorY = scrollBarY + (scrollBarHeight - indicatorHeight) * startItem / (itemCount - visibleItems);

        display->drawRect(scrollBarX, scrollBarY, scrollBarWidth, scrollBarHeight);
        display->fillRect(scrollBarX + 1, indicatorY, scrollBarWidth - 2, indicatorHeight);
    }
}

void SingleButtonInputBase::draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (!active)
        return;

    if (menuOpen) {
        drawMenu(display, x, y);
        return;
    }

    drawInterface(display, x, y);
}

const char **SingleButtonInputBase::getMenuItems(int &itemCount)
{
    // Input mode submenu
    if (inputModeMenuOpen) {
        static const char *modeItems[] = {
            "Back",
            "Morse Code",
            "Grid Keyboard",
            "Special Characters"
        };
        itemCount = 4;
        return modeItems;
    }
    
    // Main menu
    static char shiftItem[32];
    snprintf(shiftItem, sizeof(shiftItem), "Shift: %s", shift ? "ON" : "OFF");
    
    static const char *items[] = {
        "Back To Input",
        "Input Mode",
        "Backspace",
        "Remove Word",
        shiftItem,
        "Send",
        "Exit"
    };
    itemCount = 7;
    return items;
}

std::string SingleButtonInputBase::getDisplayTextWithCursor() const
{
    std::string displayText = inputText;
    // Blinking cursor (500ms on/off cycle)
    if ((millis() / 500) % 2 == 0) {
        displayText += "_";
    }
    return displayText;
}

std::string SingleButtonInputBase::formatDisplayTextWithScrolling(OLEDDisplay *display, const std::string &text) const
{
    int width = display->getStringWidth(text.c_str());
    int maxWidth = display->getWidth();
    
    if (width > maxWidth) {
        int charWidth = 6;  // Approximate character width
        int maxChars = maxWidth / charWidth;
        if (text.length() > (size_t)maxChars) {
            // Truncate with "..." prefix, showing the end of the text
            return "..." + text.substr(text.length() - maxChars + 3);
        }
    }
    
    return text;
}

} // namespace graphics

#endif
