#include "configuration.h"
#if HAS_SCREEN
#include "VirtualKeyboard.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "main.h"
#if defined(VK_HAS_CJK_IME)
#if defined(CJK_IME_ZHUYIN)
// bpmf_engine.h (and with it the composer and the dictionary) arrives through
// VirtualKeyboard.h, which needs the engine type for its member.
#elif defined(TINYLORA_ADVANCED_IME)
#include "pinyin_trie_helpers.h"
#else
#include <pinyin_simple_backend.h>
#endif
#endif
#include <Arduino.h>
#include <vector>

namespace graphics
{

#if defined(GAT562_T9_KEYBOARD)
static uint8_t gat562T9GroupForChar(char c)
{
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    if (c >= 'a' && c <= 'c')
        return 2;
    if (c >= 'd' && c <= 'f')
        return 3;
    if (c >= 'g' && c <= 'i')
        return 4;
    if (c >= 'j' && c <= 'l')
        return 5;
    if (c >= 'm' && c <= 'o')
        return 6;
    if (c >= 'p' && c <= 's')
        return 7;
    if (c >= 't' && c <= 'v')
        return 8;
    if (c >= 'w' && c <= 'z')
        return 9;
    if (c >= '2' && c <= '9')
        return c - '0';
    if (c == '1' || c == '<' || c == '>' || c == '.' || c == ',' || c == '?' || c == '!' || c == ':')
        return 1;
    if (c == '*' || c == '+')
        return 10;
    if (c == ' ' || c == '0')
        return 11;
    if (c == '#' || c == '^')
        return 12;
    return 0;
}
#endif

VirtualKeyboard::VirtualKeyboard() : cursorRow(0), cursorCol(0), lastActivityTime(millis())
{
    initializeKeyboard();
    // Set cursor to H(2, 5)
    cursorRow = 0;
    cursorCol = 0;
#if defined(GAT562_T9_KEYBOARD)
    candidateCursor = 0;
#endif
}

VirtualKeyboard::~VirtualKeyboard() {}

void VirtualKeyboard::initializeKeyboard()
{
    // New 4-row layout with 10 characters + 1 action key per row (11 columns):
    // 1) 1 2 3 4 5 6 7 8 9 0 BACK
    // 2) q w e r t y u i o p ENTER
    // 3) a s d f g h j k l ; SPACE
    // 4) z x c v b n m . , ? ESC
    static const char LAYOUT[KEYBOARD_ROWS][KEYBOARD_COLS] = {{'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\b'},
                                                              {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '\n'},
                                                              {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', ' '},
                                                              {'z', 'x', 'c', 'v', 'b', 'n', 'm', '.', ',', '?', '\x1b'}};

    // Derive layout dimensions and assert they match the configured keyboard grid
    constexpr int LAYOUT_ROWS = (int)(sizeof(LAYOUT) / sizeof(LAYOUT[0]));
    constexpr int LAYOUT_COLS = (int)(sizeof(LAYOUT[0]) / sizeof(LAYOUT[0][0]));
    static_assert(LAYOUT_ROWS == KEYBOARD_ROWS, "LAYOUT rows must equal KEYBOARD_ROWS");
    static_assert(LAYOUT_COLS == KEYBOARD_COLS, "LAYOUT cols must equal KEYBOARD_COLS");

#if defined(VK_HAS_CJK_IME)
#if defined(TINYLORA_ADVANCED_IME)
    displayList = {};
    selectionPos = {};
    resultsOffset = 0;
#else
    selectList = "";
    selectListLayout = {};
    selectListOffset = 0;
#endif
#endif

    // Initialize all keys to empty first
    for (int row = 0; row < LAYOUT_ROWS; row++) {
        for (int col = 0; col < LAYOUT_COLS; col++) {
            keyboard[row][col] = {0, VK_CHAR, 0, 0, 0, 0};
        }
    }

    // Fill keyboard from the 2D layout
    for (int row = 0; row < LAYOUT_ROWS; row++) {
        for (int col = 0; col < LAYOUT_COLS; col++) {
            char ch = LAYOUT[row][col];
            // No empty slots in the simplified layout

            VirtualKeyType type = VK_CHAR;
            if (ch == '\b') {
                type = VK_BACKSPACE;
            } else if (ch == '\n') {
                type = VK_ENTER;
            } else if (ch == '\x1b') { // ESC
                type = VK_ESC;
            } else if (ch == ' ') {
                type = VK_SPACE;
            }

            // Make action keys wider to fit text while keeping the last column aligned
            uint8_t width = (type == VK_BACKSPACE || type == VK_ENTER || type == VK_SPACE) ? (KEY_WIDTH * 3) : KEY_WIDTH;
            keyboard[row][col] = {ch, type, (uint8_t)(col * KEY_WIDTH), (uint8_t)(row * KEY_HEIGHT), width, KEY_HEIGHT};
        }
    }
}

void VirtualKeyboard::draw(OLEDDisplay *display, int16_t offsetX, int16_t offsetY)
{
    // Repeat ticking is driven by NotificationRenderer once per frame
    // Base styles
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Screen geometry
    const int screenW = display->getWidth();
    const int screenH = display->getHeight();

    // Decide wide-screen mode: if there is comfortable width, allow taller keys and reserve fixed width for last column labels
    // Heuristic: if screen width >= 200px (e.g., 240x135), treat as wide
    const bool isWide = screenW >= 200;

    // Determine last-column label max width
    display->setFont(FONT_SMALL);
    const int wENTER = display->getStringWidth("ENTER");
    int lastColLabelW = wENTER; // ENTER is usually the widest
    // Smaller padding on very small screens to avoid excessive whitespace
    const int lastColPad = (screenW <= 128 ? 2 : 6);
    const int reservedLastColW = lastColLabelW + lastColPad; // reserved width for last column keys

    // Always reserve width for the rightmost text column to avoid overlap on small screens
    int cellW = 0;
    int leftoverW = 0;
    {
        const int leftCols = KEYBOARD_COLS - 1; // 10 input characters
        int usableW = screenW - reservedLastColW;
        if (usableW < leftCols) {
            // Guard: ensure at least 1px per left cell if labels are extremely wide (unlikely)
            usableW = leftCols;
        }
        cellW = usableW / leftCols;
        leftoverW = usableW - cellW * leftCols; // distribute extra pixels over left columns (left to right)
    }

    // Dynamic key geometry
    int cellH = KEY_HEIGHT;
    if (isWide) {
        // For wide screens (e.g., T114 240x135), prefer square keys: height equals left-column key width.
        cellH = std::max((int)KEY_HEIGHT, cellW);

        // Guarantee at least 2 lines of input are visible by reducing cell height minimally if needed.
        // Replicate the spacing used in drawInputArea(): headerGap=1, box-to-header gap=1, gap above keyboard=1
        display->setFont(FONT_SMALL);
        const int headerHeight = headerText.empty() ? 0 : (FONT_HEIGHT_SMALL + 1);
        const int headerToBoxGap = 1;
        const int gapAboveKb = 1;
        const int minBoxHeightForTwoLines = 2 * FONT_HEIGHT_SMALL + 2; // inner 1px top/bottom
        int maxKeyboardHeight = screenH - (offsetY + headerHeight + headerToBoxGap + minBoxHeightForTwoLines + gapAboveKb);
        int maxCellHAllowed = maxKeyboardHeight / KEYBOARD_ROWS;
        if (maxCellHAllowed < (int)KEY_HEIGHT)
            maxCellHAllowed = KEY_HEIGHT;
        if (maxCellHAllowed > 0 && cellH > maxCellHAllowed) {
            cellH = maxCellHAllowed;
        }
    }

    // Keyboard placement from bottom
    const int keyboardHeight = KEYBOARD_ROWS * cellH;
    int keyboardStartY = screenH - keyboardHeight;
    if (keyboardStartY < 0)
        keyboardStartY = 0;

    // Draw input area above keyboard
    drawInputArea(display, offsetX, offsetY, keyboardStartY);

    // Precompute per-column x and width with leftover distributed over left columns for even spacing
    int colX[KEYBOARD_COLS];
    int colW[KEYBOARD_COLS];
    int runningX = offsetX;
    for (int col = 0; col < KEYBOARD_COLS - 1; ++col) {
        int wcol = cellW + (col < leftoverW ? 1 : 0);
        colX[col] = runningX;
        colW[col] = wcol;
        runningX += wcol;
    }
    // Last column
    colX[KEYBOARD_COLS - 1] = runningX;
    colW[KEYBOARD_COLS - 1] = reservedLastColW;

    // Draw keyboard grid
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            const VirtualKey &k = keyboard[row][col];
            if (k.character != 0 || k.type != VK_CHAR) {
                const bool isLastCol = (col == KEYBOARD_COLS - 1);
                int x = colX[col];
                int w = colW[col];
                int y = offsetY + keyboardStartY + row * cellH;
                int h = cellH;
                bool selected = (row == cursorRow && col == cursorCol);
                drawKey(display, k, selected, x, y, (uint8_t)w, (uint8_t)h, isLastCol);
            }
        }
    }
}

void VirtualKeyboard::drawInputArea(OLEDDisplay *display, int16_t offsetX, int16_t offsetY, int16_t keyboardStartY)
{
    display->setColor(WHITE);

    const int screenWidth = display->getWidth();
    const int screenHeight = display->getHeight();
    // Use the standard small font metrics for input box sizing (restore original size)
    const int inputLineH = FONT_HEIGHT_SMALL;

    // Header uses the standard small (which may be larger on big screens)
    display->setFont(FONT_SMALL);
    int headerHeight = 0;
#if defined(VK_HAS_CJK_IME)
    int chineseArea = (IMEStatus == ACTIVE) ? 12 : 0;
    if (!headerText.empty() && IMEStatus == INACTIVE) {
#else
    int chineseArea = 0;
    if (!headerText.empty()) {
#endif
        // Draw header and reserve exact font height (plus a tighter gap) to maximize input area
        display->drawString(offsetX + 2, offsetY, headerText.c_str());
        // On very small screens (e.g., 128x64), push the input box as close as possible to the header
        headerHeight = FONT_HEIGHT_SMALL; // no extra padding baked in
    }

    // Input box - from below header down to just above the keyboard
    const int boxX = offsetX + 2;
    // Smaller gap below header on tiny screens, slightly larger otherwise
    const int gapBelowHeader = (screenHeight <= 64 ? 0 : 1);
    const int boxY = offsetY + headerHeight + gapBelowHeader;
    const int boxWidth = screenWidth - 4;
    // Ensure the box doesn't touch the keyboard: prefer a bigger guard gap on 64px screens
    int gapAboveKeyboard = (screenHeight <= 64 ? 3 : 1);
    // Minimum box height to fully contain one text line with 1px padding on top and bottom
    const int minBoxHeight = inputLineH + 2;
    int availableH = keyboardStartY - boxY - gapAboveKeyboard; // initial available height
    if (screenHeight <= 64 && availableH < minBoxHeight) {
        // Try to grow the box by reducing the gap above keyboard, but keep at least 1px separation
        int need = minBoxHeight - availableH;
        int canReduce = gapAboveKeyboard - 1;
        int reduce = std::min(need, canReduce);
        if (reduce > 0) {
            gapAboveKeyboard -= reduce;
            availableH = keyboardStartY - boxY - gapAboveKeyboard;
        }
    }
    int boxHeight;
    if (screenHeight <= 64) {
        // On tiny screens, enforce at least one text line + 2px padding when possible
        if (availableH >= minBoxHeight) {
            boxHeight = availableH; // maximize
        } else {
            // If still not enough space, use whatever is available but keep >=1px
            boxHeight = std::max(1, availableH);
        }
    } else {
        if (availableH < inputLineH + 2)
            availableH = inputLineH + 2; // ensure minimum readability on larger screens
        boxHeight = availableH;
    }

    // Draw box border
    //display->drawRect(boxX, boxY, boxWidth, boxHeight);

    display->setFont(FONT_SMALL);

    // Chinese selecting area display
#if defined(VK_HAS_CJK_IME)
    if (IMEStatus == ACTIVE) {
        std::string currentPinyin = inputText.substr(processedWords, inputText.length() - processedWords);
#if defined(TINYLORA_ADVANCED_IME)
        int fulllen = 0;
        std::vector<std::string> results = unified_search(currentPinyin, 60);
        resultsfulllen = results.size() - 1;
        displayList = {};
        selectionPos = {};

        for (int i = resultsOffset; i <= resultsfulllen; i++) {
            if (results.empty()) {
                break;
            }
            uint8_t lastpos = fulllen;
            uint8_t width = display->getStringWidth(results[i].c_str(), results[i].length(), true);
            fulllen += width + 6;
            if (fulllen < display->getWidth() - 5) {
                selectionPos.push_back(fulllen - 6);
                displayList.push_back(results[i]);
                display->drawString(lastpos, boxHeight - chineseArea, results[i].c_str());
                if ((i - resultsOffset) ==
#if defined(GAT562_T9_KEYBOARD)
                    candidateCursor
#else
                    cursorCol
#endif
                ) {
                    display->drawHorizontalLine(lastpos, boxHeight, width);
                    display->drawHorizontalLine(lastpos, boxHeight + 1, width);
                }
            } else {
                break;
            }
        }
#elif defined(CJK_IME_ZHUYIN)
        // Zhuyin backend: the engine returns whole-word candidates. Pack them into
        // the same selectList/selectListLayout structure the pinyin path fills, so
        // selectChineseChar()/getChineseChar() work unchanged - each layout entry
        // is one candidate word rather than one Han character.
        uint8_t gotChars = 0;
        selectList = "";
        selectListLayout = {};
        if (currentPinyin.empty()) {
            // Nothing is being composed: predict words that continue the last
            // committed character (中 → 中文 / 中國). The engine remembers that the
            // list came from prediction, which selectChineseChar() needs in order
            // to strip that leading character when one is chosen.
            std::string lastChar;
            size_t e = processedWords <= inputText.size() ? (size_t)processedWords : inputText.size();
            if (e > 0) {
                size_t s = e - 1;
                while (s > 0 && ((uint8_t)inputText[s] & 0xC0) == 0x80)
                    s--;
                lastChar = inputText.substr(s, e - s);
            }
            bpmfEngine.predictAfter(lastChar);
        } else {
            bpmfEngine.searchFor(currentPinyin);
        }
        const std::vector<std::string> &zcands = bpmfEngine.candidates();
        selectListfulllen = (int)zcands.size();
        // Fit as many candidates as the row physically holds rather than a fixed
        // count: two-character phrases are twice as wide as single characters, so a
        // hard "9 per page" overflows the candidate area once phrases appear. Page
        // by pixel width (room kept on the right for the ">" paging marker); a page
        // of single characters still shows ~9, a page of phrases ~6, and mixed
        // pages fill to the edge. Paging advances by however many actually fit.
        const int candAreaW = display->getWidth() - 10;
        for (size_t zi = (size_t)selectListOffset; zi < zcands.size() && gotChars < 9; zi++) {
            uint8_t hlw = display->getStringWidth(selectList.c_str(), selectList.length(), true);
            uint8_t cw = display->getStringWidth(zcands[zi].c_str(), zcands[zi].length(), true);
            if (gotChars > 0 && (int)(hlw + cw) > candAreaW)
                break; // next candidate would overflow - leave it for the following page
            if (cursorCol == gotChars) {
                // Underline spans the whole candidate word, not a fixed single-char
                // width - a two-character phrase (中文) must show one line under both
                // glyphs, otherwise only its first character appears selected.
                display->drawHorizontalLine(hlw, boxHeight, cw);
                display->drawHorizontalLine(hlw, boxHeight + 1, cw);
            }
            selectList.append(zcands[zi]);
            selectListLayout.push_back((uint8_t)zcands[zi].size());
            gotChars++;
        }
        selectableChars = gotChars;
        display->drawString(0, boxHeight - chineseArea, selectList.c_str());
#else
        uint8_t gotChars = 0;
        uint8_t copiedBytes = 0;
        selectList = "";
        selectListLayout = {};
        char *resultptr = pinyin_simple_search(currentPinyin.c_str());
        std::string List = (resultptr == NULL) ? "" : resultptr;
        selectListfulllen = List.length();
        std::string str = List.substr(selectListOffset);
        while (selectListfulllen != 0 && gotChars < 9) {
            uint8_t bytesToCopy;
            if (str.length() <= copiedBytes) {
                break;
            }
            bytesToCopy = getUtf8Length(str.c_str(), copiedBytes);
            if (
#if defined(GAT562_T9_KEYBOARD)
                candidateCursor
#else
                cursorCol
#endif
                == gotChars) {
                uint8_t width = display->getStringWidth(selectList.c_str(), selectList.length(), true);
                display->drawHorizontalLine(width, boxHeight, 12);
                display->drawHorizontalLine(width, boxHeight + 1, 12);
            }
            selectList.append(str.substr(copiedBytes, bytesToCopy));
            selectListLayout.push_back(bytesToCopy);
            copiedBytes += bytesToCopy;
            gotChars++;
        }
        selectableChars = gotChars;
        display->drawString(0, boxHeight - chineseArea, selectList.c_str());
#endif
        // Hide the paging marker when the candidates fit on one page, otherwise it
        // suggests a next page that does not exist. With several pages it is drawn on
        // every one of them, and pressing it on the last wraps back to the first. The
        // pinyin path keeps drawing it unconditionally: its selectListOffset is a byte
        // offset and does not carry the same meaning.
#if defined(CJK_IME_ZHUYIN)
        const bool showPagingMarker = hasMultipleCandidatePages();
#else
        const bool showPagingMarker = true;
#endif
        if (showPagingMarker) {
            display->drawString(display->width() - 10, boxHeight - chineseArea, ">");
            if (cursorCol == 9) {
                display->drawHorizontalLine(display->width() - 10, boxHeight, display->getStringWidth(">"));
                display->drawHorizontalLine(display->width() - 10, boxHeight + 1, display->getStringWidth(">"));
            }
        }
    }
#endif

    // Text rendering: multi-line if space allows (>= 2 lines), else single-line with leading ellipsis
    const int textX = boxX + 2;
    const int maxTextWidth = boxWidth - 4;
    const int maxLines = (boxHeight - 2) / inputLineH;
#if defined(TINYLORA_ADVANCED_IME)
    auto textWidth = [&](const std::string &text) { return display->getStringWidth(text.c_str(), text.length(), true); };
    auto drawText = [&](int16_t x, int16_t y, const std::string &text) { display->drawString(x, y, text.c_str()); };
#else
    auto textWidth = [&](const std::string &text) { return display->getStringWidth(text.c_str()); };
    auto drawText = [&](int16_t x, int16_t y, const std::string &text) { display->drawString(x, y, text.c_str()); };
#endif

    // Text actually drawn in the box. Defaults to the raw buffer.
    std::string renderText = inputText;
#if defined(CJK_IME_ZHUYIN)
    // The composing zhuyin (the tail of inputText past processedWords) is drawn with the
    // 10x10 CJK font, where the symbols sit flush against each other and a syllable like
    // ㄋㄧㄠ is hard to read. For display only, put a space between each composing symbol.
    // inputText itself is left untouched - it still feeds submitText() and the candidate
    // lookup, which must not see the padding.
    if (IMEStatus == ACTIVE && processedWords < inputText.size()) {
        renderText.assign(inputText, 0, processedWords);
        bool first = true;
        for (size_t i = processedWords; i < inputText.size();) {
            unsigned char lead = (unsigned char)inputText[i];
            size_t clen = (lead < 0x80) ? 1 : (lead < 0xE0) ? 2 : (lead < 0xF0) ? 3 : 4;
            if (i + clen > inputText.size())
                clen = inputText.size() - i;
            if (!first)
                renderText.push_back(' ');
            first = false;
            renderText.append(inputText, i, clen);
            i += clen;
        }
    }
#endif

    if (maxLines >= 2) {
        // Inner bounds for caret clamping
        const int innerLeft = boxX + 1;
        const int innerRight = boxX + boxWidth - 2;
        const int innerTop = boxY + 1;
        const int innerBottom = boxY + boxHeight - 2;

        // Wrap text greedily into lines that fit maxTextWidth
        std::vector<std::string> lines;
        {
            std::string remaining = renderText;
            while (!remaining.empty()) {
                int bestLen = 0;
                for (int len = 1; len <= (int)remaining.size(); ++len) {
                    int w = textWidth(remaining.substr(0, len));
                    if (w <= maxTextWidth)
                        bestLen = len;
                    else
                        break;
                }
                if (bestLen == 0) {
                    // At least show one character to make progress
                    bestLen = 1;
                }
                lines.emplace_back(remaining.substr(0, bestLen));
                remaining.erase(0, bestLen);
            }
        }

        const bool scrolledUp = ((int)lines.size() > maxLines);
        int caretX = textX;
        int caretY = innerTop;

        // Leave a small top gap to render '...' without replacing the first line
        const int topInset = 2;
        const int lineStep = std::max(1, inputLineH - 1); // slightly tighter than font height
        int lineY = innerTop + topInset;

        if (scrolledUp) {
            // Draw three small dots centered horizontally, vertically at the midpoint of the gap
            // between the inner top and the first line's top baseline. This avoids using a tall glyph.
            const int firstLineTop = lineY;                                   // baseline top for the first visible line
            const int gapMidY = innerTop + (firstLineTop - innerTop) / 2 + 1; // shift down 1px as requested
            const int centerX = boxX + boxWidth / 2;
            const int dotSpacing = 3; // px between dots
            const int dotSize = 1;    // small square dot
            display->fillRect(centerX - dotSpacing, gapMidY, dotSize, dotSize);
            display->fillRect(centerX, gapMidY, dotSize, dotSize);
            display->fillRect(centerX + dotSpacing, gapMidY, dotSize, dotSize);
        }

        // How many lines fit with our top inset and tighter step
        const int linesCapacity = std::max(1, (innerBottom - lineY + 1) / lineStep);
        const int linesToShow = std::min((int)lines.size(), linesCapacity);
        const int startIndex = scrolledUp ? ((int)lines.size() - linesToShow) : 0;

        for (int i = 0; i < linesToShow; ++i) {
            const std::string &chunk = lines[startIndex + i];
            drawText(textX, lineY, chunk);
            caretX = textX + textWidth(chunk);
            caretY = lineY;
            lineY += lineStep;
        }

        // Draw caret at end of the last visible line
        int caretPadY = 2;
        if (boxHeight >= inputLineH + 4)
            caretPadY = 3;
        int cursorTop = caretY + caretPadY;
        // Use lineStep so caret height matches the row spacing
        int cursorH = lineStep - caretPadY * 2;
        if (cursorH < 1)
            cursorH = 1;
        // Clamp vertical bounds to stay inside the inner rect
        if (cursorTop < innerTop)
            cursorTop = innerTop;
        if (cursorTop + cursorH - 1 > innerBottom)
            cursorH = innerBottom - cursorTop + 1;
        if (cursorH < 1)
            cursorH = 1;
        // Only draw if cursor is inside inner bounds
        if (caretX >= innerLeft && caretX <= innerRight) {
            display->drawVerticalLine(caretX, cursorTop, cursorH);
        }
    } else {
        std::string displayText = renderText;
        int textW = textWidth(displayText);
        std::string scrolled = displayText;
        if (textW > maxTextWidth) {
            // Trim from the left until it fits
            while (textW > maxTextWidth && !scrolled.empty()) {
                scrolled.erase(0, 1);
                textW = textWidth(scrolled);
            }
            // Add leading ellipsis and ensure it still fits
            if (scrolled != displayText) {
                scrolled = "..." + scrolled;
                textW = textWidth(scrolled);
                // If adding ellipsis causes overflow, trim more after the ellipsis
                while (textW > maxTextWidth && scrolled.size() > 3) {
                    scrolled.erase(3, 1); // remove chars after the ellipsis
                    textW = textWidth(scrolled);
                }
            }
        } else {
            // Keep textW in sync with what we draw
            textW = textWidth(scrolled);
        }

        const int innerLeft = boxX + 1;
        const int innerRight = boxX + boxWidth - 2;
        const int innerTop = boxY + 1;
        const int innerBottom = boxY + boxHeight - 2;

        // Position text above vertical center; total up-shift by 4px for single-line
        int innerH = innerBottom - innerTop + 1;
        int textY = innerTop + std::max(0, (innerH - inputLineH) / 2) - 5; // was -4, now -5
        // Allow clamping to the outer border so upward shift remains visible on very small boxes
        if (textY < boxY)
            textY = boxY;
        if (!scrolled.empty()) {
            drawText(textX, textY, scrolled);
        }

        int cursorX = textX + textW;
        if (cursorX > innerRight)
            cursorX = innerRight;

        // Caret: height = outer box height - 4, with a 2px margin from top/bottom
        int cursorTop = boxY + 2;
        int cursorH = boxHeight - 4;
        if (cursorH < 1)
            cursorH = 1;
        // Clamp vertical bounds to stay inside the inner rect
        if (cursorTop < innerTop)
            cursorTop = innerTop;
        if (cursorTop + cursorH - 1 > innerBottom)
            cursorH = innerBottom - cursorTop + 1;
        if (cursorH < 1)
            cursorH = 1;

        // Only draw if cursor is inside inner bounds
        if (cursorX >= innerLeft && cursorX <= innerRight) {
            display->drawVerticalLine(cursorX, cursorTop, cursorH - chineseArea);
        }

        //display->drawVerticalLine(cursorX, cursorTop, cursorH);
    }
}

void VirtualKeyboard::drawKey(OLEDDisplay *display, const VirtualKey &key, bool selected, int16_t x, int16_t y, uint8_t width,
                              uint8_t height, bool isLastCol)
{
    // Draw key content
    display->setFont(FONT_SMALL);
    const int fontH = FONT_HEIGHT_SMALL;
    // Build label and metrics first
    std::string keyText;
    if (key.type == VK_BACKSPACE || key.type == VK_ENTER || key.type == VK_SPACE || key.type == VK_ESC) {
        // Keep literal text labels for the action keys on the rightmost column
        keyText = (key.type == VK_BACKSPACE) ? "BACK" : (key.type == VK_ENTER) ? "ENTER" : (key.type == VK_SPACE) ? "SPACE" : "";
        if (key.type == VK_ESC) {
#if defined(CJK_IME_ZHUYIN)
            keyText = (IMEStatus == ACTIVE) ? "TW ESC" : "EN ESC";
#elif defined(VK_HAS_CJK_IME)
            keyText = (IMEStatus == ACTIVE) ? "CN ESC" : "EN ESC";
#else
            keyText = "EN ESC";
#endif
        }
    } else {
#if defined(CJK_IME_ZHUYIN)
        // In Chinese mode a key face shows the Bopomofo symbol its grid position maps
        // to; positions with no mapping (the tone slots, the "?" key) stay blank. Latin
        // mode keeps the original ASCII labels.
        if (IMEStatus == ACTIVE) {
            const bpmf::Symbol *sym = bpmf::screen_symbol(key.character);
            keyText = sym ? std::string(sym->utf8) : "";
        } else
#endif
        {
            char c = getCharForKey(key, false);
            keyText = (key.character == ' ' || key.character == '_') ? "_" : std::string(1, c);
            // Show the common "/" pairing next to "?" like on a real keyboard
            if (key.type == VK_CHAR && key.character == '?') {
                keyText = "?/";
            }
        }
    }

#if defined(TINYLORA_ADVANCED_IME)
    int textWidth = display->getStringWidth(keyText.c_str(), keyText.length(), true);
#else
    int textWidth = display->getStringWidth(keyText.c_str());
#endif
    // Label alignment
    // - Rightmost action column: right-align text with a small right padding (~2px) so it hugs screen edge neatly.
    // - Other keys: center horizontally; use ceil-style rounding to avoid appearing left-biased on odd widths.
    int textX;
    if (isLastCol) {
        const int rightPad = 2;
        textX = x + width - textWidth - rightPad;
        if (textX < x)
            textX = x; // guard
    } else {
        textX = x + (width - textWidth) / 2;
    }
#if defined(CJK_IME_ZHUYIN)
    // Bopomofo key faces go through the CJK glyph path: drawUtf8Glyph paints a full
    // OLED_CJK_SIZE pixels starting at y + OLEDDISPLAY_UTF8_TOP_PADDING, which is
    // taller than KEY_HEIGHT. Centring on FONT_HEIGHT_SMALL therefore pushes the
    // bottom row off screen, and symbols with a stroke down there lose it.
    const bool cjkLabel = (IMEStatus == ACTIVE && key.type == VK_CHAR && !keyText.empty());
    const int cjkGlyphSpan = OLED_CJK_SIZE + OLEDDISPLAY_UTF8_TOP_PADDING;
#endif

    int contentTop = y;
    int contentH = height;
    bool pendingHighlight = false;
    int highlightY = 0;
    int highlightH = 0;
    if (selected) {
        display->setColor(WHITE);
        bool isAction = (key.type == VK_BACKSPACE || key.type == VK_ENTER || key.type == VK_SPACE || key.type == VK_ESC);
        if (isAction) {
            const int padX = 2; // small horizontal padding around text
            const int padY = 1; // vertical padding so highlight doesn't touch edges
            int hlX = textX - padX;
            int hlW = textWidth + padX * 2;
            // Constrain highlight within the key's horizontal span
            int keyRight = x + width;
            if (hlX < x) {
                hlW -= (x - hlX);
                hlX = x;
            }
            int maxW = keyRight - hlX;
            if (hlW > maxW)
                hlW = maxW;
            if (hlW < 1)
                hlW = 1;
            // Vertical: keep a small gap from top/bottom to avoid overlap with neighboring rows
            int hlY = y + padY;
            int hlH = height - padY * 2 + 2; // extend downward by 1px
            if (hlH < 1)
                hlH = 1;
            display->fillRect(hlX, hlY, hlW, hlH);
            contentTop = hlY;
            contentH = hlH;
        } else {
            highlightY = y + 1;
            highlightH = height + 1;
            if (highlightH < 1)
                highlightH = 1;
            // Deferred until the label position is final: a Bopomofo key face may be
            // clamped back on screen, and the box has to follow it. Otherwise the black
            // glyph lands outside the white box and disappears entirely.
            pendingHighlight = true;
            contentTop = highlightY;
            contentH = highlightH;
        }
        display->setColor(BLACK);
    } else {
        display->setColor(WHITE);
    }

    int centeredTextY = contentTop + (contentH - fontH) / 2;
#if defined(CJK_IME_ZHUYIN)
    if (cjkLabel) {
        const int maxTextY = display->getHeight() - cjkGlyphSpan;
        if (centeredTextY > maxTextY)
            centeredTextY = maxTextY;
        if (centeredTextY < 0)
            centeredTextY = 0;
        if (pendingHighlight) {
            highlightY = centeredTextY + OLEDDISPLAY_UTF8_TOP_PADDING;
            highlightH = OLED_CJK_SIZE;
        }
    }
#endif
    if (pendingHighlight) {
        display->setColor(WHITE);
        display->fillRect(x, highlightY, width, highlightH);
        display->setColor(BLACK);
    }
    if (key.type == VK_CHAR) {
        if (keyText.size() == 1) {
            char ch = keyText[0];
            bool tinyScreen = (display->getHeight() <= 64);
            if (tinyScreen) {
                if (ch == 'g' || ch == 'j' || ch == 'q' || ch == 'y' || ch == 'p' || ch == 'v' || ch == '.' || ch == ',' ||
                    ch == ';') {
                    centeredTextY -= 1;
                    if (centeredTextY < 0)
                        centeredTextY = 0;
                }
            }
        }
    }
    display->drawString(textX, centeredTextY, keyText.c_str());
}

char VirtualKeyboard::getCharForKey(const VirtualKey &key, bool isLongPress)
{
    if (key.type != VK_CHAR) {
        return key.character;
    }

    char c = key.character;

    // Long-press: letters become uppercase; for "?" provide "/" like a typical keyboard
    if (isLongPress) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (c == '?') {
            c = '/';
        }
    }

    return c;
}

void VirtualKeyboard::moveCursorDelta(int dRow, int dCol)
{
    resetTimeout();
    // wrap around rows and cols in the 4x11 grid
    int r = (int)cursorRow + dRow;
    int c = (int)cursorCol + dCol;
    if (r < 0)
        r = KEYBOARD_ROWS - 1;
    else if (r >= KEYBOARD_ROWS)
        r = 0;
    if (c < 0)
        c = KEYBOARD_COLS - 1;
    else if (c >= KEYBOARD_COLS)
        c = 0;
    cursorRow = (uint8_t)r;
    cursorCol = (uint8_t)c;
}

void VirtualKeyboard::moveCursorUp()
{
#if defined(GAT562_T9_KEYBOARD)
    if (moveCandidateCursor(-1))
        return;
#endif
    moveCursorDelta(-1, 0);
}
void VirtualKeyboard::moveCursorDown()
{
#if defined(GAT562_T9_KEYBOARD)
    if (moveCandidateCursor(1))
        return;
#endif
    moveCursorDelta(1, 0);
}
void VirtualKeyboard::moveCursorLeft()
{
#if defined(GAT562_T9_KEYBOARD)
    if (moveCandidateCursor(-1))
        return;
#endif
    moveCursorDelta(0, -1);
}
void VirtualKeyboard::moveCursorRight()
{
#if defined(GAT562_T9_KEYBOARD)
    if (moveCandidateCursor(1))
        return;
#endif
    moveCursorDelta(0, 1);
}

#if defined(GAT562) && !defined(GAT562_T9_KEYBOARD)
void VirtualKeyboard::moveCursorNext()
{
    resetTimeout();

    const int totalKeys = KEYBOARD_ROWS * KEYBOARD_COLS;
    int index = (int)cursorRow * KEYBOARD_COLS + cursorCol;
    for (int i = 0; i < totalKeys; i++) {
        index = (index + 1) % totalKeys;
        uint8_t row = (uint8_t)(index / KEYBOARD_COLS);
        uint8_t col = (uint8_t)(index % KEYBOARD_COLS);
        const VirtualKey &key = keyboard[row][col];
        if (key.character != 0 || key.type != VK_CHAR) {
            cursorRow = row;
            cursorCol = col;
            return;
        }
    }
}
#endif

#if defined(GAT562_T9_KEYBOARD)
bool VirtualKeyboard::hasChineseCandidates() const
{
    return IMEStatus == ACTIVE && chineseCandidateCount() > 0;
}

uint8_t VirtualKeyboard::chineseCandidateCount() const
{
#if defined(TINYLORA_ADVANCED_IME)
    return (uint8_t)displayList.size();
#else
    return selectableChars;
#endif
}

bool VirtualKeyboard::moveCandidateCursor(int delta)
{
    if (!hasChineseCandidates())
        return false;

    uint8_t count = chineseCandidateCount();
    if (count == 0)
        return false;

    int next = (int)candidateCursor + delta;
    if (next < 0) {
#if defined(TINYLORA_ADVANCED_IME)
        if (resultsOffset == 0) {
#else
        if (selectListOffset == 0) {
#endif
            candidateCursor = count - 1;
            resetTimeout();
            return true;
        }
        showPrevSelection();
        resetTimeout();
        return true;
    }
    else if (next >= count) {
        showNextSelection();
        resetTimeout();
        return true;
    }
    candidateCursor = (uint8_t)next;
    resetTimeout();
    return true;
}
#endif

void VirtualKeyboard::handlePress()
{
    resetTimeout(); // Reset timeout on any input activity

#if defined(GAT562_T9_KEYBOARD)
    if (hasChineseCandidates()) {
        selectChineseChar(candidateCursor);
        return;
    }
#endif

    const VirtualKey &key = keyboard[cursorRow][cursorCol];

    // Don't handle press if the key is empty (but allow special keys)
    if (key.character == 0 && key.type == VK_CHAR) {
        return;
    }

    // For character keys, insert lowercase character
    if (key.type == VK_CHAR) {
        insertCharacter(getCharForKey(key, false)); // false = lowercase/normal char
        return;
    }

    // Handle non-character keys immediately
    switch (key.type) {
    case VK_BACKSPACE:
        deleteCharacter();
        break;
    case VK_ENTER:
        submitText();
        break;
    case VK_SPACE:
        insertCharacter(' ');
        break;
    case VK_ESC:
        if (onTextEntered) {
            std::function<void(const std::string &)> callback = onTextEntered;
            onTextEntered = nullptr;
            inputText = "";
            callback("");
        }
        return;
    default:
        break;
    }
}

void VirtualKeyboard::handleLongPress()
{
    resetTimeout(); // Reset timeout on any input activity

    const VirtualKey &key = keyboard[cursorRow][cursorCol];
    // directly enter what cursor selected instead of enter digits.
#if defined(GAT562_T9_KEYBOARD)
    if (hasChineseCandidates()) {
        selectChineseChar(candidateCursor);
        return;
    }
#elif defined(VK_HAS_CJK_IME)
    if (IMEStatus == ACTIVE && cursorCol <= 8) {
        selectChineseChar(cursorCol);
        return;
    }
#endif

#if defined(VK_HAS_CJK_IME)
    if (IMEStatus == ACTIVE && cursorCol == 9) {
#if defined(CJK_IME_ZHUYIN)
        // draw() omits ">" when there is only one page, so a long press must not page
        // either -- the same do-nothing behaviour as a candidate slot with no candidate.
        if (hasMultipleCandidatePages())
#endif
            showNextSelection();
        return;
    }
#endif

    // Don't handle press if the key is empty (but allow special keys)
    if (key.character == 0 && key.type == VK_CHAR) {
        return;
    }

    if (key.character == '1') {
        insertCharacter('/');
        return;
    }

    // For character keys, insert uppercase/alternate character
    if (key.type == VK_CHAR) {
        insertCharacter(getCharForKey(key, true)); // true = uppercase/alternate char
        return;
    }

    switch (key.type) {
    case VK_BACKSPACE:
        // One-shot: delete up to 5 characters on long press
        for (int i = 0; i < 5; ++i) {
            if (inputText.empty())
                break;
            deleteCharacter();
        }
        break;
    case VK_ENTER:
        submitText();
        break;
    case VK_SPACE:
        insertCharacter(' ');
        break;
    case VK_ESC:
        //if (onTextEntered) {
        //    onTextEntered("");
        //}
		toggleIME();
        break;
    default:
        break;
    }
}

void VirtualKeyboard::handleBackspace()
{
    resetTimeout();
#if defined(GAT562_T9_KEYBOARD)
    lastT9Group = 0;
#endif
    deleteCharacter();
}

void VirtualKeyboard::handleCharacter(char c)
{
    resetTimeout();
#if defined(GAT562_T9_KEYBOARD)
    lastT9Group = 0;
#endif

    if (c == '\b') {
        deleteCharacter();
        return;
    }
    if (c == '\r' || c == '\n') {
        submitText();
        return;
    }
    if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
    }
    if (c >= 0x20 && c <= 0x7e) {
        insertCharacter(c);
    }
}

#if defined(GAT562_T9_KEYBOARD)
void VirtualKeyboard::handleT9Character(char c)
{
    resetTimeout();

    if (c == '\b') {
        lastT9Group = 0;
        deleteCharacter();
        return;
    }
    if (c == '\r' || c == '\n') {
        submitText();
        return;
    }

    const uint8_t group = gat562T9GroupForChar(c);
    const uint32_t now = millis();
    if (group != 0 && group == lastT9Group && now - lastT9Millis < 850) {
        deleteCharacter();
    }

    if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
    }

    // In CN mode, T9 digit suffixes are only used to cycle letters, not to select candidates.
    if (IMEStatus == ACTIVE && c >= '2' && c <= '9') {
        lastT9Group = group;
        lastT9Millis = now;
        return;
    }

    if (c >= 0x20 && c <= 0x7e) {
        insertCharacter(c);
        lastT9Group = group;
        lastT9Millis = now;
    }
}
#endif

void VirtualKeyboard::insertCharacter(char c)
{
#if defined(VK_HAS_CJK_IME)
    if (IMEStatus == ACTIVE) {
#if defined(CJK_IME_ZHUYIN)
        // The on-screen Daqian layout puts Bopomofo symbols on the digit and letter
        // keys, so look the key up first and emit the symbol; only an unmapped key is
        // treated as a literal character. Picking a candidate is a joystick long press
        // (see handleLongPress), so digits are not selection keys here and fall
        // straight through to the symbol lookup.
        if (c == ' ' && inputText.length() > processedWords) {
            // Space types the neutral tone while composing -- the grid has no room for
            // a fourth tone key, see BpmfComposer.h. With nothing composing it stays a
            // literal space and falls through to the ordinary character branch.
            const bpmf::Symbol *tone5 = bpmf::tone5_symbol();
            if (selectListOffset != 0)
                selectListOffset = 0; // candidates are recomputed, go back to page one
            inputText += tone5->utf8;
            inputTextLayout.push_back((uint8_t)strlen(tone5->utf8));
        } else if (const bpmf::Symbol *sym = bpmf::screen_symbol(c)) {
            if (selectListOffset != 0)
                selectListOffset = 0; // reset offset when input more chars.
            inputText += sym->utf8;
            inputTextLayout.push_back((uint8_t)strlen(sym->utf8));
        } else if (c == '0') {
            showNextSelection();
        } else if (inputText.length() == processedWords) { // not composing zhuyin
            inputText += c;
            inputTextLayout.push_back(1);
            processedWords++;
        }
    } else
#else
        if (c >= '1' && c <= '9'
#if defined(TINYLORA_ADVANCED_IME)
            && !displayList.empty()
#else
            && !selectList.empty()
#endif
        ) { // digits for chinese selection
#if defined(GAT562_T9_KEYBOARD)
            selectChineseChar(c - '1');
#else
            selectChineseChar(cursorCol);
#endif
        } else if (c >= 'a' && c <= 'z') { // pinyin input
#if defined(TINYLORA_ADVANCED_IME)
            if (resultsOffset != 0)
                resultsOffset = 0; // reset offset when input more chars.
#else
            if (selectListOffset != 0)
                selectListOffset = 0; // reset offset when input more chars.
#endif
            inputText += c;
            inputTextLayout.push_back(1);
#if defined(GAT562_T9_KEYBOARD)
            candidateCursor = 0;
#endif
        } else if (c == '0') {
            showNextSelection();
        } else if (inputText.length() == processedWords) { // not in pinyin selection mode
            inputText += c;
            inputTextLayout.push_back(1);
            processedWords++; // let it go.
        }
    } else
#endif // CJK_IME_ZHUYIN
#endif // VK_HAS_CJK_IME
    {
        if (inputText.length() < 160) { // Reasonable text length limit
            inputText += c;
            inputTextLayout.push_back(1);
            processedWords++; // not in ime mode so let it go.
        }
    }
}

void VirtualKeyboard::deleteCharacter()
{
    if (!inputText.empty()) {
        uint8_t lengthToErase = 1;
        if (!inputTextLayout.empty()) {
            lengthToErase = inputTextLayout.back();
            inputTextLayout.pop_back();
        } else {
            lengthToErase = getLastUtf8CharLength();
        }
        if (lengthToErase == 0 || lengthToErase > inputText.length()) {
            lengthToErase = 1;
        }
        inputText.erase(inputText.length() - lengthToErase, lengthToErase);
        if (inputText.length() < processedWords) {
            processedWords = inputText.length();
        }
    }
}

uint8_t VirtualKeyboard::getLastUtf8CharLength() const
{
    if (inputText.empty()) {
        return 0;
    }

    size_t pos = inputText.length() - 1;
    while (pos > 0 && (static_cast<uint8_t>(inputText[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    return inputText.length() - pos;
}

void VirtualKeyboard::submitText()
{
    LOG_INFO("Virtual keyboard: submitting text '%s'", inputText.c_str());

    // Only submit if text is not empty
    if (!inputText.empty() && onTextEntered) {
        // Store callback and text to submit before clearing callback
#if defined(VK_HAS_CJK_IME)
#if defined(TINYLORA_ADVANCED_IME)
        displayList = {};
        selectionPos = {};
        resultsOffset = 0;
#else
        selectList = "";
        selectListLayout = {};
        selectListOffset = 0;
#if defined(GAT562_T9_KEYBOARD)
        candidateCursor = 0;
#endif
#endif
#endif

        std::function<void(const std::string &)> callback = onTextEntered;
        std::string textToSubmit = inputText;
        onTextEntered = nullptr;
        // Don't clear inputText here - let the calling module handle cleanup
        // inputText = "";  // Removed: keep text visible until module cleans up
        callback(textToSubmit);
    } else if (inputText.empty()) {
        // For empty text, just ignore the submission - don't clear callback
        // This keeps the virtual keyboard responsive for further input
        LOG_INFO("Virtual keyboard: empty text submitted, ignoring - keyboard remains active");
    } else {
        // No callback available
        if (screen) {
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        }
    }
}

void VirtualKeyboard::cancelInput()
{
    if (onTextEntered) {
        auto callback = onTextEntered;
        onTextEntered = nullptr;
        callback(std::string());
    }
}

void VirtualKeyboard::setInputText(const std::string &text)
{
    inputText = text;
    inputTextLayout.clear();
    for (size_t i = 0; i < inputText.length();) {
        uint8_t len = getUtf8Length(inputText.c_str(), i);
        if (len == 0 || i + len > inputText.length()) {
            len = 1;
        }
        inputTextLayout.push_back(len);
        i += len;
    }
    processedWords = inputText.length();
}

std::string VirtualKeyboard::getInputText() const
{
    return inputText;
}

void VirtualKeyboard::setHeader(const std::string &header)
{
    headerText = header;
}

void VirtualKeyboard::setCallback(std::function<void(const std::string &)> callback)
{
    onTextEntered = callback;
}

void VirtualKeyboard::resetTimeout()
{
    lastActivityTime = millis();
}

bool VirtualKeyboard::isTimedOut() const
{
    return (millis() - lastActivityTime) > TIMEOUT_MS;
}

void VirtualKeyboard::toggleIME()
{
#if defined(VK_HAS_CJK_IME)
    if (IMEStatus == ACTIVE) { // reset vars
#if defined(TINYLORA_ADVANCED_IME)
        displayList = {};
        selectionPos = {};
        resultsOffset = 0;
#else
        selectList = "";
        selectListLayout = {};
        selectListOffset = 0;
#if defined(GAT562_T9_KEYBOARD)
        candidateCursor = 0;
#endif
#endif
    } else {
        processedWords = inputText.length(); // mark previous chars as processed
#if defined(GAT562_T9_KEYBOARD)
        candidateCursor = 0;
#endif
    }
    IMEStatus == ACTIVE ? IMEStatus = INACTIVE : IMEStatus = ACTIVE;
#endif
}

uint8_t VirtualKeyboard::getUtf8Length(const char *c, uint8_t pos)
{
    uint8_t byte1 = (uint8_t)c[pos];

    if (byte1 < 0x80) {
        // ASCII character
        return 1;
    } else if ((byte1 & 0xE0) == 0xC0) {
        // 2-byte UTF-8
        return 2;
    } else if ((byte1 & 0xF0) == 0xE0) {
        // 3-byte UTF-8 (most CJK characters)
        return 3;
    } else if ((byte1 & 0xF8) == 0xF0) {
        // 4-byte UTF-8
        return 4;
    }
    return 0;
}

#if defined(VK_HAS_CJK_IME)
#if !defined(TINYLORA_ADVANCED_IME)
uint8_t VirtualKeyboard::getChineseChar(uint8_t c)
{
    int ret = 0;
    for (int i = 0; i < c; i++) { // consecutively extract bytes
        ret += selectListLayout[i];
    }
    return ret;
}
#endif

void VirtualKeyboard::selectChineseChar(uint8_t chridx)
{
#if defined(TINYLORA_ADVANCED_IME)
    if (chridx + 1 > displayList.size())
        return; // make sure it won't overflow.
#else
    if (chridx >= selectableChars)
        return; // make sure it won't overflow.
#endif
    int pinyinLength = inputText.length() - processedWords;
    inputText.erase(inputText.length() - pinyinLength, inputText.length());
#if defined(CJK_IME_ZHUYIN)
    // Each Bopomofo input unit is a 3-byte symbol and inputTextLayout holds one entry
    // per symbol (value 3), so the composing region has fewer entries than bytes.
    // Erasing entries by the byte count pinyinLength, as the pinyin path does, runs the
    // iterator past begin() and corrupts the layout vector -- intermittent heap
    // corruption and reboots. Remove entries from the back until their lengths add up
    // to the byte count instead.
    {
        int bytesToRemove = pinyinLength;
        while (bytesToRemove > 0 && !inputTextLayout.empty()) {
            bytesToRemove -= inputTextLayout.back();
            inputTextLayout.pop_back();
        }
    }
#else
    inputTextLayout.erase(inputTextLayout.end() - pinyinLength, inputTextLayout.end());
#endif
#if defined(CJK_IME_ZHUYIN)
    if (bpmfEngine.isPrediction()) {
        // A prediction word starts with the character already committed (中 for the
        // candidate 中文); remove that trailing character so appending the whole word
        // yields 中文, not 中中文. In prediction mode nothing is composing, so the
        // erase above was a no-op and inputTextLayout ends with that committed char.
        if (!inputTextLayout.empty()) {
            uint8_t lastLen = inputTextLayout.back();
            inputTextLayout.pop_back();
            if (inputText.size() >= lastLen)
                inputText.erase(inputText.size() - lastLen);
            if (processedWords >= lastLen)
                processedWords -= lastLen;
        }
    }
#endif
#if defined(TINYLORA_ADVANCED_IME)
    std::string word = displayList[chridx];
#else
    std::string word = selectList.substr(getChineseChar(chridx), selectListLayout[chridx]);
#endif
    inputText.append(word);
#if defined(TINYLORA_ADVANCED_IME)
    for (int len = 0; len < word.length();) {
        uint8_t charLength = getUtf8Length(word.c_str(), len);
        inputTextLayout.push_back(charLength);
        processedWords += charLength;
        len += charLength;
    }
    resultsOffset = 0;
#else
    // A candidate may be more than one character (multi-character phrases in the
    // zhuyin dictionary), so commit every character: one layout entry each and
    // advance processedWords over the whole word. Handling only the first
    // character would leave the tail sitting past processedWords, where it is
    // mistaken for composing zhuyin and blocks all further candidates.
    for (size_t len = 0; len < word.length();) {
        uint8_t charLength = getUtf8Length(word.c_str(), (uint8_t)len);
        if (charLength == 0)
            break; // malformed UTF-8 guard; avoid an infinite loop
        inputTextLayout.push_back(charLength);
        processedWords += charLength;
        len += charLength;
    }
    selectListOffset = 0;
#if defined(GAT562_T9_KEYBOARD)
    candidateCursor = 0;
#endif
#endif
}

#if defined(CJK_IME_ZHUYIN)
bool VirtualKeyboard::hasMultipleCandidatePages() const
{
    // selectListOffset is a candidate index and selectListfulllen the candidate count
    // in this path; selectableChars is how many of them the current page fit. Testing
    // the total against one page's worth (rather than "is there anything after this
    // page") keeps the marker on the last page, where showNextSelection() wraps back
    // to the first - otherwise paging becomes a dead end at the end of the list.
    return selectableChars > 0 && selectListfulllen > selectableChars;
}
#endif

void VirtualKeyboard::showNextSelection()
{
#if defined(TINYLORA_ADVANCED_IME)
    uint8_t listlen = displayList.size();
    uint8_t nextOffset = resultsOffset + listlen;
    resultsOffset = nextOffset > resultsfulllen ? 0 : nextOffset;
#elif defined(CJK_IME_ZHUYIN)
    // In the zhuyin path selectListOffset is a candidate index and selectListfulllen the
    // candidate count (see draw()), unlike the pinyin path where both are byte offsets.
    // Advance by the number of candidates shown on the current page, not selectList's
    // byte length; otherwise a single page overshoots the count and wraps to 0, so
    // long-pressing '>' appears to do nothing.
    uint8_t nextOffset = selectListOffset + selectableChars;
    selectListOffset = nextOffset >= selectListfulllen ? 0 : nextOffset;
#else
    uint8_t listlen = selectList.length();
    uint8_t nextOffset = selectListOffset + listlen;
    selectListOffset = nextOffset >= selectListfulllen ? 0 : nextOffset;
#endif
#if defined(GAT562_T9_KEYBOARD)
    candidateCursor = 0;
#endif
}

#if defined(GAT562_T9_KEYBOARD)
void VirtualKeyboard::showPrevSelection()
{
#if defined(TINYLORA_ADVANCED_IME)
    uint8_t listlen = displayList.size();
    if (resultsOffset != 0) {
        resultsOffset = resultsOffset > listlen ? resultsOffset - listlen : 0;
    }
    candidateCursor = listlen > 0 ? listlen - 1 : 0;
#else
    uint8_t pageBytes = 0;
    if (!selectListLayout.empty())
        pageBytes = selectListLayout.front() * 9;
    else
        pageBytes = selectList.length();

    if (selectListOffset != 0) {
        selectListOffset = selectListOffset > pageBytes ? selectListOffset - pageBytes : 0;
    }
    uint8_t charBytes = selectListLayout.empty() ? 3 : selectListLayout.front();
    uint8_t remainingChars = charBytes == 0 ? 0 : (selectListfulllen - selectListOffset) / charBytes;
    uint8_t pageChars = std::min<uint8_t>(9, remainingChars);
    candidateCursor = pageChars > 0 ? pageChars - 1 : 0;
#endif
}
#endif
#endif // VK_HAS_CJK_IME

} // namespace graphics
#endif
