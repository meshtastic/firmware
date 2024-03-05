#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
#include "input/InputBroker.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/TrackballInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "input/cardKbI2cImpl.h"
#include "input/kbMatrixImpl.h"
#endif
#include "modules/AdminModule.h"
#if !MESHTASTIC_EXCLUDE_ATAK
#include "modules/AtakPluginModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_CANNEDMESSAGES
#include "modules/CannedMessageModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_DETECTIONSENSOR
#include "modules/DetectionSensorModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_NEIGHBORINFO
#include "modules/NeighborInfoModule.h"
#endif
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#if !MESHTASTIC_EXCLUDE_REMOTEHARDWARE
#include "modules/RemoteHardwareModule.h"
#endif
#include "modules/RoutingModule.h"
#include "modules/TextMessageModule.h"
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
#include "modules/TraceRouteModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_WAYPOINT
#include "modules/WaypointModule.h"
#endif
#if ARCH_PORTDUINO
#include "input/LinuxInputImpl.h"
#endif
#if HAS_TELEMETRY
#include "modules/Telemetry/DeviceTelemetry.h"
#endif
#if HAS_SENSOR && !EXCLUDE_ENVIRONMENTAL_SENSOR
#include "modules/Telemetry/AirQualityTelemetry.h"
#include "modules/Telemetry/EnvironmentTelemetry.h"
#endif
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO) && !EXCLUDE_POWER_TELEMETRY
#include "modules/Telemetry/PowerTelemetry.h"
#endif
#ifdef ARCH_ESP32
#if defined(USE_SX1280) && !MESHTASTIC_EXCLUDE_AUDIO
#include "modules/esp32/AudioModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_PAXCOUNTER
#include "modules/esp32/PaxcounterModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
#include "modules/esp32/StoreForwardModule.h"
#endif
#endif
#if defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)
#if !MESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION
#include "modules/ExternalNotificationModule.h"
#endif
#if !MESHTASTIC_EXCLUDE_RANGETEST
#include "modules/RangeTestModule.h"
#endif
#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2)
#if !MESHTASTIC_EXCLUDE_SERIAL
#include "modules/SerialModule.h"
#endif
#endif
#endif
/**
 * Create module instances here.  If you are adding a new module, you must 'new' it here (or somewhere else)
 */
void setupModules()
{
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER) {
#if (HAS_BUTTON || ARCH_PORTDUINO) && !MESHTASTIC_EXCLUDE_INPUTBROKER
        inputBroker = new InputBroker();
#endif
        adminModule = new AdminModule();
        nodeInfoModule = new NodeInfoModule();
        positionModule = new PositionModule();
#if !MESHTASTIC_EXCLUDE_WAYPOINT
        waypointModule = new WaypointModule();
#endif
        textMessageModule = new TextMessageModule();
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
        traceRouteModule = new TraceRouteModule();
#endif
#if !MESHTASTIC_EXCLUDE_NEIGHBORINFO
        neighborInfoModule = new NeighborInfoModule();
#endif
#if !MESHTASTIC_EXCLUDE_DETECTIONSENSOR
        detectionSensorModule = new DetectionSensorModule();
#endif
#if !MESHTASTIC_EXCLUDE_ATAK
        atakPluginModule = new AtakPluginModule();
#endif
        // Note: if the rest of meshtastic doesn't need to explicitly use your module, you do not need to assign the instance
        // to a global variable.

#if !MESHTASTIC_EXCLUDE_REMOTEHARDWARE
        new RemoteHardwareModule();
#endif
        // Example: Put your module here
        // new ReplyModule();
#if (HAS_BUTTON || ARCH_PORTDUINO) && !MESHTASTIC_EXCLUDE_INPUTBROKER
        rotaryEncoderInterruptImpl1 = new RotaryEncoderInterruptImpl1();
        if (!rotaryEncoderInterruptImpl1->init()) {
            delete rotaryEncoderInterruptImpl1;
            rotaryEncoderInterruptImpl1 = nullptr;
        }
        upDownInterruptImpl1 = new UpDownInterruptImpl1();
        if (!upDownInterruptImpl1->init()) {
            delete upDownInterruptImpl1;
            upDownInterruptImpl1 = nullptr;
        }
        cardKbI2cImpl = new CardKbI2cImpl();
        cardKbI2cImpl->init();
#ifdef INPUTBROKER_MATRIX_TYPE
        kbMatrixImpl = new KbMatrixImpl();
        kbMatrixImpl->init();
#endif // INPUTBROKER_MATRIX_TYPE
#endif // HAS_BUTTON
#if ARCH_PORTDUINO
        aLinuxInputImpl = new LinuxInputImpl();
        aLinuxInputImpl->init();
#endif
#if HAS_TRACKBALL && !MESHTASTIC_EXCLUDE_INPUTBROKER
        trackballInterruptImpl1 = new TrackballInterruptImpl1();
        trackballInterruptImpl1->init();
#endif
#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_CANNEDMESSAGES
        cannedMessageModule = new CannedMessageModule();
#endif
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO)
        new DeviceTelemetryModule();
#endif
#if HAS_SENSOR && !EXCLUDE_ENVIRONMENTAL_SENSOR
        new EnvironmentTelemetryModule();
        if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].first > 0) {
            new AirQualityTelemetryModule();
        }
#endif
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO) && !EXCLUDE_POWER_TELEMETRY
        new PowerTelemetryModule();
#endif
#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&               \
    !defined(CONFIG_IDF_TARGET_ESP32C3)
#if !MESHTASTIC_EXCLUDE_SERIAL
        new SerialModule();
#endif
#endif
#ifdef ARCH_ESP32
        // Only run on an esp32 based device.
#if defined(USE_SX1280) && !MESHTASTIC_EXCLUDE_AUDIO
        audioModule = new AudioModule();
#endif
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
        storeForwardModule = new StoreForwardModule();
#endif
#if !MESHTASTIC_EXCLUDE_PAXCOUNTER
        paxcounterModule = new PaxcounterModule();
#endif
#endif
#if defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)
#if !MESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION
        externalNotificationModule = new ExternalNotificationModule();
#endif
#if !MESHTASTIC_EXCLUDE_RANGETEST
        new RangeTestModule();
#endif
#endif
    } else {
        adminModule = new AdminModule();
#if HAS_TELEMETRY
        new DeviceTelemetryModule();
#endif
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
        traceRouteModule = new TraceRouteModule();
#endif
    }
    // NOTE! This module must be added LAST because it likes to check for replies from other modules and avoid sending extra
    // acks
    routingModule = new RoutingModule();
}