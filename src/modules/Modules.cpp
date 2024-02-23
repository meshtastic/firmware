#include "configuration.h"
#include "input/InputBroker.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/TrackballInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "input/cardKbI2cImpl.h"
#include "input/kbMatrixImpl.h"
#include "modules/AdminModule.h"
#include "modules/AtakPluginModule.h"
#include "modules/CannedMessageModule.h"
#include "modules/DetectionSensorModule.h"
#include "modules/NeighborInfoModule.h"
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "modules/RemoteHardwareModule.h"
#include "modules/ReplyModule.h"
#include "modules/RoutingModule.h"
#include "modules/TextMessageModule.h"
#include "modules/TraceRouteModule.h"
#include "modules/WaypointModule.h"
#if ARCH_PORTDUINO
#include "input/LinuxInputImpl.h"
#endif
#if HAS_TELEMETRY
#include "modules/Telemetry/DeviceTelemetry.h"
#endif
#if HAS_SENSOR
#include "modules/Telemetry/AirQualityTelemetry.h"
#include "modules/Telemetry/EnvironmentTelemetry.h"
#endif
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO)
#include "modules/Telemetry/PowerTelemetry.h"
#endif
#ifdef ARCH_ESP32
#ifdef USE_SX1280
#include "modules/esp32/AudioModule.h"
#endif
#include "modules/esp32/PaxcounterModule.h"
#include "modules/esp32/StoreForwardModule.h"
#endif
#if defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)
#include "modules/ExternalNotificationModule.h"
#include "modules/RangeTestModule.h"
#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2)
#include "modules/SerialModule.h"
#endif
#endif
/**
 * Create module instances here.  If you are adding a new module, you must 'new' it here (or somewhere else)
 */
void setupModules()
{
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER) {
#if HAS_BUTTON || ARCH_PORTDUINO
        inputBroker = new InputBroker();
#endif
        adminModule = new AdminModule();
        nodeInfoModule = new NodeInfoModule();
        positionModule = new PositionModule();
        waypointModule = new WaypointModule();
        textMessageModule = new TextMessageModule();
        traceRouteModule = new TraceRouteModule();
        neighborInfoModule = new NeighborInfoModule();
        detectionSensorModule = new DetectionSensorModule();
        atakPluginModule = new AtakPluginModule();
        // Note: if the rest of meshtastic doesn't need to explicitly use your module, you do not need to assign the instance
        // to a global variable.

        new RemoteHardwareModule();
        new ReplyModule();
#if HAS_BUTTON || ARCH_PORTDUINO
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
#if HAS_TRACKBALL
        trackballInterruptImpl1 = new TrackballInterruptImpl1();
        trackballInterruptImpl1->init();
#endif
#if HAS_SCREEN
        cannedMessageModule = new CannedMessageModule();
#endif
#if HAS_TELEMETRY
        new DeviceTelemetryModule();
#endif
#if HAS_SENSOR
        new EnvironmentTelemetryModule();
        if (nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].first > 0) {
            new AirQualityTelemetryModule();
        }
#endif
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO)
        new PowerTelemetryModule();
#endif
#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&               \
    !defined(CONFIG_IDF_TARGET_ESP32C3)
        new SerialModule();
#endif
#ifdef ARCH_ESP32
        // Only run on an esp32 based device.
#ifdef USE_SX1280
        audioModule = new AudioModule();
#endif
        storeForwardModule = new StoreForwardModule();
        paxcounterModule = new PaxcounterModule();
#endif
#if defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)
        externalNotificationModule = new ExternalNotificationModule();
        new RangeTestModule();
#endif
    } else {
        adminModule = new AdminModule();
#if HAS_TELEMETRY
        new DeviceTelemetryModule();
#endif
        traceRouteModule = new TraceRouteModule();
    }
    // NOTE! This module must be added LAST because it likes to check for replies from other modules and avoid sending extra
    // acks
    routingModule = new RoutingModule();
}