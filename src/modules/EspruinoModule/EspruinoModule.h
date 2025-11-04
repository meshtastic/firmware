#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/MeshModule.h"
#include ".build/espruino_embedded.h"
#include <vector>

#ifdef MESHTASTIC_INCLUDE_ESPRUINO

// Forward declare the Espruino instance type
struct ejs;

/**
 * EspruinoModule integrates the Espruino JavaScript interpreter into Meshtastic.
 * Uses Espruino's embed mode for a simple, single-file integration.
 * 
 * This is an experimental module controlled by MESHTASTIC_INCLUDE_ESPRUINO flag.
 * 
 * Currently provides:
 * - console.log() (already built into Espruino)
 * - Full JavaScript ES5 interpreter
 * - Meshtastic message dispatching to JavaScript via E.emit('message:<portnum>', from, payload)
 */
class EspruinoModule : public MeshModule, private concurrency::OSThread
{
  private:
    bool firstTime = true;
    bool initialized = false;
    struct ejs *jsInstance = nullptr;
    
    // Queue for deferred message processing to avoid stack issues
    struct PendingEvent {
        uint32_t portNum;
        uint32_t fromNode;
        char payload[256];
        size_t payloadSize;
        bool isString;
    };
    std::vector<PendingEvent> pendingEvents;

  public:
    EspruinoModule();
    virtual ~EspruinoModule();
    
    /**
     * Execute JavaScript code
     * @param code The JavaScript code to execute
     * @return true if execution was successful
     */
    bool executeJS(const char *code);
    
  protected:
    virtual int32_t runOnce() override;
    
    // MeshModule methods
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    
  private:
    void initializeEspruino();
    void cleanupEspruino();
    void mountMeshtasticNativeAPI();
    void runSmokeTests();
    void flushPendingMessages();
    void processEventInJS(const PendingEvent& event);
};

extern EspruinoModule *espruinoModule;

#endif // MESHTASTIC_INCLUDE_ESPRUINO

