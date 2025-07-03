#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

/*

Re-usable NicheGraphics tool

Makes canned message data accessible to any NicheGraphics UI.
 - handles loading & parsing from flash
 - handles the admin messages for setting & getting canned messages via client API (phone apps, etc)

The original CannedMessageModule class is bound to Screen.cpp,
making it incompatible with the NicheGraphics framework, which suppresses Screen.cpp

This implementation aims to be self-contained.
The necessary interaction with the AdminModule is done as an observer.

*/

#pragma once

#include "configuration.h"

#include "modules/AdminModule.h"

namespace NicheGraphics
{

class CannedMessageStore
{
  public:
    static CannedMessageStore *getInstance(); // Create or get the singleton instance
    const std::string &at(uint8_t i);         // Get canned message at index
    uint8_t size();                           // Get total number of canned messages

    int onAdminMessage(AdminModule_ObserverData *data); // Handle incoming admin messages

  private:
    CannedMessageStore(); // Constructor made private: force use of CannedMessageStore::instance()

    void load(); // Load from flash, and parse

    void handleSet(const meshtastic_AdminMessage *request); // Client API changing the canned messages
    void handleGet(meshtastic_AdminMessage *response);      // Client API wants to know current canned messages

    std::vector<std::string> messages;

    // Get notified of incoming admin messages, to get / set canned messages
    CallbackObserver<CannedMessageStore, AdminModule_ObserverData *> adminMessageObserver =
        CallbackObserver<CannedMessageStore, AdminModule_ObserverData *>(this, &CannedMessageStore::onAdminMessage);
};

}; // namespace NicheGraphics

#endif