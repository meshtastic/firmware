#pragma once
// The one class AdminModule.h befriends (test seam): exposes the protected/private handlers to the
// native suites and defers persistence so setters exercise in-RAM logic without disk/reboot effects.
#include "MeshTypes.h" // packetPool
#include "modules/AdminModule.h"

class AdminModuleTestShim : public AdminModule
{
  public:
    using AdminModule::checkPassKey; // session-key gate seam (see test_admin_session_repro)
    using AdminModule::handleGetConfig;
    using AdminModule::handleGetModuleConfig;
    using AdminModule::handleSetConfig;
    using AdminModule::handleSetModuleConfig;
    using AdminModule::handleSetOwner;
    using AdminModule::responseIsSolicited; // request/response pairing gate
    using AdminModule::setPassKey;

    // Run the handler as MeshModule::callModules() would for a packet that arrived via `src`.
    // AdminModule trusts from == 0 only when the dispatcher reports a local origin, so the default is
    // RX_SRC_USER: the phone/serial client every from == 0 caller in these suites stands in for.
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *r, RxSource src = RX_SRC_USER)
    {
        return handleWithDispatchContext(&mp, src, mp, r);
    }

    // Run the handler while callModules() is dispatching `dispatched` from `src`. Pass nullptr for a
    // caller outside callModules(), or a different packet for a context that does not belong to `mp`.
    bool handleWithDispatchContext(const meshtastic_MeshPacket *dispatched, RxSource src, const meshtastic_MeshPacket &mp,
                                   meshtastic_AdminMessage *r)
    {
        const meshtastic_MeshPacket *savedRequest = currentRequest;
        const RxSource savedSource = currentRequestSource;
        currentRequest = dispatched;
        currentRequestSource = src;
        const bool handled = AdminModule::handleReceivedProtobuf(mp, r);
        currentRequest = savedRequest;
        currentRequestSource = savedSource;
        return handled;
    }

    // Peek at the reply a handler queued, before drainReply() releases it.
    meshtastic_MeshPacket *reply() { return myReply; }

    // With an "open edit transaction" saveChanges() has no outward effect - no
    // applyConfigChange/saveToDisk/reboot - it only records what the eventual commit should do.
    // Mirrors begin_edit_settings exactly: stamps the clock, so a slow suite can't age the
    // transaction into expiry, and clears the deferred decisions so they start from the same
    // baseline a real transaction gets.
    void deferSaves()
    {
        hasOpenEditTransaction = true;
        editTransactionActivityMs = millis();
        enabled = true;
        setIntervalFromNow(EDIT_TRANSACTION_IDLE_MS);
        deferredShouldReboot = false;
        deferredRadioAffected = false;
    }
    int savedSegments() const { return lastSaveWhatForTest; }

    bool editTransactionOpen() const { return hasOpenEditTransaction; }
    bool editTransactionTimerEnabled() const { return enabled; }
    unsigned long editTransactionTimerInterval() const { return editTransactionTimerIntervalForTest(); }
    // Backdate past the idle window so a test sees an abandoned transaction without waiting it out.
    void ageEditTransaction() { editTransactionActivityMs = millis() - EDIT_TRANSACTION_IDLE_MS - 1; }
    int32_t runTransactionTimer() { return runOnce(); }

    // Setters may allocate an error reply from packetPool; drain it each iteration or the pool leaks.
    void drainReply()
    {
        if (myReply) {
            packetPool.release(myReply);
            myReply = nullptr;
        }
    }
};
