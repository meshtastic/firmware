#include "configuration.h"
#include "input/InputBroker.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "input/cardKbI2cImpl.h"
#include "input/facesKbI2cImpl.h"
#include "modules/AdminModule.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "modules/RemoteHardwareModule.h"
#include "modules/ReplyModule.h"
#include "modules/RoutingModule.h"
#include "modules/TextMessageModule.h"
#ifndef PORTDUINO
#include "modules/Telemetry/DeviceTelemetry.h"
#include "modules/Telemetry/EnvironmentTelemetry.h"
#endif
#ifndef NO_ESP32
#include "modules/esp32/RangeTestModule.h"
#include "modules/esp32/SerialModule.h"
#include "modules/esp32/StoreForwardModule.h"
#endif

/**
 * Create module instances here.  If you are adding a new module, you must 'new' it here (or somewhere else)
 */
void setupModules()
{
    inputBroker = new InputBroker();
    adminModule = new AdminModule();
    nodeInfoModule = new NodeInfoModule();
    positionModule = new PositionModule();
    textMessageModule = new TextMessageModule();
    
    // Note: if the rest of meshtastic doesn't need to explicitly use your module, you do not need to assign the instance
    // to a global variable.

    new RemoteHardwareModule();
    new ReplyModule();
    rotaryEncoderInterruptImpl1 = new RotaryEncoderInterruptImpl1();
    rotaryEncoderInterruptImpl1->init();
    upDownInterruptImpl1 = new UpDownInterruptImpl1();
    upDownInterruptImpl1->init();
    cardKbI2cImpl = new CardKbI2cImpl();
    cardKbI2cImpl->init();
    facesKbI2cImpl = new FacesKbI2cImpl();
    facesKbI2cImpl->init();
    cannedMessageModule = new CannedMessageModule();
#ifndef PORTDUINO
    new DeviceTelemetryModule();
    new EnvironmentTelemetryModule();
#endif
#ifndef NO_ESP32
    // Only run on an esp32 based device.

    /*
        Maintained by MC Hamster (Jm Casler) jm@casler.org
    */
    new SerialModule();
    new ExternalNotificationModule();

    storeForwardModule = new StoreForwardModule();

    new RangeTestModule();
#endif

    // NOTE! This module must be added LAST because it likes to check for replies from other modules and avoid sending extra acks
    routingModule = new RoutingModule();
}