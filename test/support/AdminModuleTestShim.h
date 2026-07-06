#pragma once
// The one class AdminModule.h befriends (test seam): exposes the protected/private handlers to the
// native suites and defers persistence so setters exercise in-RAM logic without disk/reboot effects.
#include "MeshTypes.h" // packetPool
#include "modules/AdminModule.h"

class AdminModuleTestShim : public AdminModule
{
  public:
    using AdminModule::handleReceivedProtobuf;
    using AdminModule::handleSetConfig;
    using AdminModule::handleSetModuleConfig;

    // With an "open edit transaction" saveChanges() is a pure no-op: no reloadConfig/saveToDisk/reboot.
    void deferSaves() { hasOpenEditTransaction = true; }

    // Setters may allocate an error reply from packetPool; drain it each iteration or the pool leaks.
    void drainReply()
    {
        if (myReply) {
            packetPool.release(myReply);
            myReply = nullptr;
        }
    }
};
