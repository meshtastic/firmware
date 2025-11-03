#include "EspruinoModule.h"
#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_ESPRUINO

#include ".build/espruino_embedded.h"
#include "MeshService.h"
#include <Arduino.h>

// Include API bootstrap code (debug or minified version)
#ifdef DEBUG
#include ".build/js_api.debug.h"
#else
#include ".build/js_api.min.h"
#endif

#define ESPRUINO_DEBUG(...) LOG_DEBUG("[EspruinoModule]: " __VA_ARGS__)
#define ESPRUINO_INFO(...) LOG_INFO("[EspruinoModule]: " __VA_ARGS__)
#define ESPRUINO_WARN(...) LOG_WARN("[EspruinoModule]: " __VA_ARGS__)
#define ESPRUINO_ERROR(...) LOG_ERROR("[EspruinoModule]: " __VA_ARGS__)
#define ESPRUINO_CRIT(...) LOG_CRIT("[EspruinoModule]: " __VA_ARGS__)
#define ESPRUINO_TRACE(...) LOG_TRACE("[EspruinoModule]: " __VA_ARGS__)

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
                    ESPRUINO_INFO("%s", printBuffer);
                    printBufferPos = 0;
                }
            } else {
                // Add character to buffer
                printBuffer[printBufferPos++] = c;
            }
        }
    }
    
    // Forward declarations for Espruino functions not in espruino_embedded.h
    // ArrayBuffer view types
    typedef enum {
        ARRAYBUFFERVIEW_UINT8 = 1,
        ARRAYBUFFERVIEW_INT8 = 1 | 16,
        ARRAYBUFFERVIEW_UINT16 = 2,
        ARRAYBUFFERVIEW_INT16 = 2 | 16,
        ARRAYBUFFERVIEW_UINT32 = 4,
        ARRAYBUFFERVIEW_INT32 = 4 | 16,
        ARRAYBUFFERVIEW_FLOAT32 = 4 | 32,
        ARRAYBUFFERVIEW_FLOAT64 = 8 | 32,
    } __attribute__ ((__packed__)) JsVarDataArrayBufferViewType;
    
    // String iterator
    typedef struct JsvStringIterator {
        size_t charIdx;
        size_t charsInVar;
        size_t varIndex;
        JsVar *var;
        char *ptr;
    } JsvStringIterator;
    
    // ArrayBuffer iterator
    typedef struct JsvArrayBufferIterator {
        JsvStringIterator it;
        JsVarDataArrayBufferViewType type;
        size_t byteLength;
        size_t byteOffset;
        size_t index;
        bool hasAccessedElement;
    } JsvArrayBufferIterator;
    
    // Function declarations
    JsVar *jsvNewTypedArray(JsVarDataArrayBufferViewType type, JsVarInt length);
    void jsvArrayBufferIteratorNew(JsvArrayBufferIterator *it, JsVar *arrayBuffer, size_t index);
    void jsvArrayBufferIteratorSetByteValue(JsvArrayBufferIterator *it, char c);
    void jsvArrayBufferIteratorNext(JsvArrayBufferIterator *it);
    void jsvArrayBufferIteratorFree(JsvArrayBufferIterator *it);
    void jsvUnLock2(JsVar *var1, JsVar *var2);
    void jsvUnLock3(JsVar *var1, JsVar *var2, JsVar *var3);
    JsVar *jsvNewArray(JsVar **elements, int elementCount);
}

EspruinoModule *espruinoModule;

EspruinoModule::EspruinoModule() : MeshModule("espruino"), concurrency::OSThread("EspruinoModule")
{
    ESPRUINO_INFO("Constructor");
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
    
    ESPRUINO_DEBUG("Initializing Espruino JavaScript interpreter...");
    
    // Create the Espruino interpreter with 1000 variables
    // Adjust this number based on available memory
    if (!ejs_create(1000)) {
        ESPRUINO_ERROR("Failed to create Espruino interpreter");
        return;
    }
    
    // Create a JavaScript instance (no arguments needed)
    jsInstance = ejs_create_instance();
    if (!jsInstance) {
        ESPRUINO_ERROR("Failed to create Espruino instance");
        ejs_destroy();
        return;
    }
    
    // Execute bootstrap JavaScript to initialize Meshtastic API
    ESPRUINO_DEBUG("Loading Meshtastic API bootstrap...");
    JsVar *bootstrapResult = ejs_exec(jsInstance, JS_API_BOOTSTRAP, true);
    
    // Check for exceptions during bootstrap
    if (jsInstance->exception) {
        ESPRUINO_ERROR("Failed to execute API bootstrap!");
        
        // Try to extract and log the error message
        JsVar *exception = jsvLockAgainSafe(jsInstance->exception);
        if (exception) {
            char errorMsg[256];
            jsvGetString(exception, errorMsg, sizeof(errorMsg));
            ESPRUINO_ERROR("Bootstrap error: %s", errorMsg);
            jsvUnLock(exception);
        }
        
        // Clean up and fail
        jsvUnLock(bootstrapResult);
        ejs_destroy_instance(jsInstance);
        jsInstance = nullptr;
        ejs_destroy();
        return;
    }
    
    jsvUnLock(bootstrapResult);
    
    // Run smoke tests to verify Meshtastic API is working
    ESPRUINO_DEBUG("Running Meshtastic API smoke tests...");
    
    // Get the Meshtastic object
    JsVar *meshtastic = jsvObjectGetChild(jsInstance->root, "Meshtastic", 0);
    if (meshtastic) {
        // Test 1: Call hello() with no arguments
        JsVar *helloFunc = jsvObjectGetChild(meshtastic, "hello", 0);
        if (helloFunc) {
            ESPRUINO_DEBUG("Testing Meshtastic.hello()...");
            JsVar *helloResult = ejs_execf(jsInstance, helloFunc, meshtastic, 0, NULL);
            jsvUnLock2(helloFunc, helloResult);
        }
        
        // Test 2: Call echo() with a string argument
        JsVar *echoFunc = jsvObjectGetChild(meshtastic, "echo", 0);
        if (echoFunc) {
            ESPRUINO_DEBUG("Testing Meshtastic.echo()...");
            JsVar *echoMsg = jsvNewFromString("Smoke test: echo working!");
            if (echoMsg) {
                JsVar *args[1] = { echoMsg };
                JsVar *echoResult = ejs_execf(jsInstance, echoFunc, meshtastic, 1, args);
                jsvUnLock3(echoFunc, echoMsg, echoResult);
            } else {
                jsvUnLock(echoFunc);
            }
        }
        
        // Test 3: Call ping() and get return value
        JsVar *pingFunc = jsvObjectGetChild(meshtastic, "ping", 0);
        if (pingFunc) {
            ESPRUINO_DEBUG("Testing Meshtastic.ping()...");
            JsVar *pingMsg = jsvNewFromString("test message");
            if (pingMsg) {
                JsVar *args[1] = { pingMsg };
                JsVar *pingResult = ejs_execf(jsInstance, pingFunc, meshtastic, 1, args);
                
                // Extract and log the return value
                if (pingResult) {
                    char returnValue[128];
                    jsvGetString(pingResult, returnValue, sizeof(returnValue));
                    ESPRUINO_INFO("Meshtastic.ping() returned: %s", returnValue);
                    jsvUnLock(pingResult);
                }
                
                jsvUnLock2(pingFunc, pingMsg);
            } else {
                jsvUnLock(pingFunc);
            }
        }
        
        jsvUnLock(meshtastic);
    } else {
        ESPRUINO_WARN("Could not find Meshtastic object for smoke tests");
    }
    
    initialized = true;
    ESPRUINO_DEBUG("Espruino initialized successfully with Meshtastic API");
}

void EspruinoModule::cleanupEspruino()
{
    if (!initialized) {
        return;
    }
    
    ESPRUINO_INFO("Cleaning up Espruino...");
    
    if (jsInstance) {
        ejs_destroy_instance(jsInstance);
        jsInstance = nullptr;
    }
    
    ejs_destroy();
    initialized = false;
}

bool EspruinoModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // Only process packets if Espruino is initialized
    return initialized;
}

ProcessMessage EspruinoModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!initialized || !jsInstance) {
        return ProcessMessage::CONTINUE;
    }
    
    // Get the port number from the decoded packet
    uint32_t portNum = mp.decoded.portnum;
    
    ESPRUINO_DEBUG("Received message on port %d", portNum);
    
    // TODO: Emit message events to JavaScript handlers
    // Will implement message dispatching to JS in future commits
    
    return ProcessMessage::CONTINUE;
}

int32_t EspruinoModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        
        // Initialize Espruino on first run
        initializeEspruino();
        
        // We only need to run once for initialization
        return disable();
    }
    
    return 100; // Run again in 100ms if needed
}

#endif // MESHTASTIC_INCLUDE_ESPRUINO

