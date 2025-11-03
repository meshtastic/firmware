#include "EspruinoModule.h"
#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_ESPRUINO

#include "espruino_embedded.h"
#include <Arduino.h>

// Required by Espruino embed API
extern "C" {
    // Return microseconds since epoch (or any reference point)
    uint64_t ejs_get_microseconds() {
        return (uint64_t)millis() * 1000; // Convert milliseconds to microseconds
    }
    
    // Buffer for accumulating print output (Espruino calls this char-by-char)
    static char printBuffer[256];
    static size_t printBufferPos = 0;
    
    // Print string to console - route to Meshtastic LOG_INFO
    void ejs_print(const char *str) {
        size_t len = strlen(str);
        
        for (size_t i = 0; i < len; i++) {
            char c = str[i];
            
            // If newline or buffer full, flush it
            if (c == '\n' || printBufferPos >= sizeof(printBuffer) - 1) {
                if (printBufferPos > 0) {
                    printBuffer[printBufferPos] = '\0';
                    LOG_INFO("[Espruino] %s", printBuffer);
                    printBufferPos = 0;
                }
            } else {
                // Add character to buffer
                printBuffer[printBufferPos++] = c;
            }
        }
    }
}

EspruinoModule *espruinoModule;

EspruinoModule::EspruinoModule() : concurrency::OSThread("EspruinoModule")
{
    LOG_INFO("EspruinoModule constructor");
}

EspruinoModule::~EspruinoModule()
{
    cleanupEspruino();
}

void EspruinoModule::initializeEspruino()
{
    if (initialized) {
        return;
    }
    
    LOG_INFO("Initializing Espruino JavaScript interpreter...");
    
    // Create the Espruino interpreter with 1000 variables
    // Adjust this number based on available memory
    if (!ejs_create(1000)) {
        LOG_ERROR("Failed to create Espruino interpreter");
        return;
    }
    
    // Create a JavaScript instance (no arguments needed)
    jsInstance = ejs_create_instance();
    if (!jsInstance) {
        LOG_ERROR("Failed to create Espruino instance");
        ejs_destroy();
        return;
    }
    
    initialized = true;
    LOG_INFO("Espruino initialized successfully");
}

void EspruinoModule::cleanupEspruino()
{
    if (!initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up Espruino...");
    
    if (jsInstance) {
        ejs_destroy_instance(jsInstance);
        jsInstance = nullptr;
    }
    
    ejs_destroy();
    initialized = false;
}

bool EspruinoModule::executeJS(const char *code)
{
    if (!initialized || !jsInstance) {
        LOG_ERROR("Cannot execute JS: Espruino not initialized");
        return false;
    }
    
    LOG_DEBUG("Executing JavaScript: %s", code);
    
    // Execute the JavaScript code
    // ejs_exec handles exceptions internally and prints them via ejs_print
    JsVar *result = ejs_exec(jsInstance, code, false);
    
    // Just unlock the result - exceptions are handled inside ejs_exec
    jsvUnLock(result);
    
    return true;
}

int32_t EspruinoModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        
        // Initialize Espruino on first run
        initializeEspruino();
        
        // Run test scripts
        if (initialized) {
            executeJS("console.log('Espruino module initialized');");
            executeJS("console.log('JavaScript test:', 42, true, {foo: 'bar'});");
            executeJS("console.log('Math test: 2 + 2 =', 2 + 2);");
            executeJS("throw new Error('Test error');");
        }
        
        // We only need to run once for initialization
        return disable();
    }
    
    return 100; // Run again in 100ms if needed
}

#endif // MESHTASTIC_INCLUDE_ESPRUINO

