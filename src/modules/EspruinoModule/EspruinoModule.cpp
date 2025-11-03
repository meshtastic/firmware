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
    JsVar *jspeFunctionCall(JsVar *function, JsVar *functionName, JsVar *thisArg, bool isParsing, int argCount, JsVar **argPtr);
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

bool EspruinoModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // Receive all packets destined for this node (broadcasts and direct messages)
    return true;
}

ProcessMessage EspruinoModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Only handle decoded packets
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        return ProcessMessage::CONTINUE;
    }
    
    if (!initialized || !jsInstance) {
        return ProcessMessage::CONTINUE;
    }
    
    // Extract message details
    uint32_t portNum = mp.decoded.portnum;
    uint32_t fromNodeId = mp.from;
    const uint8_t *payload = mp.decoded.payload.bytes;
    size_t payloadLength = mp.decoded.payload.size;
    
    ESPRUINO_DEBUG("Received message from node 0x%x on port %d (%d bytes)", 
              fromNodeId, portNum, payloadLength);
    
    // Create Uint8Array from binary data
    JsVar *uint8array = jsvNewTypedArray(ARRAYBUFFERVIEW_UINT8, payloadLength);
    if (!uint8array) {
        ESPRUINO_ERROR("Failed to create Uint8Array for message event");
        return ProcessMessage::CONTINUE;
    }
    
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, uint8array, 0);
    for (size_t i = 0; i < payloadLength; i++) {
        jsvArrayBufferIteratorSetByteValue(&it, (char)payload[i]);
        jsvArrayBufferIteratorNext(&it);
    }
    jsvArrayBufferIteratorFree(&it);
    
    // Create event data: [fromNode, uint8array]
    JsVar *fromNode = jsvNewFromInteger(fromNodeId);
    JsVar *elements[] = { fromNode, uint8array };
    JsVar *dataArray = jsvNewArray(elements, 2);
    
    if (!dataArray) {
        ESPRUINO_ERROR("Failed to create data array for message event");
        jsvUnLock2(fromNode, uint8array);
        return ProcessMessage::CONTINUE;
    }
    
    // Create event name: "message:<portnum>"
    char eventName[32];
    snprintf(eventName, sizeof(eventName), "message:%d", portNum);
    
    ESPRUINO_DEBUG("Dispatching event: %s", eventName);

    // Dispatch to JavaScript
    emitEvent(eventName, dataArray);
    
    // Clean up
    jsvUnLock3(dataArray, fromNode, uint8array);
    
    // Allow other modules to process this message
    return ProcessMessage::CONTINUE;
}

void EspruinoModule::emitEvent(const char* eventName, JsVar* dataArray)
{
    ESPRUINO_DEBUG("Emitting event: %s", eventName);
    if (!initialized || !jsInstance) {
        ESPRUINO_ERROR("Espruino not initialized or JavaScript instance not created");
        return;
    }
    
    // Create event name variable
    ESPRUINO_DEBUG("Creating event name variable: %s", eventName);
    JsVar *eventNameVar = jsvNewFromString(eventName);
    if (!eventNameVar) {
        ESPRUINO_ERROR("Failed to create event name variable");
        return;
    }
    
    // Get Meshtastic object from global scope
    ESPRUINO_DEBUG("Getting Meshtastic object from global scope");
    JsVar *meshtasticObj = jsvObjectGetChild(jsInstance->root, "Meshtastic", 0);
    if (!meshtasticObj) {
        ESPRUINO_ERROR("Failed to get Meshtastic object from global scope");
        jsvUnLock(eventNameVar);
        return;
    }
    
    // Get emit function from Meshtastic object
    ESPRUINO_DEBUG("Getting emit function from Meshtastic object");
    JsVar *emitFunc = jsvObjectGetChild(meshtasticObj, "emit", 0);
    if (!emitFunc) {
        ESPRUINO_ERROR("Failed to get emit function from Meshtastic object");
        jsvUnLock2(eventNameVar, meshtasticObj);
        return;
    }
    
    // Call Meshtastic.emit(eventName, dataArray)
    ESPRUINO_DEBUG("Calling Meshtastic.emit(eventName, dataArray)");
    JsVar *args[] = { eventNameVar,  };
    JsVar *result = jspeFunctionCall(emitFunc, NULL, meshtasticObj, false, 2, args);
    
    // Clean up
    jsvUnLock(result);
    jsvUnLock(emitFunc);
    jsvUnLock(meshtasticObj);
    jsvUnLock(eventNameVar);
}

#endif // MESHTASTIC_INCLUDE_ESPRUINO

