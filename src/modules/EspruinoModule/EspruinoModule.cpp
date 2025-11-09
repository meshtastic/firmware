#include "EspruinoModule.h"
#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_ESPRUINO

#include ".build/espruino_embedded.h"
#include "MeshService.h"
#include "mesh/Router.h"
#include <Arduino.h>

// Include API bootstrap code (debug or minified version)
#ifdef DEBUG
#include ".build/js_api.debug.h"
#else
#include ".build/js_api.debug.h"
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
    
    // Argument type constants for jsvNewNativeFunction
    typedef enum {
        JSWAT_VOID = 0,
        JSWAT_JSVAR = 1,
        JSWAT_INT32 = 4,
    } JsnArgumentType;
    
    // Function declarations
    JsVar *jsvNewNativeFunction(void (*ptr)(void), unsigned short argTypes);
    JsVar *jsvNewTypedArray(JsVarDataArrayBufferViewType type, JsVarInt length);
    JsVar *jsvNewArrayBufferWithData(JsVarInt length, unsigned char *data);
    JsVar *jswrap_typedarray_constructor(JsVarDataArrayBufferViewType type, JsVar *arr, JsVarInt byteOffset, JsVarInt length);
    void jsvArrayBufferIteratorNew(JsvArrayBufferIterator *it, JsVar *arrayBuffer, size_t index);
    void jsvArrayBufferIteratorSetByteValue(JsvArrayBufferIterator *it, char c);
    void jsvArrayBufferIteratorNext(JsvArrayBufferIterator *it);
    void jsvArrayBufferIteratorFree(JsvArrayBufferIterator *it);
    void jsvUnLock2(JsVar *var1, JsVar *var2);
    void jsvUnLock3(JsVar *var1, JsVar *var2, JsVar *var3);
    JsVar *jsvNewArray(JsVar **elements, int elementCount);
    JsVar *jsvNewFromInteger(JsVarInt value);
    JsVar *jsvNewFromFloat(JsVarFloat value);
    JsVarInt jsvGetInteger(const JsVar *v);
    void jsvObjectSetChildAndUnLock(JsVar *parent, const char *name, JsVar *child);
    bool jsvIsString(const JsVar *v);
    size_t jsvGetArrayBufferLength(const JsVar *v);
    JsVarInt jsvArrayBufferIteratorGetIntegerValue(JsvArrayBufferIterator *it);
    
    // Native function: MeshtasticNative.sendMessage({portNum, to, message})
    // Returns: boolean (true if message was sent successfully, false otherwise)
    static JsVar* native_sendMessage(JsVar *paramsObj) {
        ESPRUINO_DEBUG("Native: Sending message...");
        if (!service || !router) {
            ESPRUINO_ERROR("MeshService or Router not available");
            return jsvNewFromBool(false);
        }
        
        // Extract portNum from the params object
        JsVar *portNumVar = jsvObjectGetChild(paramsObj, "portNum", 0);
        int32_t portNum = portNumVar ? (int32_t)jsvGetInteger(portNumVar) : 0;
        jsvUnLock(portNumVar);
        
        // Extract to from the params object
        JsVar *toVar = jsvObjectGetChild(paramsObj, "to", 0);
        int32_t to = toVar ? (int32_t)jsvGetInteger(toVar) : 0;
        jsvUnLock(toVar);
        
        // Extract message from the params object
        JsVar *dataVar = jsvObjectGetChild(paramsObj, "message", 0);
        if (!dataVar) {
            ESPRUINO_ERROR("No message field in params object");
            return jsvNewFromBool(false);
        }
        
        meshtastic_MeshPacket *p = router->allocForSending();
        if (!p) {
            ESPRUINO_ERROR("Failed to allocate packet - router queue may be full");
            jsvUnLock(dataVar);
            return jsvNewFromBool(false);
        }
        
        p->decoded.portnum = (meshtastic_PortNum)portNum;
        p->to = (uint32_t)to;
        // if (p->to != NODENUM_BROADCAST) {
        //     p->pki_encrypted = true;
        //     p->channel = 0;
        // }

        if (jsvIsString(dataVar)) {
            char buf[meshtastic_Constants_DATA_PAYLOAD_LEN];
            size_t len = jsvGetString(dataVar, buf, sizeof(buf));
            p->decoded.payload.size = len;
            memcpy(p->decoded.payload.bytes, buf, len);
        } else {
            size_t len = jsvGetArrayBufferLength(dataVar);
            if (len > meshtastic_Constants_DATA_PAYLOAD_LEN)
                len = meshtastic_Constants_DATA_PAYLOAD_LEN;
            p->decoded.payload.size = len;
            
            JsvArrayBufferIterator it;
            jsvArrayBufferIteratorNew(&it, dataVar, 0);
            for (size_t i = 0; i < len; i++) {
                p->decoded.payload.bytes[i] = (char)jsvArrayBufferIteratorGetIntegerValue(&it);
                jsvArrayBufferIteratorNext(&it);
            }
            jsvArrayBufferIteratorFree(&it);
        }
        
        jsvUnLock(dataVar);
        
        ESPRUINO_DEBUG("Native: Sending msg type=%d to=0x%x (%d bytes)", portNum, p->to, p->decoded.payload.size);
        service->sendToMesh(p, RX_SRC_LOCAL, true);
        return jsvNewFromBool(true);
    }
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
    if (!ejs_create(5000)) {
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
    
    // Mount native functions on Meshtastic API
    mountMeshtasticNativeAPI();
    
    // Run smoke tests to verify everything is working
    runSmokeTests();
    
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

void EspruinoModule::mountMeshtasticNativeAPI()
{
    ESPRUINO_DEBUG("Mounting native Meshtastic API functions...");
    
    JsVar *native = jsvObjectGetChild(jsInstance->root, "MeshtasticNative", 0);
    if (!native) {
        ESPRUINO_ERROR("MeshtasticNative object not found");
        return;
    }
    
    // JSVAR | JSVAR = 1|(1<<3) = 9
    // Function takes a single object parameter: {portNum, to, message}
    // Returns: boolean (true if sent successfully, false otherwise)
    unsigned short argTypes = JSWAT_JSVAR | (JSWAT_JSVAR << 3);
    
    JsVar *sendFunc = jsvNewNativeFunction((void (*)(void))native_sendMessage, argTypes);
    if (sendFunc) {
        jsvObjectSetChildAndUnLock(native, "sendMessage", sendFunc);
        ESPRUINO_DEBUG("  Registered: MeshtasticNative.sendMessage({portNum, to, message}) -> boolean");
    }
    
    jsvUnLock(native);
}

void EspruinoModule::runSmokeTests()
{
    ESPRUINO_DEBUG("Running Meshtastic API smoke tests...");
    
    // Get the Meshtastic object
    JsVar *meshtastic = jsvObjectGetChild(jsInstance->root, "Meshtastic", 0);
    if (!meshtastic) {
        ESPRUINO_WARN("Could not find Meshtastic object for smoke tests");
        return;
    }
    
    // Test 1: Call hello() with no arguments
    JsVar *helloFunc = jsvObjectGetChild(meshtastic, "hello", 0);
    if (helloFunc) {
        ESPRUINO_DEBUG("  Testing Meshtastic.hello()...");
        JsVar *helloResult = ejs_execf(jsInstance, helloFunc, meshtastic, 0, NULL);
        jsvUnLock2(helloFunc, helloResult);
    }
    
    // Test 2: Call echo() with a string argument
    JsVar *echoFunc = jsvObjectGetChild(meshtastic, "echo", 0);
    if (echoFunc) {
        ESPRUINO_DEBUG("  Testing Meshtastic.echo()...");
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
        ESPRUINO_DEBUG("  Testing Meshtastic.ping()...");
        JsVar *pingMsg = jsvNewFromString("test message");
        if (pingMsg) {
            JsVar *args[1] = { pingMsg };
            JsVar *pingResult = ejs_execf(jsInstance, pingFunc, meshtastic, 1, args);
            
            // Extract and log the return value
            if (pingResult) {
                char returnValue[128];
                jsvGetString(pingResult, returnValue, sizeof(returnValue));
                ESPRUINO_INFO("  Meshtastic.ping() returned: %s", returnValue);
                jsvUnLock(pingResult);
            }
            
            jsvUnLock2(pingFunc, pingMsg);
        } else {
            jsvUnLock(pingFunc);
        }
    }
    
    // Test 4: Call sendTextMessage() which queues via MeshtasticNative
    ESPRUINO_DEBUG("  Testing Meshtastic.sendTextMessage()...");
    executeJS("Meshtastic.sendTextMessage(0xEF6B3731, \"Smoke test message.\\nEspruino lives.\")");
    executeJS("Meshtastic.sendTextMessage(0xEF6B3731, \"Multiple messages are supported.\")");

    jsvUnLock(meshtastic);
    ESPRUINO_DEBUG("Smoke tests completed");
}

void EspruinoModule::flushPendingMessages()
{
    if (!jsInstance) return;

    // ESPRUINO_DEBUG("Native: Flushing pending messages...");

    // ESPRUINO_DEBUG("Flushing pending messages...");
    JsVar *native = jsvObjectGetChild(jsInstance->root, "MeshtasticNative", 0);
    if (!native) return;
    
    JsVar *flushFunc = jsvObjectGetChild(native, "flushPendingMessages", 0);
    if (flushFunc) {
        // ESPRUINO_DEBUG("  Calling MeshtasticNative.flushPendingMessages()...");
        JsVar *result = ejs_execf(jsInstance, flushFunc, native, 0, NULL);
        jsvUnLock2(flushFunc, result);
        // ESPRUINO_DEBUG("  MeshtasticNative.flushPendingMessages() returned: %s", result);
    } else {
        ESPRUINO_ERROR("Failed to get MeshtasticNative.flushPendingMessages function");
    }
    
    jsvUnLock(native);
    // ESPRUINO_DEBUG("Native: Flushed pending messages");
}

void EspruinoModule::processEventInJS(const PendingEvent& event)
{
    if (!jsInstance) return;
    
    // Get Meshtastic object and emit function
    JsVar *meshtastic = jsvObjectGetChild(jsInstance->root, "Meshtastic", 0);
    if (!meshtastic) {
        ESPRUINO_ERROR("Failed to get Meshtastic object");
        return;
    }
    
    JsVar *emitFunc = jsvObjectGetChild(meshtastic, "emit", 0);
    if (!emitFunc) {
        ESPRUINO_ERROR("Failed to get Meshtastic.emit function");
        jsvUnLock(meshtastic);
        return;
    }
    
    // Create event name
    char eventName[32];
    snprintf(eventName, sizeof(eventName), "message:%d", event.portNum);
    JsVar *eventNameVar = jsvNewFromString(eventName);
    if (!eventNameVar) {
        ESPRUINO_ERROR("Failed to create event name");
        jsvUnLock2(meshtastic, emitFunc);
        return;
    }
    
    // Create message data
    JsVar *messageData = NULL;
    if (event.isString) {
        messageData = jsvNewFromString(event.payload);
    } else {
        // For binary data, create ArrayBuffer and Uint8Array
        JsVar *arrayBuffer = jsvNewArrayBufferWithData(event.payloadSize, (unsigned char*)event.payload);
        if (arrayBuffer) {
            messageData = jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8, arrayBuffer, 0, 0);
            jsvUnLock(arrayBuffer);
        }
        if (!messageData) {
            messageData = jsvNewTypedArray(ARRAYBUFFERVIEW_UINT8, 0);
        }
    }
    
    if (!messageData) {
        ESPRUINO_ERROR("Failed to create message data");
        jsvUnLock3(meshtastic, emitFunc, eventNameVar);
        return;
    }
    
    JsVar *fromVar = jsvNewFromFloat((double)event.fromNode);
    if (!fromVar) {
        ESPRUINO_ERROR("Failed to create from node variable");
        jsvUnLock3(meshtastic, emitFunc, eventNameVar);
        jsvUnLock(messageData);
        return;
    }
    
    // Call emit
    JsVar *argsArray[2] = { fromVar, messageData };
    JsVar *dataArray = jsvNewArray(argsArray, 2);
    if (!dataArray) {
        ESPRUINO_ERROR("Failed to create data array");
        jsvUnLock3(meshtastic, emitFunc, eventNameVar);
        jsvUnLock2(messageData, fromVar);
        return;
    }
    
    JsVar *args[2] = { eventNameVar, dataArray };
    JsVar *result = ejs_execf(jsInstance, emitFunc, meshtastic, 2, args);
    
    // Check for exceptions
    if (jsInstance->exception) {
        ESPRUINO_ERROR("Exception calling Meshtastic.emit()");
    }
    
    // Cleanup
    jsvUnLock3(meshtastic, emitFunc, eventNameVar);
    jsvUnLock3(messageData, fromVar, dataArray);
    jsvUnLock(result);
}

bool EspruinoModule::executeJS(const char *code)
{
    if (!jsInstance) {
        ESPRUINO_ERROR("Cannot execute JS: Espruino instance not available");
        return false;
    }
    
    ESPRUINO_DEBUG("Executing JavaScript: %s", code);
    
    // Execute the JavaScript code
    // ejs_exec handles exceptions internally and prints them via ejs_print
    JsVar *result = ejs_exec(jsInstance, code, false);
    
    // Just unlock the result - exceptions are handled inside ejs_exec
    jsvUnLock(result);
    
    return true;
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
    
    // Queue event for processing in runOnce() to avoid deep stack nesting
    PendingEvent event;
    event.portNum = mp.decoded.portnum;
    event.fromNode = mp.from;
    event.payloadSize = mp.decoded.payload.size;
    
    if (event.payloadSize > sizeof(event.payload)) {
        event.payloadSize = sizeof(event.payload);
    }
    
    memcpy(event.payload, mp.decoded.payload.bytes, event.payloadSize);
    event.payload[event.payloadSize] = '\0'; // Null-terminate for string messages
    event.isString = (mp.decoded.portnum == 1); // TEXT_MESSAGE_APP
    
    pendingEvents.push_back(event);
    
    ESPRUINO_DEBUG("Queued event for port %d from 0x%x (will process in runOnce)", 
                   event.portNum, event.fromNode);
    
    return ProcessMessage::CONTINUE;
}

int32_t EspruinoModule::runOnce()
{
    if (firstTime) {
        firstTime = false;
        initializeEspruino();
        return 100;
    }
    
    // Process any pending events
    while (!pendingEvents.empty()) {
        PendingEvent event = pendingEvents.front();
        pendingEvents.erase(pendingEvents.begin());
        
        ESPRUINO_DEBUG("Processing queued event: port %d from 0x%x", 
                       event.portNum, event.fromNode);
        
        // Now execute JS (same code that was in handleReceived)
        processEventInJS(event);
    }
    
    // Flush pending outgoing messages
    flushPendingMessages();
    return 100;
}

#endif // MESHTASTIC_INCLUDE_ESPRUINO

