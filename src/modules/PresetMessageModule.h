#pragma once
#if defined(HAS_SCREEN) && (ELECROW_ThinkNode_M8)
#include "input/InputBroker.h"
#include "ProtobufModule.h"
#include "MeshService.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/ScreenFonts.h"
#include "graphics/images.h"
#include "graphics/emotes.h"
#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
#include "graphics/EInkDynamicDisplay.h" // To select between full and fast refresh on E-Ink displays
#endif

#include "buzz.h"
#include <Throttle.h>

#ifndef CANNED_MESSAGE_MODULE_ENABLE
#define CANNED_MESSAGE_MODULE_ENABLE 0
#endif

#define PRESET_MESSAGE_MODULE_PRIORITY_MAX_COUNT 10
#define PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT 20

#define PRESET_INACTIVATE_AFTER_MS 30000

struct PresetNodeEntry {
    meshtastic_NodeInfoLite *node;
    uint32_t lastHeard;
};

enum PresetMessageModuleRunState 
{
    PRESET_MESSAGE_RUN_STATE_DISABLED,
    PRESET_MESSAGE_RUN_STATE_INACTIVE,

    PRESET_MESSAGE_RUN_STATE_ACTIVE,
    PRESET_MESSAGE_RUN_STATE_SENDING_ACTIVE,

    PRESET_MESSAGE_RUN_STATE_ACTION_SELECT,
    PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION,
    PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION,

    PRESET_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED,
};

class PresetMessageModule : public SinglePortModule, public Observable<const UIFrameEvent *>,private concurrency::OSThread
{
    public:
        PresetMessageModule();

        void LaunchWithDestination(NodeNum newDest, uint8_t newChannel = 0);

        void LaunchRepeatDestination();

        void clean_PresetMessageModule_state();

        virtual bool interceptingKeyboardInput() override;

        /*=== Message navigation ===*/
        const char *getNodeName(NodeNum node);

        const char *getPriorityByIndex(int index);

        const char *getMessageByIndex(int index);

        /*=== State/UI ===*/
        bool shouldDraw();

        void updateDestinationSelectionList();

        void drawDestinationSelectionScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

        virtual bool wantPacket(const meshtastic_MeshPacket *p) override
        {
            if (p->rx_rssi != 0)
                lastRxRssi = p->rx_rssi;
            if (p->rx_snr > 0)
                lastRxSnr = p->rx_snr;
            return (p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) ? waitingForAck : false;
        }

    protected:
        virtual int32_t runOnce() override;

        /*=== Transmission ===*/
        void sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies);

        void drawHeader(OLEDDisplay *display, int16_t x, int16_t y, char *buffer);

        int ConfiguredPresetMessages();

        int getNextPriorityIndex();

        int getPrevPriorityIndex();

        int getNextMessagesIndex();

        int getPrevMessagesIndex();

        /*=== Input Handling ===*/ 
        virtual bool wantUIFrame() override { return shouldDraw(); }

        virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }

        int handleInputEvent(const InputEvent *event);

        void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

        virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    private:
        /*=== Display and UI ===*/  
        int displayHeight = 64;

        int destIndex = 0;

        int scrollIndex = 0;
     
        int visibleRows = 0;

        /*=== Message Storage ===*/
        const char *messages_priority[PRESET_MESSAGE_MODULE_PRIORITY_MAX_COUNT];

        const char *messages_array[PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT];

        const char *Highest_messages[PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT];

        const char *High_messages[PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT];
        
        const char *Middle_messages[PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT];

        const char *Low_messages[PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT];

        const char *General_messages[PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT];

        int priorityCount = 0;

        int messageCount = 0;

        int highestCount = 0;

        int highCount = 0;

        int middleCount = 0;

        int lowCount = 0;

        int generalCount = 0;
        
        int currentpriorityIndex = -1;

        int currentMessageIndex = -1;

        /*=== Routing & Acknowledgment ===*/
        NodeNum dest = NODENUM_BROADCAST;     // Destination node for outgoing messages (default: broadcast)

        NodeNum lastSentNode = 0;             // Tracks the most recent node we sent a message to (for UI display)

        NodeNum incoming = NODENUM_BROADCAST; // Source node from which last ACK/NACK was received

        NodeNum lastDest = NODENUM_BROADCAST;

        bool lastDestSet = false;

        ChannelIndex channel = 0;             // Channel index used when sending a message

        uint8_t lastChannel = 0;

        bool ack = false;               // True = ACK received, False = NACK or failed
        
        bool waitingForAck = false;     // True if we're expecting an ACK and should monitor routing packets

        bool lastAckWasRelayed = false; // True if the ACK was relayed through intermediate nodes

        uint8_t lastAckHopStart = 0;    // Hop start value from the received ACK packet

        uint8_t lastAckHopLimit = 0;    // Hop limit value from the received ACK packet

        float lastRxSnr = 0;    // SNR from last received ACK (used for diagnostics/UI)

        int32_t lastRxRssi = 0; // RSSI from last received ACK (used for diagnostics/UI)

        /*=== Input Observers ===*/
        CallbackObserver<PresetMessageModule, const InputEvent *> inputObserver =
            CallbackObserver<PresetMessageModule, const InputEvent *>(this, &PresetMessageModule::handleInputEvent);

        /*=== State Tracking ===*/
        PresetMessageModuleRunState runState = PRESET_MESSAGE_RUN_STATE_INACTIVE;

        unsigned long LastOperationTime = 0;

        std::vector<uint8_t> activeChannelIndices;

        std::vector<PresetNodeEntry> filteredNodes;

        bool isUpEvent(const InputEvent *event);

        bool isDownEvent(const InputEvent *event);

        bool isSelectEvent(const InputEvent *event);

        int handleDestinationSelectionInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect);

        bool handlePrioritySelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect);

        bool handleMessageSelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect);

        bool hasKeyForNode(const meshtastic_NodeInfoLite *node);
};

extern PresetMessageModule *presetmessagemodule;

#endif
