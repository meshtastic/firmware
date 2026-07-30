#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./KeyboardApplet.h"

#include <cctype>

using namespace NicheGraphics;

namespace
{
bool usePortraitKeyboardSizing()
{
    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    return inkhud && inkhud->height() > inkhud->width();
}
} // namespace

InkHUD::KeyboardApplet::KeyboardApplet()
{
    for (uint8_t row = 0; row < LEGACY_KBD_ROWS; row++) {
        legacyRowWidths[row] = 0;
        for (uint8_t col = 0; col < KBD_COLS; col++)
            legacyRowWidths[row] += legacyKeyWidths[row * KBD_COLS + col];
    }

    mode = MODE_TEXT;
    lastTypingMode = MODE_TEXT;
    emotePage = 0;
    selectedKey = 0;
    prevSelectedKey = 0;
    normalizeSelection();
}

void InkHUD::KeyboardApplet::onRender(bool full)
{
    if (!useTouchKeyboard()) {
        renderLegacyKeyboard(full);
        return;
    }

    const bool showSelection = showSelectionHighlight();

    if (full) {
        for (uint8_t i = 0; i < KBD_KEY_COUNT; i++)
            drawKey(i, showSelection && i == selectedKey);
    } else if (showSelection && selectedKey != prevSelectedKey) {
        drawKey(prevSelectedKey, false);
        drawKey(selectedKey, true);
    }

    prevSelectedKey = selectedKey;
}

bool InkHUD::KeyboardApplet::useTouchKeyboard() const
{
    return inkhud->hasTouchEnabledProvider();
}

void InkHUD::KeyboardApplet::renderLegacyKeyboard(bool full)
{
    uint16_t em = fontSmall.lineHeight();
    uint16_t keyH = Y(1.0) / LEGACY_KBD_ROWS;
    int16_t keyTopPadding = (keyH - fontSmall.lineHeight()) / 2;

    if (full) {
        for (uint8_t row = 0; row < LEGACY_KBD_ROWS; row++) {
            int16_t keyXPadding = X(1.0) - ((legacyRowWidths[row] * em) >> 4);
            uint16_t xPos = 0;
            for (uint8_t col = 0; col < KBD_COLS; col++) {
                Color fgcolor = BLACK;
                uint8_t index = row * KBD_COLS + col;
                uint16_t keyX = ((xPos * em) >> 4) + ((col * keyXPadding) / (KBD_COLS - 1));
                uint16_t keyY = row * keyH;
                uint16_t keyW = (legacyKeyWidths[index] * em) >> 4;
                if (index == selectedKey) {
                    fgcolor = WHITE;
                    fillRect(keyX, keyY, keyW, keyH, BLACK);
                }
                drawLegacyKeyLabel(keyX, keyY + keyTopPadding, keyW, legacyKeys[index], fgcolor);
                xPos += legacyKeyWidths[index];
            }
        }
    } else if (selectedKey != prevSelectedKey) {
        uint8_t row = prevSelectedKey / KBD_COLS;
        int16_t keyXPadding = X(1.0) - ((legacyRowWidths[row] * em) >> 4);
        uint16_t xPos = 0;
        for (uint8_t i = prevSelectedKey - (prevSelectedKey % KBD_COLS); i < prevSelectedKey; i++)
            xPos += legacyKeyWidths[i];
        uint16_t keyX = ((xPos * em) >> 4) + (((prevSelectedKey % KBD_COLS) * keyXPadding) / (KBD_COLS - 1));
        uint16_t keyY = row * keyH;
        uint16_t keyW = (legacyKeyWidths[prevSelectedKey] * em) >> 4;
        fillRect(keyX, keyY, keyW, keyH, WHITE);
        drawLegacyKeyLabel(keyX, keyY + keyTopPadding, keyW, legacyKeys[prevSelectedKey], BLACK);

        row = selectedKey / KBD_COLS;
        keyXPadding = X(1.0) - ((legacyRowWidths[row] * em) >> 4);
        xPos = 0;
        for (uint8_t i = selectedKey - (selectedKey % KBD_COLS); i < selectedKey; i++)
            xPos += legacyKeyWidths[i];
        keyX = ((xPos * em) >> 4) + (((selectedKey % KBD_COLS) * keyXPadding) / (KBD_COLS - 1));
        keyY = row * keyH;
        keyW = (legacyKeyWidths[selectedKey] * em) >> 4;
        fillRect(keyX, keyY, keyW, keyH, BLACK);
        drawLegacyKeyLabel(keyX, keyY + keyTopPadding, keyW, legacyKeys[selectedKey], WHITE);
    }

    prevSelectedKey = selectedKey;
}

void InkHUD::KeyboardApplet::drawLegacyKeyLabel(uint16_t left, uint16_t top, uint16_t width, char key, Color color)
{
    if (key == '\b') {
        const uint8_t bsBitmap[] = {0x0f, 0xf8, 0x18, 0x08, 0x32, 0x28, 0x61, 0x48, 0xc0,
                                    0x88, 0x61, 0x48, 0x32, 0x28, 0x18, 0x08, 0x0f, 0xf8};
        uint16_t leftPadding = (width - 13) >> 1;
        drawBitmap(left + leftPadding, top + 1, bsBitmap, 13, 9, color);
    } else if (key == '\n') {
        const uint8_t doneBitmap[] = {0x00, 0x30, 0x00, 0x60, 0x00, 0xc0, 0x01, 0x80, 0x03,
                                      0x00, 0xc6, 0x00, 0x6c, 0x00, 0x38, 0x00, 0x10, 0x00};
        uint16_t leftPadding = (width - 12) >> 1;
        drawBitmap(left + leftPadding, top + 1, doneBitmap, 12, 9, color);
    } else if (key == ' ') {
        const uint8_t spaceBitmap[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
                                       0x08, 0x80, 0x08, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00};
        uint16_t leftPadding = (width - 13) >> 1;
        drawBitmap(left + leftPadding, top + 1, spaceBitmap, 13, 9, color);
    } else if (key == '\x1b') {
        setTextColor(color);
        std::string keyText = "ESC";
        uint16_t leftPadding = (width - getTextWidth(keyText)) >> 1;
        printAt(left + leftPadding, top, keyText);
    } else {
        setTextColor(color);
        if (key >= 0x61)
            key -= 32;
        std::string keyText = std::string(1, key);
        uint16_t leftPadding = (width - getTextWidth(keyText)) >> 1;
        printAt(left + leftPadding, top, keyText);
    }
}

void InkHUD::KeyboardApplet::drawKey(uint8_t index, bool selected)
{
    uint16_t keyX = 0;
    uint16_t keyY = 0;
    uint16_t keyW = 0;
    uint16_t keyH = 0;
    if (!getKeyBounds(index, keyX, keyY, keyW, keyH))
        return;
    if (keyW == 0 || keyH == 0)
        return;

    // Translate absolute tile coordinates into applet-local coordinates.
    const int16_t localX = keyX - getTile()->getLeft();
    const int16_t localY = keyY - getTile()->getTop();
    const bool enabled = isKeyEnabledAt(index);

    // Clean background first so hidden keys never leave stale pixels when mode changes.
    fillRect(localX, localY, keyW, keyH, WHITE);

    if (!enabled)
        return;

    fillRoundRect(localX, localY, keyW, keyH, KEY_RADIUS, selected ? BLACK : WHITE);
    drawRoundRect(localX, localY, keyW, keyH, KEY_RADIUS, BLACK);

    const int16_t labelTop = localY + ((keyH - fontSmall.lineHeight()) / 2);
    drawKeyLabel(localX, labelTop, keyW, getKeyLabelAt(index), selected ? WHITE : BLACK);
}

void InkHUD::KeyboardApplet::drawKeyLabel(uint16_t left, uint16_t top, uint16_t width, const std::string &label, Color color)
{
    if (label.empty())
        return;

    setTextColor(color);
    uint16_t textW = getTextWidth(label);
    if (textW > width) {
        // Keep labels readable in narrow keys.
        textW = getTextWidth("..");
        printAt(left + ((width - textW) >> 1), top, "..");
        return;
    }

    uint16_t leftPadding = (width - textW) >> 1;
    printAt(left + leftPadding, top, label);
}

void InkHUD::KeyboardApplet::onForeground()
{
    handleInput = true;
    mode = MODE_TEXT;
    lastTypingMode = MODE_TEXT;
    emotePage = 0;
    selectedKey = 0;
    prevSelectedKey = 0;
    normalizeSelection();
}

void InkHUD::KeyboardApplet::onBackground()
{
    handleInput = false;
}

void InkHUD::KeyboardApplet::onButtonShortPress()
{
    if (!useTouchKeyboard()) {
        handleLegacyInput(false);
        return;
    }

    inputSelectedKey(false);
}

void InkHUD::KeyboardApplet::onButtonLongPress()
{
    if (!useTouchKeyboard()) {
        handleLegacyInput(true);
        return;
    }

    inputSelectedKey(true);
}

void InkHUD::KeyboardApplet::handleLegacyInput(bool longPress)
{
    char key = legacyKeys[selectedKey];
    if (key == '\n') {
        inkhud->freeTextDone();
        inkhud->closeKeyboard();
    } else if (key == '\x1b') {
        inkhud->freeTextCancel();
        inkhud->closeKeyboard();
    } else {
        if (longPress && key >= 0x61)
            key -= 32;
        inkhud->freeText(key);
    }
}

void InkHUD::KeyboardApplet::onExitShort()
{
    inkhud->freeTextCancel();
    inkhud->closeKeyboard();
}

void InkHUD::KeyboardApplet::onExitLong()
{
    inkhud->freeTextCancel();
    inkhud->closeKeyboard();
}

void InkHUD::KeyboardApplet::onNavUp()
{
    if (!useTouchKeyboard()) {
        if (selectedKey < KBD_COLS)
            selectedKey += KBD_COLS * (LEGACY_KBD_ROWS - 1);
        else
            selectedKey -= KBD_COLS;

        requestUpdate(EInk::UpdateTypes::FAST, false);
        inkhud->forceUpdate(EInk::UpdateTypes::FAST);
        return;
    }

    if (selectedKey < KBD_COLS)
        selectedKey += KBD_COLS * (KBD_ROWS - 1);
    else
        selectedKey -= KBD_COLS;

    normalizeSelection();
    requestFastKeyboardRefresh();
}

void InkHUD::KeyboardApplet::onNavDown()
{
    if (!useTouchKeyboard()) {
        selectedKey += KBD_COLS;
        selectedKey %= LEGACY_KBD_KEY_COUNT;
        requestUpdate(EInk::UpdateTypes::FAST, false);
        inkhud->forceUpdate(EInk::UpdateTypes::FAST);
        return;
    }

    selectedKey += KBD_COLS;
    selectedKey %= KBD_KEY_COUNT;
    normalizeSelection();
    requestFastKeyboardRefresh();
}

void InkHUD::KeyboardApplet::onNavLeft()
{
    if (!useTouchKeyboard()) {
        if (selectedKey % KBD_COLS == 0)
            selectedKey += KBD_COLS - 1;
        else
            selectedKey--;

        requestUpdate(EInk::UpdateTypes::FAST, false);
        inkhud->forceUpdate(EInk::UpdateTypes::FAST);
        return;
    }

    if (selectedKey % KBD_COLS == 0)
        selectedKey += KBD_COLS - 1;
    else
        selectedKey--;

    normalizeSelection();
    requestFastKeyboardRefresh();
}

void InkHUD::KeyboardApplet::onNavRight()
{
    if (!useTouchKeyboard()) {
        if (selectedKey % KBD_COLS == KBD_COLS - 1)
            selectedKey -= KBD_COLS - 1;
        else
            selectedKey++;

        requestUpdate(EInk::UpdateTypes::FAST, false);
        inkhud->forceUpdate(EInk::UpdateTypes::FAST);
        return;
    }

    if (selectedKey % KBD_COLS == KBD_COLS - 1)
        selectedKey -= KBD_COLS - 1;
    else
        selectedKey++;

    normalizeSelection();
    requestFastKeyboardRefresh();
}

bool InkHUD::KeyboardApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    // If touch is outside our tile, let other handlers process it.
    if (!getTile())
        return false;
    const uint16_t tileL = getTile()->getLeft();
    const uint16_t tileT = getTile()->getTop();
    const uint16_t tileR = tileL + getTile()->getWidth();
    const uint16_t tileB = tileT + getTile()->getHeight();
    if (x < tileL || x >= tileR || y < tileT || y >= tileB)
        return false;

    const int16_t hitIndex = getKeyIndexAt(x, y);
    // Consume touches that land in keyboard whitespace/disabled cells so we don't
    // fall back to generic short-press behavior (which would type the old selection).
    if (hitIndex < 0)
        return true;

    const uint8_t newSelected = (uint8_t)hitIndex;
    if (selectedKey != newSelected) {
        selectedKey = newSelected;
        normalizeSelection();
        if (showSelectionHighlight())
            requestFastKeyboardRefresh();
    }

    if (!isKeyEnabledAt(selectedKey))
        return true;

    inputSelectedKey(longPress);
    return true;
}

bool InkHUD::KeyboardApplet::getKeyBounds(uint8_t index, uint16_t &left, uint16_t &top, uint16_t &width, uint16_t &height)
{
    if (index >= KBD_KEY_COUNT || !getTile())
        return false;

    const uint16_t tileW = getTile()->getWidth();
    const uint16_t tileH = getTile()->getHeight();
    const uint16_t tileL = getTile()->getLeft();
    const uint16_t tileT = getTile()->getTop();
    const uint8_t row = index / KBD_COLS;
    const uint8_t col = index % KBD_COLS;

    const uint16_t totalGapY = KEY_GAP_Y * (KBD_ROWS + 1);
    const uint16_t keyH = (tileH > totalGapY) ? ((tileH - totalGapY) / KBD_ROWS) : (tileH / KBD_ROWS);
    top = tileT + KEY_GAP_Y + row * (keyH + KEY_GAP_Y);
    height = keyH;

    const uint16_t totalGapX = KEY_GAP_X * (KBD_COLS + 1);
    const uint16_t rowSpace = (tileW > totalGapX) ? (tileW - totalGapX) : tileW;
    uint32_t rowUnits = 0;
    const uint8_t rowStart = row * KBD_COLS;
    for (uint8_t i = 0; i < KBD_COLS; i++) {
        rowUnits += getKeyWidthAt(rowStart + i);
    }
    if (rowUnits == 0)
        return false;

    uint32_t cursorX = tileL + KEY_GAP_X;
    for (uint8_t i = 0; i < col; i++) {
        const uint8_t rowIndex = rowStart + i;
        const uint32_t keyW = ((uint32_t)rowSpace * getKeyWidthAt(rowIndex)) / rowUnits;
        cursorX += keyW + KEY_GAP_X;
    }

    left = (uint16_t)cursorX;

    if (col == (KBD_COLS - 1)) {
        const uint32_t rightEdge = tileL + tileW - KEY_GAP_X;
        width = (rightEdge > cursorX) ? (uint16_t)(rightEdge - cursorX) : 0;
    } else {
        width = (uint16_t)(((uint32_t)rowSpace * getKeyWidthAt(index)) / rowUnits);
    }

    return true;
}

int16_t InkHUD::KeyboardApplet::getKeyIndexAt(uint16_t x, uint16_t y)
{
    for (uint8_t i = 0; i < KBD_KEY_COUNT; i++) {
        uint16_t keyL = 0;
        uint16_t keyT = 0;
        uint16_t keyW = 0;
        uint16_t keyH = 0;
        if (!getKeyBounds(i, keyL, keyT, keyW, keyH))
            return -1;

        if (keyW == 0 || keyH == 0)
            continue;

        if (x >= keyL && x < (keyL + keyW) && y >= keyT && y < (keyT + keyH))
            return i;
    }

    return -1;
}

void InkHUD::KeyboardApplet::inputSelectedKey(bool longPress)
{
    inputKeyCode(getKeyCodeAt(selectedKey), longPress);
}

void InkHUD::KeyboardApplet::inputKeyCode(int16_t keyCode, bool longPress)
{
    if (keyCode == KEY_NONE)
        return;

    if (keyCode >= KEY_EMOTE_SLOT_BASE) {
        const uint8_t slot = (uint8_t)(keyCode - KEY_EMOTE_SLOT_BASE);
        const uint16_t emoteIndex = emotePage * EMOTE_SLOT_COUNT + slot;
        if (emoteIndex < fontEmoteCount)
            inkhud->freeText((char)fontEmotes[emoteIndex]);
        return;
    }

    switch (keyCode) {
    case KEY_BACKSPACE:
        inkhud->freeText('\b');
        return;
    case KEY_SEND:
        inkhud->freeTextDone();
        inkhud->closeKeyboard();
        return;
    case KEY_EMOTE_TOGGLE:
        toggleEmoteMode();
        return;
    case KEY_PUNCT_TOGGLE:
    case KEY_ALPHA_TOGGLE:
        togglePunctuationMode();
        return;
    case KEY_EMOTE_UP:
        pageEmotes(false);
        return;
    case KEY_EMOTE_DOWN:
        pageEmotes(true);
        return;
    default:
        break;
    }

    if (keyCode >= 0 && keyCode <= 0xFF) {
        char key = (char)keyCode;
        if (longPress && key >= 'a' && key <= 'z')
            key = (char)std::toupper((unsigned char)key);
        inkhud->freeText(key);
    }
}

int16_t InkHUD::KeyboardApplet::getKeyCodeAt(uint8_t index) const
{
    if (index >= KBD_KEY_COUNT)
        return KEY_NONE;

    if (mode == MODE_TEXT)
        return textKeys[index];
    if (mode == MODE_PUNCT)
        return punctKeys[index];

    // Emote mode
    if (index < EMOTE_SLOT_COUNT) {
        const uint16_t emoteIndex = emotePage * EMOTE_SLOT_COUNT + index;
        if (emoteIndex < fontEmoteCount)
            return KEY_EMOTE_SLOT_BASE + index;
        return KEY_NONE;
    }

    // Emote controls on the bottom row
    switch (index - EMOTE_SLOT_COUNT) {
    case 0:
        return KEY_EMOTE_UP;
    case 1:
        return KEY_EMOTE_DOWN;
    case 2:
        return KEY_ALPHA_TOGGLE;
    case 3:
        return ',';
    case 4:
        return ' ';
    case 5:
        return '.';
    case 6:
        return KEY_SEND;
    case 7:
        return KEY_BACKSPACE;
    default:
        return KEY_NONE;
    }
}

uint16_t InkHUD::KeyboardApplet::getKeyWidthAt(uint8_t index) const
{
    if (index >= KBD_KEY_COUNT)
        return 0;

    if (mode == MODE_EMOTE)
        return emoteKeyWidths[index];
    return typingKeyWidths[index];
}

std::string InkHUD::KeyboardApplet::getKeyLabelAt(uint8_t index) const
{
    const int16_t keyCode = getKeyCodeAt(index);
    if (keyCode == KEY_NONE)
        return "";

    if (keyCode >= KEY_EMOTE_SLOT_BASE) {
        const uint8_t slot = (uint8_t)(keyCode - KEY_EMOTE_SLOT_BASE);
        const uint16_t emoteIndex = emotePage * EMOTE_SLOT_COUNT + slot;
        if (emoteIndex < fontEmoteCount)
            return std::string(1, (char)fontEmotes[emoteIndex]);
        return "";
    }

    switch (keyCode) {
    case KEY_BACKSPACE:
        return "DEL";
    case KEY_SEND:
        return "SEND";
    case KEY_EMOTE_TOGGLE:
        return std::string(1, (char)0x03); // Smiling face icon from InkHUD emote font map
    case KEY_PUNCT_TOGGLE:
        return "!#1";
    case KEY_ALPHA_TOGGLE:
        return "ABC";
    case KEY_EMOTE_UP:
        return "UP";
    case KEY_EMOTE_DOWN:
        return "DN";
    default:
        break;
    }

    if (keyCode >= 0 && keyCode <= 0xFF) {
        const char c = (char)keyCode;
        if (c == ' ')
            return "SPACE";
        if (c >= 'a' && c <= 'z')
            return std::string(1, (char)std::toupper((unsigned char)c));
        return std::string(1, c);
    }

    return "";
}

bool InkHUD::KeyboardApplet::isKeyEnabledAt(uint8_t index) const
{
    return getKeyCodeAt(index) != KEY_NONE;
}

void InkHUD::KeyboardApplet::normalizeSelection()
{
    if (!useTouchKeyboard()) {
        if (selectedKey >= LEGACY_KBD_KEY_COUNT)
            selectedKey = 0;
        return;
    }

    if (selectedKey >= KBD_KEY_COUNT)
        selectedKey = 0;

    if (isKeyEnabledAt(selectedKey))
        return;

    for (uint8_t i = 0; i < KBD_KEY_COUNT; i++) {
        if (isKeyEnabledAt(i)) {
            selectedKey = i;
            return;
        }
    }
}

void InkHUD::KeyboardApplet::togglePunctuationMode()
{
    if (mode == MODE_EMOTE) {
        mode = lastTypingMode;
    } else {
        mode = (mode == MODE_TEXT) ? MODE_PUNCT : MODE_TEXT;
        lastTypingMode = mode;
    }

    normalizeSelection();
    requestFastKeyboardRefresh(true);
}

void InkHUD::KeyboardApplet::toggleEmoteMode()
{
    if (mode == MODE_EMOTE) {
        mode = lastTypingMode;
    } else {
        lastTypingMode = mode;
        mode = MODE_EMOTE;
    }

    emotePage = 0;
    normalizeSelection();
    requestFastKeyboardRefresh(true);
}

void InkHUD::KeyboardApplet::pageEmotes(bool down)
{
    if (mode != MODE_EMOTE)
        return;

    const uint8_t maxPage = (fontEmoteCount == 0) ? 0 : (uint8_t)((fontEmoteCount - 1) / EMOTE_SLOT_COUNT);

    if (down) {
        if (emotePage < maxPage)
            emotePage++;
    } else {
        if (emotePage > 0)
            emotePage--;
    }

    normalizeSelection();
    requestFastKeyboardRefresh(true);
}

void InkHUD::KeyboardApplet::requestFastKeyboardRefresh(bool full)
{
    requestUpdate(EInk::UpdateTypes::FAST, full);
}

bool InkHUD::KeyboardApplet::showSelectionHighlight() const
{
    // On touch-capable devices, prioritize input throughput over per-key highlight updates.
    // E-ink refresh can lag rapid taps; skipping highlight avoids update-induced input latency.
    return !inkhud->hasTouchEnabledProvider();
}

uint16_t InkHUD::KeyboardApplet::getKeyboardHeight()
{
    const auto *hud = NicheGraphics::InkHUD::InkHUD::getInstance();
    if (!hud || !hud->hasTouchEnabledProvider())
        return static_cast<uint16_t>(fontSmall.lineHeight() * 1.2f) * LEGACY_KBD_ROWS;

    // Keep touch keys tall and roomy for finger input.
    // In portrait orientation we increase row height for larger touch targets.
    const uint16_t rowUnit = fontSmall.lineHeight() + 8;
    const uint8_t rowScale = usePortraitKeyboardSizing() ? 3 : 2;
    const uint16_t keyH = rowUnit * rowScale;
    return (keyH * KBD_ROWS) + (KEY_GAP_Y * (KBD_ROWS + 1));
}
#endif
