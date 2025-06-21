#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./CannedMessageStore.h"

#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "generated/meshtastic/cannedmessages.pb.h"

using namespace NicheGraphics;

// Location of the file which stores the canned messages on flash
static const char *cannedMessagesConfigFile = "/prefs/cannedConf.proto";

CannedMessageStore::CannedMessageStore()
{
#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe(adminModule);
#endif

    // Load & parse messages from flash
    load();
}

// Get access to (or create) the singleton instance of this class
CannedMessageStore *CannedMessageStore::getInstance()
{
    // Instantiate the class the first time this method is called
    static CannedMessageStore *const singletonInstance = new CannedMessageStore;

    return singletonInstance;
}

// Access canned messages by index
// Consumer should check CannedMessageStore::size to avoid accessing out of bounds
const std::string &CannedMessageStore::at(uint8_t i)
{
    assert(i < messages.size());
    return messages.at(i);
}

// Number of canned message strings available
uint8_t CannedMessageStore::size()
{
    return messages.size();
}

// Load canned message data from flash, and parse into the individual strings
void CannedMessageStore::load()
{
    // In case we're reloading
    messages.clear();

    // Attempt to load the bulk canned message data from flash
    meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;
    LoadFileResult result = nodeDB->loadProto("/prefs/cannedConf.proto", meshtastic_CannedMessageModuleConfig_size,
                                              sizeof(meshtastic_CannedMessageModuleConfig),
                                              &meshtastic_CannedMessageModuleConfig_msg, &cannedMessageModuleConfig);

    // Abort if nothing to load
    if (result != LoadFileResult::LOAD_SUCCESS || strlen(cannedMessageModuleConfig.messages) == 0)
        return;

    // Split into individual canned messages
    // These are concatenated when stored in flash, using '|' as a delimiter
    std::string s;
    for (char c : cannedMessageModuleConfig.messages) { // Character by character

        // If found end of a string
        if (c == '|' || c == '\0') {
            // Copy into the vector (if non-empty)
            if (!s.empty())
                messages.push_back(s);

            // Reset the string builder
            s.clear();

            // End of data, all strings processed
            if (c == 0)
                break;
        }

        // Otherwise, append char (continue building string)
        else
            s.push_back(c);
    }
}

// Handle incoming admin messages
// We get these as an observer of AdminModule
// It's our responsibility to handle setting and getting of canned messages via the client API
// Ordinarily, this would be handled by the CannedMessageModule, but it is bound to Screen.cpp, so not suitable for NicheGraphics
int CannedMessageStore::onAdminMessage(AdminModule_ObserverData *data)
{
    switch (data->request->which_payload_variant) {

    // Client API changing the canned messages
    case meshtastic_AdminMessage_set_canned_message_module_messages_tag:
        handleSet(data->request);
        *data->result = AdminMessageHandleResult::HANDLED;
        break;

    // Client API wants to know the current canned messages
    case meshtastic_AdminMessage_get_canned_message_module_messages_request_tag:
        handleGet(data->response);
        *data->result = AdminMessageHandleResult::HANDLED_WITH_RESPONSE;
        break;

    default:
        break;
    }

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

// Client API changing the canned messages
void CannedMessageStore::handleSet(const meshtastic_AdminMessage *request)
{
    // Copy into the correct struct (for writing to flash as protobuf)
    meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;
    strncpy(cannedMessageModuleConfig.messages, request->set_canned_message_module_messages,
            sizeof(cannedMessageModuleConfig.messages));

    // Ensure the directory exists
#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    // Write to flash
    nodeDB->saveProto(cannedMessagesConfigFile, meshtastic_CannedMessageModuleConfig_size,
                      &meshtastic_CannedMessageModuleConfig_msg, &cannedMessageModuleConfig);

    // Reload from flash, to update the canned messages in RAM
    // (This is a lazy way to handle it)
    load();
}

// Client API wants to know the current canned messages
// We're reconstructing the monolithic canned message string from our copy of the messages in RAM
// Lazy, but more convenient that reloading the monolithic string from flash just for this
void CannedMessageStore::handleGet(meshtastic_AdminMessage *response)
{
    // Merge the canned messages back into the delimited format expected
    std::string merged;
    if (!messages.empty()) { // Don't run if no messages: error on pop_back with size=0
        merged.reserve(201);
        for (std::string &s : messages) {
            merged += s;
            merged += '|';
        }
        merged.pop_back(); // Drop the final delimiter (loop added one too many)
    }

    // Place the data into the response
    // This response is scoped to AdminModule::handleReceivedProtobuf
    // We were passed reference to it via the observable
    response->which_payload_variant = meshtastic_AdminMessage_get_canned_message_module_messages_response_tag;
    strncpy(response->get_canned_message_module_messages_response, merged.c_str(), strlen(merged.c_str()) + 1);
}

#endif