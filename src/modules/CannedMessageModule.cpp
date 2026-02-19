#include "configuration.h"
#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif
#if HAS_SCREEN
#include "CannedMessageModule.h"
#include "Channels.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "buzz.h"
#include "detect/ScanI2C.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/MessageRenderer.h"
#include "graphics/draw/NotificationRenderer.h"
#include "graphics/images.h"
#include "main.h" // for cardkb_found
#include "mesh/generated/meshtastic/cannedmessages.pb.h"
#include "modules/AdminModule.h"
#include "modules/ExternalNotificationModule.h" // for buzzer control
extern MessageStore messageStore;
#if HAS_TRACKBALL
#include "input/TrackballInterruptImpl1.h"
#endif
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#ifndef INPUTBROKER_MATRIX_TYPE
#define INPUTBROKER_MATRIX_TYPE 0
#endif

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

// Remove Canned message screen if no action is taken for some milliseconds
#define INACTIVATE_AFTER_MS 20000

namespace graphics
{
extern int bannerSignalBars;
}
extern ScanI2C::DeviceAddress cardkb_found;
extern bool osk_found;

static const char *cannedMessagesConfigFile = "/prefs/cannedConf.proto";
NodeNum lastDest = NODENUM_BROADCAST;
uint8_t lastChannel = 0;
bool lastDestSet = false;

meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;

CannedMessageModule *cannedMessageModule;

CannedMessageModule::CannedMessageModule()
    : SinglePortModule("canned", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("CannedMessage")
{
    this->loadProtoForModule();
    if ((this->splitConfiguredMessages() <= 0) && (cardkb_found.address == 0x00) && !INPUTBROKER_MATRIX_TYPE) {
        LOG_INFO("CannedMessageModule: No messages are configured. Module is disabled");
        this->runState = CANNED_MESSAGE_RUN_STATE_DISABLED;
        disable();
    } else {
        LOG_INFO("CannedMessageModule is enabled");
        moduleConfig.canned_message.enabled = true;
        this->inputObserver.observe(inputBroker);
    }
}

void CannedMessageModule::LaunchWithDestination(NodeNum newDest, uint8_t newChannel)
{
    // Do NOT override explicit broadcast replies
    // Only reuse lastDest in LaunchRepeatDestination()

    dest = newDest;
    channel = newChannel;

    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;

    // Upon activation, highlight "[Select Destination]"
    int selectDestination = 0;
    for (int i = 0; i < messagesCount; ++i) {
        if (strcmp(messages[i], "[Select Destination]") == 0) {
            selectDestination = i;
            break;
        }
    }
    currentMessageIndex = selectDestination;

    // This triggers the canned message list
    runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    LOG_DEBUG("[CannedMessage] LaunchWithDestination dest=0x%08x ch=%d", dest, channel);
}

void CannedMessageModule::LaunchRepeatDestination()
{
    if (!lastDestSet) {
        LaunchWithDestination(NODENUM_BROADCAST, 0);
    } else {
        LaunchWithDestination(lastDest, lastChannel);
    }
}

void CannedMessageModule::LaunchFreetextWithDestination(NodeNum newDest, uint8_t newChannel)
{
    // Do NOT override explicit broadcast replies
    // Only reuse lastDest in LaunchRepeatDestination()

    dest = newDest;
    channel = newChannel;

    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;

    runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    LOG_DEBUG("[CannedMessage] LaunchFreetextWithDestination dest=0x%08x ch=%d", dest, channel);
}

bool hasKeyForNode(const meshtastic_NodeInfoLite *node)
{
    return node && node->has_user && node->user.public_key.size > 0;
}
/**
 * @brief Items in array this->messages will be set to be pointing on the right
 *     starting points of the string this->messageStore
 *
 * @return int Returns the number of messages found.
 */

int CannedMessageModule::splitConfiguredMessages()
{
    int i = 0;

    String canned_messages = cannedMessageModuleConfig.messages;

    // Copy all message parts into the buffer
    strncpy(this->messageBuffer, canned_messages.c_str(), sizeof(this->messageBuffer));

    // Temporary array to allow for insertion
    const char *tempMessages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT + 3] = {0};
    int tempCount = 0;
    // Insert at position 0 (top)
    tempMessages[tempCount++] = "[Select Destination]";
#if defined(USE_VIRTUAL_KEYBOARD)
    // Add a "Free Text" entry at the top if using a touch screen virtual keyboard
    tempMessages[tempCount++] = "[-- Free Text --]";
#else
    if (osk_found && screen) {
        tempMessages[tempCount++] = "[-- Free Text --]";
    }
#endif

    // First message always starts at buffer start
    tempMessages[tempCount++] = this->messageBuffer;
    int upTo = strlen(this->messageBuffer) - 1;

    // Walk buffer, splitting on '|'
    while (i < upTo) {
        if (this->messageBuffer[i] == '|') {
            this->messageBuffer[i] = '\0'; // End previous message
            if (tempCount >= CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT)
                break;
            tempMessages[tempCount++] = (this->messageBuffer + i + 1);
        }
        i += 1;
    }

    // Add [Exit] as the last entry
    tempMessages[tempCount++] = "[Exit]";

    // Copy to the member array
    for (int k = 0; k < tempCount; ++k) {
        this->messages[k] = (char *)tempMessages[k];
    }
    this->messagesCount = tempCount;

    return this->messagesCount;
}
void CannedMessageModule::drawHeader(OLEDDisplay *display, int16_t x, int16_t y, char *buffer)
{
    if (graphics::currentResolution == graphics::ScreenResolution::High) {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: #%s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: @%s", getNodeName(this->dest));
        }
    } else {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: #%.20s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: @%s", getNodeName(this->dest));
        }
    }
}

// Returns true if character input is currently allowed (used for search/freetext states)
bool CannedMessageModule::isCharInputAllowed() const
{
    return runState == CANNED_MESSAGE_RUN_STATE_FREETEXT || runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION;
}
/**
 * Main input event dispatcher for CannedMessageModule.
 * Routes keyboard/button/touch input to the correct handler based on the current runState.
 * Only one handler (per state) processes each event, eliminating redundancy.
 */
int CannedMessageModule::handleInputEvent(const InputEvent *event)
{
    // Block ALL input if an alert banner is active
    if (screen && screen->isOverlayBannerShowing()) {
        return 0;
    }

    // Tab key: Always allow switching between canned/destination screens
    if (event->kbchar == INPUT_BROKER_MSG_TAB && handleTabSwitch(event))
        return 1;

    // Matrix keypad: If matrix key, trigger action select for canned message
    if (event->inputEvent == INPUT_BROKER_MATRIXKEY) {
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
        payload = INPUT_BROKER_MATRIXKEY;
        currentMessageIndex = event->kbchar - 1;
        lastTouchMillis = millis();
        requestFocus();
        return 1;
    }

    // Always normalize navigation/select buttons for further handlers
    bool isUp = isUpEvent(event);
    bool isDown = isDownEvent(event);
    bool isSelect = isSelectEvent(event);

    // Route event to handler for current UI state (no double-handling)
    switch (runState) {
    // Node/Channel destination selection mode: Handles character search, arrows, select, cancel, backspace
    case CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION:
        if (handleDestinationSelectionInput(event, isUp, isDown, isSelect))
            return 1;
        return 0; // prevent fall-through to selector input

    // Free text input mode: Handles character input, cancel, backspace, select, etc.
    case CANNED_MESSAGE_RUN_STATE_FREETEXT:
        return handleFreeTextInput(event); // All allowed input for this state

    // Virtual keyboard mode: Show virtual keyboard and handle input

    // If sending, block all input except global/system (handled above)
    case CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE:
        return 1;

    // If sending, block all input except global/system (handled above)
    case CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER:
        return handleEmotePickerInput(event);

    case CANNED_MESSAGE_RUN_STATE_INACTIVE:
        if (event->inputEvent == INPUT_BROKER_ALT_LONG) {
            LaunchWithDestination(NODENUM_BROADCAST);
            return 1;
        }
        if (tryStartFreeTextFromInactive(event))
            return 1;
        return 0;
        break;

    // (Other states can be added here as needed)
    default:
        break;
    }

    // If no state handler above processed the event, let the message selector try to handle it
    // (Handles up/down/select on canned message list, exit/return)
    if (handleMessageSelectorInput(event, isUp, isDown, isSelect))
        return 1;

    // Default: event not handled by canned message system, allow others to process
    return 0;
}

bool CannedMessageModule::isUpEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_UP ||
           ((runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER ||
             runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) &&
            (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_ALT_PRESS));
}
bool CannedMessageModule::isDownEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_DOWN ||
           ((runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER ||
             runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) &&
            (event->inputEvent == INPUT_BROKER_RIGHT || event->inputEvent == INPUT_BROKER_USER_PRESS));
}
bool CannedMessageModule::isSelectEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_SELECT;
}

bool CannedMessageModule::handleTabSwitch(const InputEvent *event)
{
    if (event->kbchar != 0x09)
        return false;

    runState = (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) ? CANNED_MESSAGE_RUN_STATE_FREETEXT
                                                                            : CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION;

    destIndex = 0;
    scrollIndex = 0;
    // RESTORE THIS!
    if (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION)
        updateDestinationSelectionList();
    requestFocus();

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
    screen->forceDisplay();
    return true;
}

int32_t CannedMessageModule::runOnce()
{
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION && needsUpdate) {
        updateDestinationSelectionList();
        needsUpdate = false;
    }

    // If we're in node selection, do nothing except keep alive
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        return INACTIVATE_AFTER_MS;
    }

    // Normal module disable/idle handling
    if ((this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) || (this->runState == CANNED_MESSAGE_RUN_STATE_INACTIVE)) {
        // Clean up virtual keyboard if needed when going inactive
        if (graphics::NotificationRenderer::virtualKeyboard && graphics::NotificationRenderer::textInputCallback == nullptr) {
            LOG_INFO("Performing delayed virtual keyboard cleanup");
            graphics::OnScreenKeyboardModule::instance().stop(false);
        }

        return INT32_MAX;
    }

    // Handle delayed virtual keyboard message sending
    if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE && this->payload == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        // Virtual keyboard message sending case - text was not empty
        if (this->freetext.length() > 0) {
            LOG_INFO("Processing delayed virtual keyboard send: '%s'", this->freetext.c_str());
            sendText(this->dest, this->channel, this->freetext.c_str(), true);

            // Clean up virtual keyboard after sending
            if (graphics::NotificationRenderer::virtualKeyboard) {
                LOG_INFO("Cleaning up virtual keyboard after message send");
                graphics::OnScreenKeyboardModule::instance().stop(false);
                graphics::NotificationRenderer::resetBanner();
            }

            // Clear payload to indicate virtual keyboard processing is complete
            // But keep SENDING_ACTIVE state to show "Sending..." screen for 2 seconds
            this->payload = 0;
        } else {
            // Empty message, just go inactive
            LOG_INFO("Empty freetext detected in delayed processing, returning to inactive state");
            this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        }

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->notifyObservers(&e);
        return 2000;
    }

    UIFrameEvent e;
    if ((this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE && this->payload != 0 &&
         this->payload != CANNED_MESSAGE_RUN_STATE_FREETEXT) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_MESSAGE_SELECTION)) {
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->notifyObservers(&e);
    }
    // Handle SENDING_ACTIVE state transition after virtual keyboard message
    else if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE && this->payload == 0) {
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        return INT32_MAX;
    } else if (((this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) || (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT)) &&
               !Throttle::isWithinTimespanMs(this->lastTouchMillis, INACTIVATE_AFTER_MS)) {
        // Reset module on inactivity
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

        // Clean up virtual keyboard if it exists during timeout
        if (graphics::NotificationRenderer::virtualKeyboard) {
            LOG_INFO("Cleaning up virtual keyboard due to module timeout");
            graphics::OnScreenKeyboardModule::instance().stop(false);
            graphics::NotificationRenderer::resetBanner();
        }

        this->notifyObservers(&e);
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_SELECT) {
        if (this->payload == 0) {
            // [Exit] button pressed - return to inactive state
            LOG_INFO("Processing [Exit] action - returning to inactive state");
            this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        } else if (this->payload == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
            if (this->freetext.length() > 0) {
                sendText(this->dest, this->channel, this->freetext.c_str(), true);

                // Clean up state but *don’t* deactivate yet
                this->currentMessageIndex = -1;
                this->freetext = "";
                this->cursor = 0;

                // Tell Screen to jump straight to the TextMessage frame
                e.action = UIFrameEvent::Action::SWITCH_TO_TEXTMESSAGE;
                this->notifyObservers(&e);

                // Now deactivate this module
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

                return INT32_MAX; // don't fall back into canned list
            } else {
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
            }
        } else {
            if (strcmp(this->messages[this->currentMessageIndex], "[Select Destination]") == 0) {
                this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
                return INT32_MAX;
            }
            if ((this->messagesCount > this->currentMessageIndex) && (strlen(this->messages[this->currentMessageIndex]) > 0)) {
                if (strcmp(this->messages[this->currentMessageIndex], "~") == 0) {
                    return INT32_MAX;
                } else {
                    sendText(this->dest, this->channel, this->messages[this->currentMessageIndex], true);

                    // Clean up state
                    this->currentMessageIndex = -1;
                    this->freetext = "";
                    this->cursor = 0;

                    // Tell Screen to jump straight to the TextMessage frame
                    e.action = UIFrameEvent::Action::SWITCH_TO_TEXTMESSAGE;
                    this->notifyObservers(&e);

                    // Now deactivate this module
                    this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

                    return INT32_MAX; // don't fall back into canned list
                }
            } else {
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
            }
        }
        // fallback clean-up if nothing above returned
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;

        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->notifyObservers(&e);

        // Immediately stop, don't linger on canned screen
        return INT32_MAX;
    }
    // Highlight [Select Destination] initially when entering the message list
    else if ((this->runState != CANNED_MESSAGE_RUN_STATE_FREETEXT) && (this->currentMessageIndex == -1)) {
        // Only auto-highlight [Select Destination] if we’re ACTIVELY browsing,
        // not when coming back from a sent message.
        if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
            int selectDestination = 0;
            for (int i = 0; i < this->messagesCount; ++i) {
                if (strcmp(this->messages[i], "[Select Destination]") == 0) {
                    selectDestination = i;
                    break;
                }
            }
            this->currentMessageIndex = selectDestination;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_UP) {
        if (this->messagesCount > 0) {
            this->currentMessageIndex = getPrevIndex();
            this->freetext = "";
            this->cursor = 0;
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_DOWN) {
        if (this->messagesCount > 0) {
            this->currentMessageIndex = this->getNextIndex();
            this->freetext = "";
            this->cursor = 0;
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        return runFreeTextState(e);
    }

    if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
        this->lastTouchMillis = millis();
        this->notifyObservers(&e);
        return INACTIVATE_AFTER_MS;
    }
    return INT32_MAX;
}

const char *CannedMessageModule::getCurrentMessage()
{
    return this->messages[this->currentMessageIndex];
}
const char *CannedMessageModule::getPrevMessage()
{
    return this->messages[this->getPrevIndex()];
}
const char *CannedMessageModule::getNextMessage()
{
    return this->messages[this->getNextIndex()];
}
const char *CannedMessageModule::getMessageByIndex(int index)
{
    return (index >= 0 && index < this->messagesCount) ? this->messages[index] : "";
}

const char *CannedMessageModule::getNodeName(NodeNum node)
{
    if (node == NODENUM_BROADCAST)
        return "Broadcast";

    meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(node);
    if (info && info->has_user && strlen(info->user.long_name) > 0) {
        return info->user.long_name;
    }

    static char fallback[12];
    snprintf(fallback, sizeof(fallback), "0x%08x", node);
    return fallback;
}

bool CannedMessageModule::shouldDraw()
{
    // Only allow drawing when we're in an interactive UI state.
    return (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT ||
            this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION ||
            this->runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER);
}

// Has the user defined any canned messages?
// Expose publicly whether canned message module is ready for use
bool CannedMessageModule::hasMessages()
{
    return (this->messagesCount > 0);
}

int CannedMessageModule::getNextIndex()
{
    if (this->currentMessageIndex >= (this->messagesCount - 1)) {
        return 0;
    } else {
        return this->currentMessageIndex + 1;
    }
}

int CannedMessageModule::getPrevIndex()
{
    if (this->currentMessageIndex <= 0) {
        return this->messagesCount - 1;
    } else {
        return this->currentMessageIndex - 1;
    }
}

#if defined(USE_VIRTUAL_KEYBOARD)

String CannedMessageModule::keyForCoordinates(uint x, uint y)
{
    int outerSize = *(&this->keyboard[this->charSet] + 1) - this->keyboard[this->charSet];

    for (int8_t outerIndex = 0; outerIndex < outerSize; outerIndex++) {
        int innerSize = *(&this->keyboard[this->charSet][outerIndex] + 1) - this->keyboard[this->charSet][outerIndex];

        for (int8_t innerIndex = 0; innerIndex < innerSize; innerIndex++) {
            Letter letter = this->keyboard[this->charSet][outerIndex][innerIndex];

            if (x > letter.rectX && x < (letter.rectX + letter.rectWidth) && y > letter.rectY &&
                y < (letter.rectY + letter.rectHeight)) {
                return letter.character;
            }
        }
    }

    return "";
}

void CannedMessageModule::drawKeyboard(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    int outerSize = *(&this->keyboard[this->charSet] + 1) - this->keyboard[this->charSet];

    int xOffset = 0;

    int yOffset = 56;

    display->setTextAlignment(TEXT_ALIGN_LEFT);

    display->setFont(FONT_SMALL);

    display->setColor(OLEDDISPLAY_COLOR::WHITE);

    display->drawStringMaxWidth(0, 0, display->getWidth(),
                                cannedMessageModule->drawWithCursor(cannedMessageModule->freetext, cannedMessageModule->cursor));

    display->setFont(FONT_MEDIUM);

    int cellHeight = round((display->height() - 64) / outerSize);

    int yCorrection = 8;

    for (int8_t outerIndex = 0; outerIndex < outerSize; outerIndex++) {
        yOffset += outerIndex > 0 ? cellHeight : 0;

        int innerSizeBound = *(&this->keyboard[this->charSet][outerIndex] + 1) - this->keyboard[this->charSet][outerIndex];

        int innerSize = 0;

        for (int8_t innerIndex = 0; innerIndex < innerSizeBound; innerIndex++) {
            if (this->keyboard[this->charSet][outerIndex][innerIndex].character != "") {
                innerSize++;
            }
        }

        int cellWidth = display->width() / innerSize;

        for (int8_t innerIndex = 0; innerIndex < innerSize; innerIndex++) {
            xOffset += innerIndex > 0 ? cellWidth : 0;

            Letter letter = this->keyboard[this->charSet][outerIndex][innerIndex];

            Letter updatedLetter = {letter.character, letter.width, xOffset, yOffset, cellWidth, cellHeight};

#ifdef RAK14014 // Optimize the touch range of the virtual keyboard in the bottom row
            if (outerIndex == outerSize - 1) {
                updatedLetter.rectHeight = 240 - yOffset;
            }
#endif
            this->keyboard[this->charSet][outerIndex][innerIndex] = updatedLetter;

            float characterOffset = ((cellWidth / 2) - (letter.width / 2));

            if (letter.character == "⇧") {
                if (this->shift) {
                    display->fillRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->setColor(OLEDDISPLAY_COLOR::BLACK);

                    drawShiftIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);

                    display->setColor(OLEDDISPLAY_COLOR::WHITE);
                } else {
                    display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                    drawShiftIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);
                }
            } else if (letter.character == "⌫") {
                if (this->highlight == letter.character[0]) {
                    display->fillRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->setColor(OLEDDISPLAY_COLOR::BLACK);

                    drawBackspaceIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);

                    display->setColor(OLEDDISPLAY_COLOR::WHITE);

                    setIntervalFromNow(0);
                } else {
                    display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                    drawBackspaceIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);
                }
            } else if (letter.character == "↵") {
                display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                drawEnterIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.7);
            } else {
                if (this->highlight == letter.character[0]) {
                    display->fillRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->setColor(OLEDDISPLAY_COLOR::BLACK);

                    display->drawString(xOffset + characterOffset, yOffset + yCorrection,
                                        letter.character == " " ? "space" : letter.character);

                    display->setColor(OLEDDISPLAY_COLOR::WHITE);

                    setIntervalFromNow(0);
                } else {
                    display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->drawString(xOffset + characterOffset, yOffset + yCorrection,
                                        letter.character == " " ? "space" : letter.character);
                }
            }
        }

        xOffset = 0;
    }

    this->highlight = 0x00;
}

void CannedMessageModule::drawShiftIcon(OLEDDisplay *display, int x, int y, float scale)
{
    PointStruct shiftIcon[10] = {{8, 0}, {15, 7}, {15, 8}, {12, 8}, {12, 12}, {4, 12}, {4, 8}, {1, 8}, {1, 7}, {8, 0}};

    int size = 10;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (shiftIcon[i].x * scale);
        int y0 = y + (shiftIcon[i].y * scale);
        int x1 = x + (shiftIcon[i + 1].x * scale);
        int y1 = y + (shiftIcon[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }
}

void CannedMessageModule::drawBackspaceIcon(OLEDDisplay *display, int x, int y, float scale)
{
    PointStruct backspaceIcon[6] = {{0, 7}, {5, 2}, {15, 2}, {15, 12}, {5, 12}, {0, 7}};

    int size = 6;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (backspaceIcon[i].x * scale);
        int y0 = y + (backspaceIcon[i].y * scale);
        int x1 = x + (backspaceIcon[i + 1].x * scale);
        int y1 = y + (backspaceIcon[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }

    PointStruct backspaceIconX[4] = {{7, 4}, {13, 10}, {7, 10}, {13, 4}};

    size = 4;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (backspaceIconX[i].x * scale);
        int y0 = y + (backspaceIconX[i].y * scale);
        int x1 = x + (backspaceIconX[i + 1].x * scale);
        int y1 = y + (backspaceIconX[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }
}

void CannedMessageModule::drawEnterIcon(OLEDDisplay *display, int x, int y, float scale)
{
    PointStruct enterIcon[6] = {{0, 7}, {4, 3}, {4, 11}, {0, 7}, {15, 7}, {15, 0}};

    int size = 6;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (enterIcon[i].x * scale);
        int y0 = y + (enterIcon[i].y * scale);
        int x1 = x + (enterIcon[i + 1].x * scale);
        int y1 = y + (enterIcon[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }
}

#endif

// Indicate to screen class that module is handling keyboard input specially (at certain times)
// This prevents the left & right keys being used for nav. between screen frames during text entry.
bool CannedMessageModule::interceptingKeyboardInput()
{
    switch (runState) {
    case CANNED_MESSAGE_RUN_STATE_DISABLED:
    case CANNED_MESSAGE_RUN_STATE_INACTIVE:
        return false;
    default:
        return true;
    }
}

// Draw the node/channel selection screen
void CannedMessageModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    this->displayHeight = display->getHeight(); // Store display height for later use.
    char buffer[50];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Never draw if state is outside our UI modes.
    if (!(runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || runState == CANNED_MESSAGE_RUN_STATE_FREETEXT ||
          runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION || runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER ||
          runState == CANNED_MESSAGE_RUN_STATE_DISABLED)) {
        return;
    }

    // Emote Picker Screen
    if (this->runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER) {
        drawEmotePickerScreen(display, state, x, y);
        return;
    }

    // Destination Selection
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        drawDestinationSelectionScreen(display, state, x, y);
        return;
    }

    // Disabled Screen
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) {
        display->drawString(10 + x, 0 + y + FONT_HEIGHT_SMALL, "Canned Message\nModule disabled.");
        return;
    }

    // Free Text Input Screen
    if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        drawFreeTextScreen(display, state, x, y, buffer);
        return;
    }

    drawCannedMessageListScreen(display, state, x, y, buffer);
}

// Load canned-message module protobuf config, or install defaults on first boot.
void CannedMessageModule::loadProtoForModule()
{
    if (nodeDB->loadProto(cannedMessagesConfigFile, meshtastic_CannedMessageModuleConfig_size,
                          sizeof(meshtastic_CannedMessageModuleConfig), &meshtastic_CannedMessageModuleConfig_msg,
                          &cannedMessageModuleConfig) != LoadFileResult::LOAD_SUCCESS) {
        installDefaultCannedMessageModuleConfig();
    }
}
/**
 * @brief Save the module config to file.
 *
 * @return true On success.
 * @return false On error.
 */
bool CannedMessageModule::saveProtoForModule()
{
    bool okay = true;

#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    okay &= nodeDB->saveProto(cannedMessagesConfigFile, meshtastic_CannedMessageModuleConfig_size,
                              &meshtastic_CannedMessageModuleConfig_msg, &cannedMessageModuleConfig);

    return okay;
}

/**
 * @brief Fill configuration with default values.
 */
void CannedMessageModule::installDefaultCannedMessageModuleConfig()
{
    strncpy(cannedMessageModuleConfig.messages, "Hi|Bye|Yes|No|Ok", sizeof(cannedMessageModuleConfig.messages));
}

/**
 * @brief An admin message arrived to AdminModule. We are asked whether we want to handle that.
 *
 * @param mp The mesh packet arrived.
 * @param request The AdminMessage request extracted from the packet.
 * @param response The prepared response
 * @return AdminMessageHandleResult HANDLED if message was handled
 *   HANDLED_WITH_RESULT if a result is also prepared.
 */
AdminMessageHandleResult CannedMessageModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                          meshtastic_AdminMessage *request,
                                                                          meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_get_canned_message_module_messages_request_tag:
        LOG_DEBUG("Client getting radio canned messages");
        this->handleGetCannedMessageModuleMessages(mp, response);
        result = AdminMessageHandleResult::HANDLED_WITH_RESPONSE;
        break;

    case meshtastic_AdminMessage_set_canned_message_module_messages_tag:
        LOG_DEBUG("Client getting radio canned messages");
        this->handleSetCannedMessageModuleMessages(request->set_canned_message_module_messages);
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

void CannedMessageModule::handleGetCannedMessageModuleMessages(const meshtastic_MeshPacket &req,
                                                               meshtastic_AdminMessage *response)
{
    LOG_DEBUG("*** handleGetCannedMessageModuleMessages");
    if (req.decoded.want_response) {
        response->which_payload_variant = meshtastic_AdminMessage_get_canned_message_module_messages_response_tag;
        strncpy(response->get_canned_message_module_messages_response, cannedMessageModuleConfig.messages,
                sizeof(response->get_canned_message_module_messages_response));
    } // Don't send anything if not instructed to. Better than asserting.
}

void CannedMessageModule::handleSetCannedMessageModuleMessages(const char *from_msg)
{
    int changed = 0;

    if (*from_msg) {
        changed |= strcmp(cannedMessageModuleConfig.messages, from_msg);
        strncpy(cannedMessageModuleConfig.messages, from_msg, sizeof(cannedMessageModuleConfig.messages));
        LOG_DEBUG("*** from_msg.text:%s", from_msg);
    }

    if (changed) {
        this->saveProtoForModule();
        if (splitConfiguredMessages()) {
            moduleConfig.canned_message.enabled = true;
        }
    }
}

#endif
