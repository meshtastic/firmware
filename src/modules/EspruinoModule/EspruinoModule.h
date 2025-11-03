#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"

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
 */
class EspruinoModule : private concurrency::OSThread
{
  private:
    bool firstTime = true;
    bool initialized = false;
    struct ejs *jsInstance = nullptr;

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
    
  private:
    void initializeEspruino();
    void cleanupEspruino();
};

extern EspruinoModule *espruinoModule;

#endif // MESHTASTIC_INCLUDE_ESPRUINO

