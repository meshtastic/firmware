#include "EspruinoModule.h"
#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_ESPRUINO

#include "espruino_embedded.h"
#include "MeshService.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include <Arduino.h>

// Forward declarations for Espruino internal functions that aren't in espruino_embedded.h
// These exist in espruino_embedded.c but aren't in the public API header
extern "C" {
    // Additional JsVar functions not in the public header
    JsVarInt jsvGetInteger(const JsVar *v);
    JsVar *jsvNewObject();
    JsVar *jsvObjectSetChild(JsVar *obj, const char *name, JsVar *child);
    void jsvArrayPush(JsVar *arr, JsVar *value);
    JsVar *jsvNewNativeFunction(void (*ptr)(void), unsigned short argTypes);
    
    // Argument type constants for jsvNewNativeFunction
    #define JSWAT_VOID 0
    #define JSWAT_JSVAR 1
    #define JSWAT_MASK 7
    
    // execInfo structure to access root object
    typedef struct {
        JsVar *root;
        JsVar *hiddenRoot;
        JsVar *baseScope;
        // ... other fields we don't need
    } JsExecInfo;
    extern JsExecInfo execInfo;
}

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
        
        // Maximum usable log size accounting for "[Espruino] " prefix and null terminator
        const size_t MAX_LOG_SIZE = 150; // Leave room for "[Espruino] " and safety margin
        
        for (size_t i = 0; i < len; i++) {
            char c = str[i];
            
            // If newline, buffer full, or exceeding LOG_INFO limit, flush it
            if (c == '\n' || printBufferPos >= sizeof(printBuffer) - 1 || printBufferPos >= MAX_LOG_SIZE) {
                if (printBufferPos > 0) {
                    printBuffer[printBufferPos] = '\0';
                    LOG_INFO("[Espruino] %s", printBuffer);
                    printBufferPos = 0;
                }
                // Skip the newline character itself (don't add it to next buffer)
                if (c == '\n') {
                    continue;
                }
            }
            
            // Add character to buffer
            printBuffer[printBufferPos++] = c;
        }
    }
}

EspruinoModule *espruinoModule;

// Native functions callable from JavaScript
// These need to be extern "C" for Espruino's native function mechanism
extern "C" {
    // Meshtastic.onMessage(portNum, callback)
    void js_onMessage(JsVar *portNumVar, JsVar *callback) {
        if (!jsvIsNumeric(portNumVar) || !jsvIsFunction(callback)) {
            LOG_ERROR("onMessage: Invalid arguments");
            return;
        }
        
        int portNum = (int)jsvGetInteger(portNumVar);
        espruinoModule->registerCallback(portNum, callback);
        LOG_INFO("Registered callback for port %d", portNum);
    }
    
    // Meshtastic.onTextMessage(callback)
    void js_onTextMessage(JsVar *callback) {
        if (!jsvIsFunction(callback)) {
            LOG_ERROR("onTextMessage: Invalid arguments");
            return;
        }
        
        espruinoModule->registerCallback(meshtastic_PortNum_TEXT_MESSAGE_APP, callback);
        LOG_INFO("Registered text message callback");
    }
    
    // Meshtastic.sendTextMessage(to, message)
    void js_sendTextMessage(JsVar *toVar, JsVar *messageVar) {
        if (!jsvIsNumeric(toVar) || !jsvIsString(messageVar)) {
            LOG_ERROR("sendTextMessage: Invalid arguments");
            return;
        }
        
        uint32_t to = (uint32_t)jsvGetInteger(toVar);
        char message[256];
        jsvGetString(messageVar, message, sizeof(message));
        
        LOG_INFO("Sending text message to 0x%x: %s", to, message);
        
        // Allocate a packet (router is the global routing object)
        meshtastic_MeshPacket *p = router->allocForSending();
        p->to = to;
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        p->want_ack = true;
        
        // Copy message to payload
        size_t len = strlen(message);
        if (len > sizeof(p->decoded.payload.bytes) - 1) {
            len = sizeof(p->decoded.payload.bytes) - 1;
        }
        memcpy(p->decoded.payload.bytes, message, len);
        p->decoded.payload.size = len;
        
        // Send to mesh
        service->sendToMesh(p, RX_SRC_LOCAL, true);
    }
}

EspruinoModule::EspruinoModule() : MeshModule("espruino")
{
    LOG_INFO("EspruinoModule constructor");
    
    // Create initialization thread
    initThread = new InitThread(this);
}

EspruinoModule::~EspruinoModule()
{
    cleanupEspruino();
    
    if (initThread) {
        delete initThread;
        initThread = nullptr;
    }
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
    
    // Set up JavaScript API
    setupJavaScriptAPI();
    
    // Run test script demonstrating the API
    executeJS("console.log('Espruino module initialized');");
    executeJS("console.log('Available constants:', Meshtastic.TEXT_MESSAGE_APP, Meshtastic.POSITION_APP);");
    
    // Set up a text message auto-responder
    executeJS(
        "Meshtastic.onTextMessage(function(from, msg) {"
        "  console.log('Received text from', from.toString(16), ':', msg);"
        "  Meshtastic.sendTextMessage(from, 'Echo: ' + msg);"
        "});"
    );
}

void EspruinoModule::setupJavaScriptAPI()
{
    if (!initialized || !jsInstance) {
        return;
    }
    
    // Activate the instance to work with it
    ejs_set_instance(jsInstance);
    
    // Create Meshtastic global object
    JsVar *meshtastic = jsvNewObject();
    jsvObjectSetChild(execInfo.root, "Meshtastic", meshtastic);
    
    // Add port number constants
    jsvObjectSetChild(meshtastic, "TEXT_MESSAGE_APP", 
        jsvNewFromInteger(meshtastic_PortNum_TEXT_MESSAGE_APP));
    jsvObjectSetChild(meshtastic, "REMOTE_HARDWARE_APP", 
        jsvNewFromInteger(meshtastic_PortNum_REMOTE_HARDWARE_APP));
    jsvObjectSetChild(meshtastic, "POSITION_APP", 
        jsvNewFromInteger(meshtastic_PortNum_POSITION_APP));
    jsvObjectSetChild(meshtastic, "NODEINFO_APP", 
        jsvNewFromInteger(meshtastic_PortNum_NODEINFO_APP));
    jsvObjectSetChild(meshtastic, "ROUTING_APP", 
        jsvNewFromInteger(meshtastic_PortNum_ROUTING_APP));
    jsvObjectSetChild(meshtastic, "ADMIN_APP", 
        jsvNewFromInteger(meshtastic_PortNum_ADMIN_APP));
    
    // Add native functions
    // Note: These are created as JavaScript functions that call our C functions
    JsVar *onMessage = jsvNewNativeFunction((void (*)(void))js_onMessage, JSWAT_JSVAR | (JSWAT_JSVAR << 2));
    jsvObjectSetChild(meshtastic, "onMessage", onMessage);
    jsvUnLock(onMessage);
    
    JsVar *onTextMessage = jsvNewNativeFunction((void (*)(void))js_onTextMessage, JSWAT_JSVAR);
    jsvObjectSetChild(meshtastic, "onTextMessage", onTextMessage);
    jsvUnLock(onTextMessage);
    
    JsVar *sendTextMessage = jsvNewNativeFunction((void (*)(void))js_sendTextMessage, JSWAT_JSVAR | (JSWAT_JSVAR << 2));
    jsvObjectSetChild(meshtastic, "sendTextMessage", sendTextMessage);
    jsvUnLock(sendTextMessage);
    
    jsvUnLock(meshtastic);
    
    // Deactivate the instance
    ejs_unset_instance();
    
    LOG_INFO("JavaScript API setup complete");
}

void EspruinoModule::cleanupEspruino()
{
    if (!initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up Espruino...");
    
    // Activate instance to clean up callbacks
    if (jsInstance) {
        ejs_set_instance(jsInstance);
        
        // Unlock all stored callbacks
        for (auto &pair : messageCallbacks) {
            jsvUnLock(pair.second);
        }
        messageCallbacks.clear();
        
        ejs_unset_instance();
        
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

void EspruinoModule::registerCallback(int portNum, JsVar *callback)
{
    if (!initialized || !jsInstance) {
        LOG_ERROR("Cannot register callback: Espruino not initialized");
        return;
    }
    
    // Activate instance
    ejs_set_instance(jsInstance);
    
    // If we already have a callback for this port, unlock the old one
    auto it = messageCallbacks.find(portNum);
    if (it != messageCallbacks.end()) {
        jsvUnLock(it->second);
    }
    
    // Lock the new callback and store it
    messageCallbacks[portNum] = jsvLockAgainSafe(callback);
    
    ejs_unset_instance();
}

void EspruinoModule::invokeCallback(int portNum, uint32_t from, const uint8_t *payload, size_t payloadSize)
{
    if (!initialized || !jsInstance) {
        return;
    }
    
    // Check if we have a callback for this port
    auto it = messageCallbacks.find(portNum);
    if (it == messageCallbacks.end()) {
        return;
    }
    
    JsVar *callback = it->second;
    if (!callback) {
        return;
    }
    
    LOG_DEBUG("Invoking JS callback for port %d from 0x%x", portNum, from);
    
    // Activate instance
    ejs_set_instance(jsInstance);
    
    // Create arguments
    JsVar *args[2];
    args[0] = jsvNewFromInteger(from);
    
    // For text messages, convert payload to string; for others, pass as array
    if (portNum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        char *strBuf = (char*)malloc(payloadSize + 1);
        if (strBuf) {
            memcpy(strBuf, payload, payloadSize);
            strBuf[payloadSize] = '\0';
            args[1] = jsvNewFromString(strBuf);
            free(strBuf);
        } else {
            args[1] = jsvNewFromString("");
        }
    } else {
        // For non-text messages, create a byte array
        args[1] = jsvNewEmptyArray();
        for (size_t i = 0; i < payloadSize; i++) {
            JsVar *byte = jsvNewFromInteger(payload[i]);
            jsvArrayPush(args[1], byte);
            jsvUnLock(byte);
        }
    }
    
    // Call the callback
    JsVar *result = ejs_execf(jsInstance, callback, NULL, 2, args);
    
    // Clean up
    jsvUnLock(args[0]);
    jsvUnLock(args[1]);
    jsvUnLock(result);
    
    ejs_unset_instance();
}

ProcessMessage EspruinoModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Check if we have a callback registered for this port
    int portNum = mp.decoded.portnum;
    auto it = messageCallbacks.find(portNum);
    
    if (it != messageCallbacks.end()) {
        // Invoke the JavaScript callback
        invokeCallback(portNum, mp.from, mp.decoded.payload.bytes, mp.decoded.payload.size);
    }
    
    return ProcessMessage::CONTINUE; // Let other modules see this too
}

bool EspruinoModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // We want packets for any port that has a registered callback
    int portNum = p->decoded.portnum;
    return messageCallbacks.find(portNum) != messageCallbacks.end();
}

#endif // MESHTASTIC_INCLUDE_ESPRUINO
