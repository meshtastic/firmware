#pragma once

#include "mesh/MeshModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <map>
#include "espruino_embedded.h"

#ifdef MESHTASTIC_INCLUDE_ESPRUINO

// Forward declare the Espruino instance type
struct ejs;

/**
 * EspruinoModule integrates the Espruino JavaScript interpreter into Meshtastic.
 * Uses Espruino's embed mode for a simple, single-file integration.
 * 
 * This is an experimental module controlled by MESHTASTIC_INCLUDE_ESPRUINO flag.
 * 
 * Provides JavaScript API:
 * - console.log() (built into Espruino)
 * - Meshtastic.onTextMessage(callback)
 * - Meshtastic.sendTextMessage(to, message)
 * - Meshtastic.onMessage(portNum, callback)
 * - Port number constants (TEXT_MESSAGE_APP, etc.)
 */
class EspruinoModule : public MeshModule
{
  private:
    bool initialized = false;
    struct ejs *jsInstance = nullptr;
    
    // Initialization thread helper
    class InitThread : public concurrency::OSThread {
        EspruinoModule *parent;
      public:
        InitThread(EspruinoModule *_parent) : OSThread("EspruinoInit"), parent(_parent) {}
      protected:
        int32_t runOnce() override {
            parent->initializeEspruino();
            return disable(); // Run only once
        }
    };
    InitThread *initThread = nullptr;
    
    // Callback storage: map from port number to JavaScript callback function
    std::map<int, JsVar*> messageCallbacks;

  public:
    EspruinoModule();
    virtual ~EspruinoModule();
    
    /**
     * Execute JavaScript code
     * @param code The JavaScript code to execute
     * @return true if execution was successful
     */
    bool executeJS(const char *code);
    
    /**
     * Register a JavaScript callback for a specific port number
     */
    void registerCallback(int portNum, JsVar *callback);
    
    /**
     * Invoke JavaScript callback for an incoming message
     */
    void invokeCallback(int portNum, uint32_t from, const uint8_t *payload, size_t payloadSize);

  protected:
    /** Called to handle a particular incoming message */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    
    /** @return true if we want to receive this packet */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    
  private:
    void initializeEspruino();
    void cleanupEspruino();
    void setupJavaScriptAPI();
};

extern EspruinoModule *espruinoModule;

#endif // MESHTASTIC_INCLUDE_ESPRUINO

