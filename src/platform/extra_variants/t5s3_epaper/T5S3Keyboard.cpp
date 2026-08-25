#include "configuration.h"

#if defined(T5S3_EPD_TOUCH_KEYBOARD) && !defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)

#include "T5S3Keyboard.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "mesh/Throttle.h"
#include "UptimeClock.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace graphics
{

void T5S3Keyboard::start(const char *headerText, const char *initialText, uint32_t durationMs, Callback cb,
                         const char *buttonLabel)
{
    active = false;
    callback = nullptr;
    memset(text, 0, sizeof(text));
    memset(header, 0, sizeof(header));
    memset(submitLabel, 0, sizeof(submitLabel));
    t5_kb_action_queue_init(&pendingActions);
    pressedUntilMs = 0;

    if (headerText)
        strncpy(header, headerText, sizeof(header) - 1);
    strncpy(submitLabel, buttonLabel ? buttonLabel : "Send", sizeof(submitLabel) - 1);

    t5_kb_init(&state, text, sizeof(text), sizeof(text) - 1, nullptr, 0);
    t5_kb_set_text(&state, initialText ? initialText : "");
    callback = cb;
    timeoutMs = durationMs;
    lastActivityMs = Time::getMillis();
    active = true;
}

void T5S3Keyboard::stop(bool callEmptyCallback)
{
    Callback cb = callback;
    callback = nullptr;
    active = false;
    t5_kb_action_queue_init(&pendingActions);
    pressedUntilMs = 0;
    t5_kb_clear_pressed(&state);
    if (callEmptyCallback && cb)
        cb("");
}

bool T5S3Keyboard::isTimedOut() const
{
    return active && timeoutMs != 0 && Throttle::hasElapsed(lastActivityMs, timeoutMs);
}

void T5S3Keyboard::finish(bool submitted)
{
    Callback cb = callback;
    std::string result = submitted ? std::string(t5_kb_text(&state)) : std::string();
    callback = nullptr;
    active = false;
    t5_kb_action_queue_init(&pendingActions);
    pressedUntilMs = 0;
    t5_kb_clear_pressed(&state);
    if (cb)
        cb(result);
}

T5KeyboardResult T5S3Keyboard::activateKey(uint16_t keyId)
{
    const T5KeyboardResult result = t5_kb_press_key(&state, keyId);
    if (result == T5_KB_RESULT_SUBMIT) {
        finish(true);
    } else if (result == T5_KB_RESULT_CANCEL) {
        finish(false);
    } else if (result != T5_KB_RESULT_IGNORED) {
        lastActivityMs = Time::getMillis();
    }
    return result;
}

void T5S3Keyboard::enqueueKey(uint16_t keyId)
{
    if (!active)
        return;

    lastActivityMs = Time::getMillis();
    pressedUntilMs = lastActivityMs + PRESSED_FEEDBACK_MS;

    // Drain one old action if the queue is full. This keeps the input path
    // non-blocking without dropping a typed character.
    if (!t5_kb_action_queue_push(&pendingActions, keyId)) {
        uint16_t oldest = 0;
        if (t5_kb_action_queue_pop(&pendingActions, &oldest))
            activateKey(oldest);
        if (active)
            t5_kb_action_queue_push(&pendingActions, keyId);
    }
}

void T5S3Keyboard::drainActions()
{
    uint16_t keyId = 0;
    while (active && t5_kb_action_queue_pop(&pendingActions, &keyId))
        activateKey(keyId);

    if (pressedUntilMs != 0 && Throttle::deadlinePassed(pressedUntilMs)) {
        pressedUntilMs = 0;
        t5_kb_clear_pressed(&state);
    }
}

bool T5S3Keyboard::activateCharacter(char character)
{
    const uint16_t count = t5_kb_get_key_count(t5_kb_mode(&state));
    T5KeyboardKey key{};
    for (uint16_t index = 0; index < count; index++) {
        if (!t5_kb_get_key(t5_kb_mode(&state), index, &key))
            continue;
        if (key.action == T5_KB_ACTION_CHARACTER && key.character == character) {
            enqueueKey(key.id);
            return true;
        }
    }
    return false;
}

bool T5S3Keyboard::handleInput(const InputEvent &event)
{
    if (!active)
        return false;

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1) || defined(MESHTASTIC_T5S3_EPAPER_V2_UI)
    if (event.touchTargetKind == static_cast<uint8_t>(meshtastic::TouchTargetKind::KeyboardKey)) {
        enqueueKey(static_cast<uint16_t>(event.touchTargetValue));
        return true;
    }
#endif

    // A touch that leaves a registered key is consumed rather than becoming a cursor move or page swipe.
    if (event.touchX != 0 || event.touchY != 0)
        return true;

    switch (event.inputEvent) {
    case INPUT_BROKER_CANCEL:
    case INPUT_BROKER_ALT_LONG:
        enqueueKey(T5_KB_KEY_CANCEL);
        return true;
    case INPUT_BROKER_BACK:
        enqueueKey(t5_kb_length(&state) > 0 ? T5_KB_KEY_BACKSPACE : T5_KB_KEY_CANCEL);
        return true;
    case INPUT_BROKER_SELECT:
        enqueueKey(T5_KB_KEY_SUBMIT);
        return true;
    case INPUT_BROKER_LEFT:
    case INPUT_BROKER_ALT_PRESS:
        enqueueKey(T5_KB_KEY_LEFT);
        return true;
    case INPUT_BROKER_RIGHT:
    case INPUT_BROKER_USER_PRESS:
        enqueueKey(T5_KB_KEY_RIGHT);
        return true;
    default:
        break;
    }

    if (event.kbchar >= 32 && event.kbchar <= 126) {
        activateCharacter(static_cast<char>(event.kbchar));
        return true;
    }
    return false;
}

void T5S3Keyboard::drawLabelCentered(OLEDDisplay *display, const char *label, int16_t x, int16_t y, int16_t width,
                                     int16_t height)
{
    if (!label)
        return;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const int16_t labelY = static_cast<int16_t>(y + (height - FONT_HEIGHT_SMALL) / 2);
    display->drawString(x + width / 2, labelY, label);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void T5S3Keyboard::drawKey(OLEDDisplay *display, const T5KeyboardKey &key, int16_t x, int16_t y, int16_t width,
                           int16_t height)
{
    const bool pressed = t5_kb_pressed_key(&state) == key.id;
    display->setColor(pressed ? WHITE : BLACK);
    display->fillRect(x, y, width, height);
    display->setColor(pressed ? BLACK : WHITE);
    display->drawRect(x, y, width, height);
    drawLabelCentered(display, key.label, x, y, width, height);
    display->setColor(BLACK);
}

void T5S3Keyboard::drawInput(OLEDDisplay *display)
{
    const int16_t boxX = SCREEN_MARGIN;
    const int16_t boxWidth = static_cast<int16_t>(display->getWidth() - SCREEN_MARGIN * 2);
    const int16_t textX = boxX + 10;
    const int16_t textWidth = boxWidth - 20;
    const int16_t lineStep = FONT_HEIGHT_SMALL + 4;
    const int16_t maxLines = std::max<int16_t>(1, (INPUT_HEIGHT - 20) / lineStep);
    const int16_t charWidth = std::max<int16_t>(1, display->getStringWidth("M"));
    const uint16_t charsPerLine = std::max<uint16_t>(1, static_cast<uint16_t>(textWidth / charWidth));
    const uint16_t length = t5_kb_length(&state);
    const uint16_t cursor = t5_kb_cursor(&state);
    const uint16_t cursorLine = static_cast<uint16_t>(cursor / charsPerLine);
    const uint16_t firstLine = cursorLine >= static_cast<uint16_t>(maxLines - 1) ? cursorLine - (maxLines - 1) : 0;
    const char *value = t5_kb_text(&state);

    display->setFont(FONT_SMALL);
    display->setColor(WHITE);
    display->drawRect(boxX, INPUT_TOP, boxWidth, INPUT_HEIGHT);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    for (int16_t line = 0; line < maxLines; line++) {
        const uint16_t lineIndex = static_cast<uint16_t>(firstLine + line);
        const uint16_t start = static_cast<uint16_t>(lineIndex * charsPerLine);
        if (start > length)
            break;
        const uint16_t count = std::min<uint16_t>(charsPerLine, static_cast<uint16_t>(length - start));
        char lineBuffer[TEXT_CAPACITY] = {};
        if (count > 0)
            memcpy(lineBuffer, value + start, count);
        lineBuffer[count] = '\0';
        display->drawString(textX, INPUT_TOP + 8 + line * lineStep, lineBuffer);

        if (cursor >= start && cursor <= start + count) {
            char prefix[TEXT_CAPACITY] = {};
            const uint16_t prefixLength = static_cast<uint16_t>(cursor - start);
            if (prefixLength > 0)
                memcpy(prefix, value + start, prefixLength);
            prefix[prefixLength] = '\0';
            int16_t cursorX = static_cast<int16_t>(textX + display->getStringWidth(prefix));
            if (cursorX > boxX + boxWidth - 8)
                cursorX = boxX + boxWidth - 8;
            display->drawVerticalLine(cursorX, INPUT_TOP + 7 + line * lineStep, FONT_HEIGHT_SMALL + 2);
        }
    }

    char countLabel[32] = {};
    snprintf(countLabel, sizeof(countLabel), "%u/%u", static_cast<unsigned>(length),
             static_cast<unsigned>(state.max_length));
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(boxX + boxWidth - 10, INPUT_TOP + INPUT_HEIGHT - FONT_HEIGHT_SMALL - 6, countLabel);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void T5S3Keyboard::drawFooter(OLEDDisplay *display)
{
    const int16_t footerY = FOOTER_TOP;
    const int16_t footerWidth = static_cast<int16_t>(display->getWidth() - SCREEN_MARGIN * 2);
    const int16_t buttonGap = 12;
    const int16_t buttonWidth = static_cast<int16_t>((footerWidth - buttonGap) / 2);
    const uint16_t pressed = t5_kb_pressed_key(&state);

    display->setColor(WHITE);
    display->drawLine(SCREEN_MARGIN, footerY - 1, display->getWidth() - SCREEN_MARGIN, footerY - 1);

    const uint16_t ids[2] = {T5_KB_KEY_CANCEL, T5_KB_KEY_SUBMIT};
    const char *labels[2] = {"Cancel", submitLabel};
    for (uint8_t index = 0; index < 2; index++) {
        const int16_t x = static_cast<int16_t>(SCREEN_MARGIN + index * (buttonWidth + buttonGap));
        const bool selected = pressed == ids[index];
        display->setColor(selected ? WHITE : BLACK);
        display->fillRect(x, footerY + 12, buttonWidth, FOOTER_HEIGHT - 24);
        display->setColor(selected ? BLACK : WHITE);
        display->drawRect(x, footerY + 12, buttonWidth, FOOTER_HEIGHT - 24);
        drawLabelCentered(display, labels[index], x, footerY + 12, buttonWidth, FOOTER_HEIGHT - 24);

        if (screen) {
            screen->addTouchTarget({x, static_cast<int16_t>(footerY + 12), static_cast<int16_t>(x + buttonWidth),
                                    static_cast<int16_t>(footerY + FOOTER_HEIGHT - 12)},
                                   meshtastic::TouchTargetKind::KeyboardKey, ids[index], INPUT_BROKER_NONE);
        }
        display->setColor(BLACK);
    }
}

bool T5S3Keyboard::draw(OLEDDisplay *display)
{
    if (!active || display == nullptr)
        return false;

    drainActions();
    if (!active)
        return false;

    display->setColor(BLACK);
    display->fillRect(0, 0, display->getWidth(), display->getHeight());
    display->setColor(WHITE);
    display->setFont(FONT_MEDIUM);
    display->drawString(SCREEN_MARGIN, 8, header[0] ? header : "Text Input");
    drawInput(display);

    const uint16_t count = t5_kb_get_key_count(t5_kb_mode(&state));
    const int16_t keyboardWidth = static_cast<int16_t>(display->getWidth() - SCREEN_MARGIN * 2);
    for (uint8_t row = 0; row < 4; row++) {
        uint16_t rowCount = 0;
        uint16_t weightSum = 0;
        T5KeyboardKey key{};
        for (uint16_t index = 0; index < count; index++) {
            if (!t5_kb_get_key(t5_kb_mode(&state), index, &key) || key.row != row)
                continue;
            rowCount++;
            weightSum += key.width_weight;
        }

        if (rowCount == 0 || weightSum == 0)
            continue;

        const int16_t totalGap = static_cast<int16_t>((rowCount - 1) * KEY_GAP);
        const int16_t usableWidth = keyboardWidth - totalGap;
        int16_t x = SCREEN_MARGIN;
        uint16_t seen = 0;
        for (uint16_t index = 0; index < count; index++) {
            if (!t5_kb_get_key(t5_kb_mode(&state), index, &key) || key.row != row)
                continue;

            int16_t width = static_cast<int16_t>((usableWidth * key.width_weight) / weightSum);
            if (seen == rowCount - 1)
                width = static_cast<int16_t>(SCREEN_MARGIN + keyboardWidth - x);
            const int16_t y = static_cast<int16_t>(KEYBOARD_TOP + row * (KEYBOARD_HEIGHT / 4));
            const int16_t height = static_cast<int16_t>(KEYBOARD_HEIGHT / 4);
            drawKey(display, key, x, y, width, height);
            if (screen)
                screen->addTouchTarget({x, y, static_cast<int16_t>(x + width), static_cast<int16_t>(y + height)},
                                       meshtastic::TouchTargetKind::KeyboardKey, key.id, INPUT_BROKER_NONE);
            x = static_cast<int16_t>(x + width + KEY_GAP);
            seen++;
        }
    }

    drawFooter(display);
    if (screen)
        screen->markTouchFrameMapped();
    return true;
}

} // namespace graphics

#endif // T5S3_EPD_TOUCH_KEYBOARD && !MESHTASTIC_INCLUDE_NICHE_GRAPHICS
