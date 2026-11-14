#include "UptimeClock.h"
#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "EnvironmentTelemetry.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Power.h"
#include "PowerFSM.h"
#include "Router.h"
#include "TelemetryPublish.h"
#include "TelemetrySensorPower.h"
#include "TransmitHistory.h"
#include "UnitConversions.h"
#include "buzz.h"
#include "gps/RTC.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "modules/ExternalNotificationModule.h"
#include "sleep.h"
#include "target_specific.h"
#include <OLEDDisplay.h>

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL

// Sensors
#include "Sensor/CGRadSensSensor.h"
#include "Sensor/RCWL9620Sensor.h"
#include "Sensor/nullSensor.h"

namespace graphics
{
extern void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y, const char *titleStr, bool force_no_invert,
                             bool show_date);
}
#if __has_include(<Adafruit_AHTX0.h>)
#include "Sensor/AHT10.h"
#endif

#if __has_include(<Adafruit_BME280.h>)
#include "Sensor/BME280Sensor.h"
#endif

#if __has_include(<Adafruit_BMP085.h>)
#include "Sensor/BMP085Sensor.h"
#endif

#if __has_include(<Adafruit_BMP280.h>)
#include "Sensor/BMP280Sensor.h"
#endif

#if __has_include(<Adafruit_LTR390.h>)
#include "Sensor/LTR390UVSensor.h"
#endif

#if __has_include(<Adafruit_BME680.h>)
#include "Sensor/BME680Sensor.h"
#endif

#if __has_include(<Adafruit_DPS310.h>)
#include "Sensor/DPS310Sensor.h"
#endif

#if __has_include(<Adafruit_MCP9808.h>)
#include "Sensor/MCP9808Sensor.h"
#endif

#if __has_include(<Adafruit_LPS2X.h>)
#include "Sensor/LPS22HBSensor.h"
#endif

#if __has_include("RAK12035_SoilMoisture.h") && defined(RAK_4631) && RAK_4631 == 1
#include "Sensor/RAK12035Sensor.h"
#endif

#if __has_include(<Adafruit_VEML7700.h>)
#include "Sensor/VEML7700Sensor.h"
#endif

#if __has_include(<Adafruit_TSL2591.h>)
#include "Sensor/TSL2591Sensor.h"
#endif

#if __has_include(<ClosedCube_OPT3001.h>)
#include "Sensor/OPT3001Sensor.h"
#endif

#if __has_include(<SHTSensor.h>)
#include "Sensor/SHTXXSensor.h"
#endif

#if __has_include(<SparkFun_MLX90632_Arduino_Library.h>)
#include "Sensor/MLX90632Sensor.h"
#endif

#if __has_include(<DFRobot_LarkWeatherStation.h>)
#include "Sensor/DFRobotLarkSensor.h"
#endif

#if __has_include(<DFRobot_RainfallSensor.h>)
#include "Sensor/DFRobotGravitySensor.h"
#endif

#if __has_include(<SparkFun_AS3935.h>)
#include "Sensor/AS3935Sensor.h"
#endif

#if __has_include(<SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>)
#include "Sensor/NAU7802Sensor.h"
#endif

#if __has_include(<Adafruit_BMP3XX.h>)
#include "Sensor/BMP3XXSensor.h"
#endif

#if __has_include(<Adafruit_PCT2075.h>)
#include "Sensor/PCT2075Sensor.h"
#endif

#endif
#ifdef T1000X_SENSOR_EN
#include "Sensor/T1000xSensor.h"
#endif

#if ARCH_PORTDUINO
#include "Sensor/CommandSensor.h"
#endif

#if __has_include(<Adafruit_SPA06_003.h>)
#include "Sensor/SPA06Sensor.h"
#endif

#if __has_include(<Adafruit_TSL2561_U.h>)
#include "Sensor/TSL2561Sensor.h"
#endif

#if __has_include(<BH1750_WE.h>)
#include "Sensor/BH1750Sensor.h"
#endif

#if __has_include(<Adafruit_ADS1X15.h>)
#include "Sensor/ADS1X15Sensor.h"
#endif

#if __has_include(<Adafruit_DS248x.h>)
#include "Sensor/DS248XSensor.h"
#endif

#ifdef HAS_RAKHUB
#include "Sensor/RAKSensorHub.h" // env:rak_wismesh_sensorhub only
#endif

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#include "FSCommon.h"
#include "Sensor/AddI2CSensorTemplate.h"
// SENSOR role keeps its readings in a file, so a batch survives reboot and deep sleep. Not on STM32: tiny flash.
#if !defined(ENVIRONMENT_TELEMETRY_HISTORY_PATH) && defined(FSCom) && !defined(ARCH_STM32)
#define ENVIRONMENT_TELEMETRY_HISTORY_PATH "/prefs/env_history.bin"
#endif
#ifdef ENVIRONMENT_TELEMETRY_HISTORY_PATH
#include "FileTelemetryStore.h"
#endif
#include "graphics/ScreenFonts.h"
#include <Throttle.h>

EnvironmentTelemetryModule *environmentTelemetryModule = nullptr;

#if ARCH_PORTDUINO
// Also in `sensors`, but read last in getEnvironmentTelemetry() so a configured command wins a shared field.
static TelemetrySensor *commandSensor = nullptr;
#endif

namespace
{
EnvironmentTelemetryModule::DisplaySource gDisplaySource = EnvironmentTelemetryModule::DisplaySource::Mesh;
} // namespace

static constexpr uint16_t TX_HISTORY_KEY_ENVIRONMENT_TELEMETRY = 0x8002;
static constexpr uint32_t IMMEDIATE_SEND_MAX_STALENESS_MS = 5UL * 60UL * 1000; // 5 minutes
static constexpr uint32_t LOCAL_DISPLAY_REFRESH_INTERVAL_MS = 1000;

EnvironmentTelemetryModule::DisplaySource EnvironmentTelemetryModule::getDisplaySource()
{
    return gDisplaySource;
}

void EnvironmentTelemetryModule::setDisplaySource(DisplaySource source)
{
    gDisplaySource = source;
    if (environmentTelemetryModule != nullptr) {
        environmentTelemetryModule->lastLocalDisplayRefreshMs = 0;
        environmentTelemetryModule->refreshDisplayedMeasurement();
    }
}

void EnvironmentTelemetryModule::clearMeasurementPacket()
{
    if (lastMeasurementPacket != nullptr) {
        packetPool.release(lastMeasurementPacket);
        lastMeasurementPacket = nullptr;
    }
}

bool EnvironmentTelemetryModule::shouldDisplayRemoteNode(NodeNum nodeNum) const
{
    if (nodeNum == 0 || nodeNum == nodeDB->getNodeNum()) {
        return false;
    }

    switch (getDisplaySource()) {
    case DisplaySource::LocalSensor:
        return false;
    case DisplaySource::Mesh:
        return true;
    case DisplaySource::FavoriteNodesOnly:
        return nodeDB->isFavorite(nodeNum);
    }

    return false;
}

bool EnvironmentTelemetryModule::shouldKeepCurrentRemoteDisplay() const
{
    if (lastMeasurementPacket == nullptr) {
        return false;
    }

    return shouldDisplayRemoteNode(getFrom(lastMeasurementPacket));
}

bool EnvironmentTelemetryModule::shouldDisplayLocalMeasurement() const
{
    switch (getDisplaySource()) {
    case DisplaySource::LocalSensor:
        return true;
    case DisplaySource::Mesh:
        return !shouldKeepCurrentRemoteDisplay();
    case DisplaySource::FavoriteNodesOnly:
        return false;
    }

    return false;
}

bool EnvironmentTelemetryModule::refreshLocalMeasurementPacket()
{
    // The scheduled reading: reading the sensors here would double every read and wake sleeping sensors.
    // A screen-only module has no sensors (i2cScanFinished adds none), so it shows only mesh readings.
    if (!hasLatest)
        return false;

    meshtastic_MeshPacket *localPacket = allocDataProtobuf(latest);
    if (localPacket == nullptr) {
        return false;
    }

    clearMeasurementPacket();
    lastMeasurementPacket = packetPool.allocCopy(*localPacket);
    packetPool.release(localPacket);
    return lastMeasurementPacket != nullptr;
}

void EnvironmentTelemetryModule::refreshDisplayedMeasurement()
{
    if (shouldDisplayLocalMeasurement()) {
        if (lastMeasurementPacket != nullptr &&
            Throttle::isWithinTimespanMs(lastLocalDisplayRefreshMs, LOCAL_DISPLAY_REFRESH_INTERVAL_MS)) {
            return;
        }
        lastLocalDisplayRefreshMs = millis();
        if (!refreshLocalMeasurementPacket()) {
            clearMeasurementPacket();
        }
        return;
    }

    if (!shouldKeepCurrentRemoteDisplay()) {
        clearMeasurementPacket();
    }
}

void EnvironmentTelemetryModule::i2cScanFinished(ScanI2C *i2cScanner)
{
    if (!moduleConfig.telemetry.environment_measurement_enabled && !ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE) {
        return;
    }
    LOG_INFO("Environment Telemetry adding I2C devices");

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.telemetry.environment_measurement_enabled = 1;
    // moduleConfig.telemetry.environment_screen_enabled = 1;
    // moduleConfig.telemetry.environment_update_interval = 15;

    // order by priority of metrics/values (low top, high bottom)

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#if ARCH_PORTDUINO
    // Not an I2C device.
    commandSensor = new CommandSensor(CommandSensorFields::Message::Environment);
    if (commandSensor->initDevice(nullptr, nullptr)) {
        sensors.push_front(commandSensor);
    } else {
        delete commandSensor;
        commandSensor = nullptr;
    }
#endif
#ifdef T1000X_SENSOR_EN
    // Not a real I2C device
    addSensor<T1000xSensor>(i2cScanner, ScanI2C::DeviceType::NONE);
#else
#if HAS_SPA06 && __has_include(<Adafruit_SPA06_003.h>)
    addSensor<SPA06Sensor>(i2cScanner, ScanI2C::DeviceType::SPA06);
#endif
#endif
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL
    addSensor<RCWL9620Sensor>(i2cScanner, ScanI2C::DeviceType::RCWL9620);
    addSensor<CGRadSensSensor>(i2cScanner, ScanI2C::DeviceType::CGRADSENS);
#if __has_include(<DFRobot_LarkWeatherStation.h>)
    addSensor<DFRobotLarkSensor>(i2cScanner, ScanI2C::DeviceType::DFROBOT_LARK);
#endif
#if __has_include(<DFRobot_RainfallSensor.h>)
    addSensor<DFRobotGravitySensor>(i2cScanner, ScanI2C::DeviceType::DFROBOT_RAIN);
#endif
#if __has_include(<SparkFun_AS3935.h>)
    addSensor<AS3935Sensor>(i2cScanner, ScanI2C::DeviceType::AS3935);
#endif
#if __has_include(<Adafruit_AHTX0.h>)
    addSensor<AHT10Sensor>(i2cScanner, ScanI2C::DeviceType::AHT10);
#endif
#if __has_include(<Adafruit_BMP085.h>)
    addSensor<BMP085Sensor>(i2cScanner, ScanI2C::DeviceType::BMP_085);
#endif
#if __has_include(<Adafruit_BME280.h>)
    addSensor<BME280Sensor>(i2cScanner, ScanI2C::DeviceType::BME_280);
#endif
#if __has_include(<Adafruit_LTR390.h>)
    addSensor<LTR390UVSensor>(i2cScanner, ScanI2C::DeviceType::LTR390UV);
#endif
#if __has_include(<Adafruit_BME680.h>)
    addSensor<BME680Sensor>(i2cScanner, ScanI2C::DeviceType::BME_680);
#endif
#if __has_include(<Adafruit_BMP280.h>)
    addSensor<BMP280Sensor>(i2cScanner, ScanI2C::DeviceType::BMP_280);
#endif
#if __has_include(<Adafruit_DPS310.h>)
    addSensor<DPS310Sensor>(i2cScanner, ScanI2C::DeviceType::DPS310);
#endif
#if __has_include(<Adafruit_MCP9808.h>)
    addSensor<MCP9808Sensor>(i2cScanner, ScanI2C::DeviceType::MCP9808);
#endif
#if __has_include(<Adafruit_LPS2X.h>)
    addSensor<LPS22HBSensor>(i2cScanner, ScanI2C::DeviceType::LPS22HB);
#endif
#if __has_include("RAK12035_SoilMoisture.h") && defined(RAK_4631) && RAK_4631 == 1
    addSensor<RAK12035Sensor>(i2cScanner, ScanI2C::DeviceType::RAK12035);
#endif
#if __has_include(<Adafruit_VEML7700.h>)
    addSensor<VEML7700Sensor>(i2cScanner, ScanI2C::DeviceType::VEML7700);
#endif
#if __has_include(<Adafruit_TSL2591.h>)
    addSensor<TSL2591Sensor>(i2cScanner, ScanI2C::DeviceType::TSL2591);
#endif
#if __has_include(<ClosedCube_OPT3001.h>)
    addSensor<OPT3001Sensor>(i2cScanner, ScanI2C::DeviceType::OPT3001);
#endif
#if __has_include(<SparkFun_MLX90632_Arduino_Library.h>)
    addSensor<MLX90632Sensor>(i2cScanner, ScanI2C::DeviceType::MLX90632);
#endif
#if __has_include(<Adafruit_BMP3XX.h>)
    addSensor<BMP3XXSensor>(i2cScanner, ScanI2C::DeviceType::BMP_3XX);
#endif
#if __has_include(<Adafruit_PCT2075.h>)
    addSensor<PCT2075Sensor>(i2cScanner, ScanI2C::DeviceType::PCT2075);
#endif
#if __has_include(<Adafruit_TSL2561_U.h>)
    addSensor<TSL2561Sensor>(i2cScanner, ScanI2C::DeviceType::TSL2561);
#endif
#if __has_include(<SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>)
    addSensor<NAU7802Sensor>(i2cScanner, ScanI2C::DeviceType::NAU7802);
#endif
#if __has_include(<BH1750_WE.h>)
    addSensor<BH1750Sensor>(i2cScanner, ScanI2C::DeviceType::BH1750);
#endif
#if __has_include(<Adafruit_ADS1X15.h>)
    addSensor<ADS1X15Sensor>(i2cScanner, ScanI2C::DeviceType::ADS1X15);
    addSensor<ADS1X15Sensor>(i2cScanner, ScanI2C::DeviceType::ADS1X15_ALT);
#endif
#if __has_include(<SHTSensor.h>)
    // TODO Can we scan for multiple sensors connected on the same bus?
    addSensor<SHTXXSensor>(i2cScanner, ScanI2C::DeviceType::SHTXX);
#endif
#if __has_include(<Adafruit_DS248x.h>)
    addSensor<DS248XSensor>(i2cScanner, ScanI2C::DeviceType::DS248X);
#endif

#endif
}

int32_t EnvironmentTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        if (shouldDeferDeepSleep())
            return PREFLIGHT_SLEEP_RETRY_MS;
        sleepOnNextExecution = false;
        enterSharedDeepSleep(); // returns only when a read or publish is too close to be worth sleeping
    }

    uint32_t result = UINT32_MAX;

    if (!(moduleConfig.telemetry.environment_measurement_enabled || moduleConfig.telemetry.environment_screen_enabled ||
          ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disableTelemetry();
    }

    if (!history)
        return disableTelemetry();

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;

        if (moduleConfig.telemetry.environment_measurement_enabled || ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE) {
            LOG_INFO("Environment Telemetry: init");

            // check if we have at least one sensor
            if (!sensors.empty()) {
                result = DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
            }

#ifdef T1000X_SENSOR_EN
#elif !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL
            if (ina219Sensor.hasSensor())
                result = ina219Sensor.runOnce();
            if (ina260Sensor.hasSensor())
                result = ina260Sensor.runOnce();
            if (ina3221Sensor.hasSensor())
                result = ina3221Sensor.runOnce();
            if (max17048Sensor.hasSensor())
                result = max17048Sensor.runOnce();
            // this only works on the wismesh hub with the solar option. This is not an I2C sensor, so we don't need the
            // sensormap here.
#ifdef HAS_RAKPROT
            if (rak9154Sensor.hasSensor())
                result = rak9154Sensor.runOnce();
#endif
#ifdef HAS_RAKHUB
            // 1-Wire SensorHub poll (not I2C); same UART as legacy RAK9154 but different env macro
            if (rakSensorHub.hasSensor())
                result = rakSensorHub.runOnce();
#endif
#endif
            refreshDisplayedMeasurement();
        }

        if (restoreScheduleAfterSleep(TELEMETRY_SLEEP_SLOT_ENVIRONMENT, schedule))
            LOG_DEBUG("Env: schedule restored after deep sleep");
        seedLatestFromHistory();
        refreshDisplayedMeasurement();

        // it's possible to have this module enabled, only for displaying values on the screen.
        // therefore, we should only enable the sensor loop if measurement is also enabled.
        // A power-saving SENSOR woke up for this read, so it does not wait out the broadcast spacing.
        if (result == UINT32_MAX)
            return disableTelemetry();
        measuring = true;
        return isPowerSavingSensor() ? result : setStartDelay();
    }

    // if we somehow got to a second run of this module with measurement disabled, then just wait forever
    if (!moduleConfig.telemetry.environment_measurement_enabled && !ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE) {
        measuring = false;
        return disableTelemetry();
    }

    // Some sensors need polling between reads (e.g. BME680 measurement in flight)
    uint32_t pollMs = UINT32_MAX;
    for (TelemetrySensor *sensor : sensors) {
        uint32_t delay = sensor->runOnce();
        if (delay < pollMs)
            pollMs = delay;
    }
    refreshDisplayedMeasurement();

    // Give up on a stale immediate-send request rather than fire an arbitrarily late broadcast.
    if (immediateSendRequested && !Throttle::isWithinTimespanMs(immediateSendRequestedAtMs, IMMEDIATE_SEND_MAX_STALENESS_MS)) {
        immediateSendRequested = false;
    }
    if (immediateSendRequested && immediateReadPending) {
        immediateReadPending = false;
        schedule.lastStoredMs = 0; // read and store now, the gap since the last reading stays known
    }

    const TelemetryCadence c = cadence();
    const bool phoneConnected = service->api_state != MeshService::STATE_DISCONNECTED && !isPowerSavingSensor();
    uint32_t nowMs = Time::getMillis();

    const uint32_t msUntilRead = schedule.msUntilRead(nowMs, c, phoneConnected);
    const uint32_t waitMs = prepareSensorsForRead(sensors, msUntilRead);
    warmingUp = msUntilRead == 0 && waitMs > 0;
    if (warmingUp) {
        LOG_DEBUG("Env: sensors warming up, %ums", (unsigned)waitMs);
        return min(waitMs, pollMs);
    }

    if (msUntilRead == 0) {
        captureReading(nowMs, c, phoneConnected);
        nowMs = Time::getMillis();
        sleepSensorsUntilNextRead(sensors, schedule.msUntilRead(nowMs, c, phoneConnected));
    }

    const uint32_t lastMesh = lastMeshMs();
    const bool meshAllowed = airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
                             airTime->isTxAllowedAirUtil();
    const bool meshDue = immediateSendRequested || (schedule.meshDue(nowMs, lastMesh, c) && !schedule.storedReadSoon(nowMs, c));

    if (meshDue) {
        bool sent = false;
        if (meshAllowed && history->hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH))
            sent = c.batchReadings ? sendBufferedTelemetry() : sendTelemetry(NODENUM_BROADCAST, false);
        if (sent) {
            immediateSendRequested = false;
            schedule.onMeshSent(nowMs);
            noteSentToMesh();
            if (transmitHistory)
                transmitHistory->setLastSentToMesh(TX_HISTORY_KEY_ENVIRONMENT_TELEMETRY);
        } else {
            LOG_DEBUG("Env: mesh due, %s, retry later", meshAllowed ? "nothing new to send" : "airtime limit");
            schedule.onMeshSkipped(nowMs,
                                   meshAllowed ? schedule.meshRetryWhenEmpty(nowMs, c) : TelemetryScheduler::MESH_RETRY_MS);
        }
    }

    // SENSOR role: readings the broker has not had, straight to it without going on air.
    // Unreachable broker: the readings stay pending for MQTT. Awake, that counts as an attempt so the due
    // uplink does not spin; on reads, the next read's wake-up tries again.
    if (schedule.mqttDue(nowMs, c)) {
        if (telemetryMqttUplinkAvailable()) {
            if (c.batchReadings)
                sendTelemetryBatch(*history, allocDataPacket(), TELEMETRY_PUBLISHED_MQTT);
            else
                publishTelemetryReadings(*history, [this](const meshtastic_Telemetry &t) { return allocDataProtobuf(t); });
            schedule.onMqttPublished(nowMs);
        } else if (!c.publishOnReads) {
            schedule.onMqttPublished(nowMs);
        }
    }

    if (isPowerSavingSensor() && sharedSleepBudgetMs() >= TELEMETRY_MIN_DEEP_SLEEP_MS) {
        // Grace period so the queued packet leaves before the radio is powered down
        sleepOnNextExecution = true;
        preflightSleepDeferrals = 0;
        LOG_DEBUG("Start next execution in 5s, then sleep");
        return FIVE_SECONDS_MS;
    }

    nowMs = Time::getMillis();
    const uint32_t nextMs = schedule.msUntilNextAction(nowMs, c, phoneConnected, lastMeshMs(), sensorWakeLeadMs(sensors));
    const uint32_t nextWakeMs = prepareSensorsForRead(sensors, schedule.msUntilRead(nowMs, c, phoneConnected));
    uint32_t next = min(nextMs, nextWakeMs);
    if (immediateSendRequested)
        next = min(next, TelemetryScheduler::MESH_RETRY_MS);
    return max((uint32_t)DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS, min(next, pollMs));
}

TelemetryCadence EnvironmentTelemetryModule::cadence() const
{
    const uint32_t meshSecs = moduleConfig.telemetry.environment_update_interval;
    TelemetryCadence c;
    c.meshMs = Default::getConfiguredOrDefaultMsScaled(meshSecs, default_telemetry_broadcast_interval_secs, numOnlineNodes,
                                                       TrafficType::TELEMETRY);
    // Unset: read as often as the mesh publishes, as before the read interval existed
    const uint32_t readSecs = moduleConfig.telemetry.environment_telemetry_read_interval;
    c.readMs =
        readSecs
            ? Default::getConfiguredOrDefaultMs(Default::getConfiguredOrMinimumValue(readSecs, min_telemetry_read_interval_secs))
            : Default::getConfiguredOrDefaultMs(meshSecs, default_telemetry_broadcast_interval_secs);
    c.phoneMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_phone_interval,
                                                  default_telemetry_phone_interval_secs);
    // Deep sleeping, each uplink costs a Wi-Fi join: batch it like the mesh, on the read wake-ups
    c.publishOnReads = isPowerSavingSensor();
    c.mqttMs = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && moduleConfig.mqtt.telemetry_uplink_enabled
                   ? (c.publishOnReads ? c.meshMs : c.readMs)
                   : 0;
    // A SENSOR batches only when it reads more often than it publishes; otherwise each reading goes out
    // on its own as Telemetry, which every client and node already understands
    c.batchReadings = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && c.readMs < c.meshMs;
    return c;
}

uint32_t EnvironmentTelemetryModule::lastMeshMs() const
{
    return schedule.lastMeshMs(transmitHistory ? transmitHistory->getLastSentToMeshMillis(TX_HISTORY_KEY_ENVIRONMENT_TELEMETRY)
                                               : 0);
}

uint32_t EnvironmentTelemetryModule::msUntilNeededAwake()
{
    if (!measuring)
        return UINT32_MAX;
    return schedule.msUntilNextAction(Time::getMillis(), cadence(), false, lastMeshMs(), sensorWakeLeadMs(sensors));
}

bool EnvironmentTelemetryModule::isTelemetryBusy()
{
    // Not yet initialised: it may still want a read before the node sleeps
    return warmingUp || immediateSendRequested ||
           (firstTime && (moduleConfig.telemetry.environment_measurement_enabled || ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE));
}

void EnvironmentTelemetryModule::onTelemetryDeepSleep(uint32_t sleepMs)
{
    sleepSensorsUntilNextRead(sensors, sleepMs);
    saveScheduleForSleep(TELEMETRY_SLEEP_SLOT_ENVIRONMENT, schedule);
}

void EnvironmentTelemetryModule::openHistory()
{
#ifdef ENVIRONMENT_TELEMETRY_HISTORY_PATH
    // A SENSOR batches between publishes, so its readings have to outlive a reboot or deep sleep
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR) {
        auto *persistent = makeFileTelemetryStore<meshtastic_EnvironmentMetrics>(
            ENVIRONMENT_TELEMETRY_HISTORY_PATH, ENVIRONMENT_TELEMETRY_HISTORY_SIZE, ENVIRONMENT_TELEMETRY_HISTORY_FS);
        if (persistent && persistent->isUsable()) {
            history.reset(persistent);
            return;
        }

        // No card, full filesystem, whatever: measuring into RAM beats not measuring
        delete persistent;
        LOG_WARN("Env history: %s unusable, keeping readings in RAM", ENVIRONMENT_TELEMETRY_HISTORY_PATH);
    }
#endif

    history.reset(new (std::nothrow) RamTelemetryStore<meshtastic_EnvironmentMetrics>(ENVIRONMENT_TELEMETRY_HISTORY_SIZE));
    if (!history)
        LOG_ERROR("Env history: no memory for a store, telemetry disabled");
}

bool EnvironmentTelemetryModule::storeReading(const meshtastic_EnvironmentMetrics &metrics, uint32_t time, uint32_t deltaSecs)
{
    return storeWithRamFallback(history, ENVIRONMENT_TELEMETRY_HISTORY_SIZE, metrics, time, deltaSecs, "Env");
}

void EnvironmentTelemetryModule::seedLatestFromHistory()
{
    // So a request right after boot or a wake-up is answered before the first read
    TelemetryReading<meshtastic_EnvironmentMetrics> reading;
    if (hasLatest || !history->newest(reading))
        return;
    latest = toTelemetry(reading);
    hasLatest = true;
}

void EnvironmentTelemetryModule::captureReading(uint32_t nowMs, const TelemetryCadence &cadence, bool phoneConnected)
{
    const bool onCadence = schedule.readIsStored(nowMs, cadence);
    bool stored = onCadence;
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    const bool valid = getEnvironmentTelemetry(&m);

    if (valid) {
        latest = m;
        hasLatest = true;
        lastLocalDisplayRefreshMs = 0; // the screen picks up the new reading now
        refreshDisplayedMeasurement();
        if (stored)
            stored = storeReading(m.variant.environment_metrics, m.time, schedule.deltaSecs(nowMs));
        LOG_INFO("Env: read %s, %u stored", stored ? "stored" : "for phone", (unsigned)history->size());
    } else {
        LOG_WARN("Env read failed, keep the previous reading");
    }
    // A reading the store refused does not count as the previous one for deltaSecs
    schedule.onRead(nowMs, onCadence, valid && stored, cadence.readMs, sharedReadAnchorMs(this));

    if (valid && phoneConnected && service->isToPhoneQueueEmpty() && sendTelemetry(NODENUM_BROADCAST, true) && stored)
        history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
}

bool EnvironmentTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.environment_screen_enabled;
}

#if HAS_SCREEN
void EnvironmentTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // === Setup display ===
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    int line = 1;

    // === Set Title
    const char *titleStr = (graphics::currentResolution == graphics::ScreenResolution::High) ? "Environment" : "Env.";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === Row spacing setup ===
    const int rowHeight = FONT_HEIGHT_SMALL - 4;
    int currentY = graphics::getTextPositions(display)[line++];

    // === Show "No Telemetry" if no data available ===
    if (!lastMeasurementPacket) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // Decode the telemetry message from the latest received packet
    const meshtastic_Data &p = lastMeasurementPacket->decoded;
    meshtastic_Telemetry telemetry;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &telemetry)) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // Bound the float metrics before String(float) renders them (see UnitConversions::displaySafeFloat);
    // the stored packet is always the environment variant.
    {
        auto &e = telemetry.variant.environment_metrics;
        e.temperature = UnitConversions::displaySafeFloat(e.temperature);
        e.relative_humidity = UnitConversions::displaySafeFloat(e.relative_humidity);
        e.barometric_pressure = UnitConversions::displaySafeFloat(e.barometric_pressure);
        e.voltage = UnitConversions::displaySafeFloat(e.voltage);
        e.current = UnitConversions::displaySafeFloat(e.current);
        e.lux = UnitConversions::displaySafeFloat(e.lux);
        e.white_lux = UnitConversions::displaySafeFloat(e.white_lux);
        e.weight = UnitConversions::displaySafeFloat(e.weight);
        e.distance = UnitConversions::displaySafeFloat(e.distance);
        e.radiation = UnitConversions::displaySafeFloat(e.radiation);
    }

    const auto &m = telemetry.variant.environment_metrics;

    // Check if any telemetry field has valid data
    bool hasAny = m.has_temperature || m.has_relative_humidity || m.barometric_pressure != 0 || m.has_iaq || m.voltage != 0 ||
                  m.current != 0 || m.lux != 0 || m.white_lux != 0 || m.weight != 0 || m.distance != 0 || m.radiation != 0;

    if (!hasAny) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // === First line: Show sender name + time since received (left), and first metric (right) ===
    const bool isLocalTelemetry = (getFrom(lastMeasurementPacket) == nodeDB->getNodeNum());
    const char *sender = isLocalTelemetry ? "Local Sensor" : getSenderShortName(*lastMeasurementPacket);
    String leftStr = String(sender);
    if (!isLocalTelemetry) {
        uint32_t agoSecs = service->GetTimeSinceMeshPacket(lastMeasurementPacket);
        String agoStr = (agoSecs > 864000) ? "?"
                        : (agoSecs > 3600) ? String(agoSecs / 3600) + "h"
                        : (agoSecs > 60)   ? String(agoSecs / 60) + "m"
                                           : String(agoSecs) + "s";
        leftStr += " (" + agoStr + ")";
    }
    display->drawString(x, currentY, leftStr); // Left side: who and when

    // === Collect sensor readings as label strings (no icons) ===
    std::vector<String> entries;

    if (m.has_temperature) {
        String tempStr = moduleConfig.telemetry.environment_display_fahrenheit
                             ? "Tmp: " + String(UnitConversions::CelsiusToFahrenheit(m.temperature), 1) + "°F"
                             : "Tmp: " + String(m.temperature, 1) + "°C";
        entries.push_back(tempStr);
    }
    if (m.has_relative_humidity)
        entries.push_back("Hum: " + String(m.relative_humidity, 0) + "%");
    if (m.barometric_pressure != 0)
        entries.push_back("Prss: " + String(m.barometric_pressure, 0) + " hPa");
    if (m.has_iaq) {
        String aqi = "IAQ: " + String(m.iaq);
        const char *bannerMsg = nullptr; // Default: no banner

        if (m.iaq <= 25)
            aqi += " (Excellent)";
        else if (m.iaq <= 50)
            aqi += " (Good)";
        else if (m.iaq <= 100)
            aqi += " (Moderate)";
        else if (m.iaq <= 150)
            aqi += " (Poor)";
        else if (m.iaq <= 200) {
            aqi += " (Unhealthy)";
            bannerMsg = "Unhealthy IAQ";
        } else if (m.iaq <= 300) {
            aqi += " (Very Unhealthy)";
            bannerMsg = "Very Unhealthy IAQ";
        } else {
            aqi += " (Hazardous)";
            bannerMsg = "Hazardous IAQ";
        }

        entries.push_back(aqi);

        // === IAQ alert logic ===
        static uint32_t lastAlertTime = 0;
        uint32_t now = millis();

        bool isOwnTelemetry = isLocalTelemetry;
        bool isCooldownOver = (now - lastAlertTime > 60000);

        if (isOwnTelemetry && bannerMsg && isCooldownOver) {
            LOG_INFO("drawFrame: IAQ %d (own) - showing banner: %s", m.iaq, bannerMsg);
            screen->showSimpleBanner(bannerMsg, 3000);

            // Only buzz if IAQ is over 200
            if (m.iaq > 200 && moduleConfig.external_notification.enabled && !externalNotificationModule->getMute()) {
                playLongBeep();
            }

            lastAlertTime = now;
        }
    }
    if (m.voltage != 0 || m.current != 0)
        entries.push_back(String(m.voltage, 1) + "V / " + String(m.current, 0) + "mA");
    if (m.lux != 0)
        entries.push_back("Light: " + String(m.lux, 0) + "lx");
    if (m.white_lux != 0)
        entries.push_back("White: " + String(m.white_lux, 0) + "lx");
    if (m.weight != 0)
        entries.push_back("Weight: " + String(m.weight, 0) + "kg");
    if (m.distance != 0)
        entries.push_back("Level: " + String(m.distance, 0) + "mm");
    if (m.radiation != 0)
        entries.push_back("Rad: " + String(m.radiation, 2) + " µR/h");

    // === Show first available metric on top-right of first line ===
    if (!entries.empty()) {
        String valueStr = entries.front();
        int rightX = SCREEN_WIDTH - display->getStringWidth(valueStr);
        display->drawString(rightX, currentY, valueStr);
        entries.erase(entries.begin()); // Remove from queue
    }

    // === Advance to next line for remaining telemetry entries ===
    currentY += rowHeight;

    // === Draw remaining entries in 2-column format (left and right) ===
    for (size_t i = 0; i < entries.size(); i += 2) {
        // Left column
        display->drawString(x, currentY, entries[i]);

        // Right column if it exists
        if (i + 1 < entries.size()) {
            int rightX = SCREEN_WIDTH / 2;
            display->drawString(rightX, currentY, entries[i + 1]);
        }

        currentY += rowHeight;
    }
    graphics::drawCommonFooter(display, x, y);
}
#endif

bool EnvironmentTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): barometric_pressure=%f, current=%f, gas_resistance=%f, relative_humidity=%f, "
                 "temperature=%f",
                 sender, t->variant.environment_metrics.barometric_pressure, t->variant.environment_metrics.current,
                 t->variant.environment_metrics.gas_resistance, t->variant.environment_metrics.relative_humidity,
                 t->variant.environment_metrics.temperature);
        LOG_INFO("(Received from %s): voltage=%f, IAQ=%d, distance=%f, lux=%f, white_lux=%f", sender,
                 t->variant.environment_metrics.voltage, t->variant.environment_metrics.iaq,
                 t->variant.environment_metrics.distance, t->variant.environment_metrics.lux,
                 t->variant.environment_metrics.white_lux);

        LOG_INFO("(Received from %s): wind speed=%fm/s, direction=%d degrees, weight=%fkg", sender,
                 t->variant.environment_metrics.wind_speed, t->variant.environment_metrics.wind_direction,
                 t->variant.environment_metrics.weight);

        LOG_INFO("(Received from %s): radiation=%fµR/h", sender, t->variant.environment_metrics.radiation);

#endif
        // Cache the latest env metrics per node on NodeDB so the phone can
        // pull last-known values across reboots and replays.
        nodeDB->updateTelemetry(getFrom(&mp), *t, RX_SRC_RADIO);

        const NodeNum senderNode = getFrom(&mp);
        if (shouldDisplayRemoteNode(senderNode)) {
            clearMeasurementPacket();
            lastMeasurementPacket = packetPool.allocCopy(mp);
        } else if (!shouldKeepCurrentRemoteDisplay()) {
            refreshDisplayedMeasurement();
        }
    }

    return false; // Let others look at this message also if they want
}

bool EnvironmentTelemetryModule::getEnvironmentTelemetry(meshtastic_Telemetry *m)
{
    bool valid = false;
    bool hasSensor = false;
    // getMetrics() doesn't always get evaluated because of
    // short-circuit evaluation rules in c++
    bool get_metrics;
    // getTime() falls back to uptime-as-epoch when unset; 0 lets the receiver use its rx time
    m->time = getValidTime(RTCQualityDevice);
    m->which_variant = meshtastic_Telemetry_environment_metrics_tag;
    m->variant.environment_metrics = meshtastic_EnvironmentMetrics_init_zero;

    for (TelemetrySensor *sensor : sensors) {
#if ARCH_PORTDUINO
        if (sensor == commandSensor)
            continue;
#endif
        get_metrics = sensor->getMetrics(m); // avoid short-circuit evaluation rules
        valid = valid || get_metrics;
        hasSensor = true;
    }

#ifndef T1000X_SENSOR_EN
    if (ina219Sensor.hasSensor()) {
        get_metrics = ina219Sensor.getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
    if (ina260Sensor.hasSensor()) {
        get_metrics = ina260Sensor.getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
    if (ina3221Sensor.hasSensor()) {
        get_metrics = ina3221Sensor.getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
    if (max17048Sensor.hasSensor()) {
        get_metrics = max17048Sensor.getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
#endif
#ifdef HAS_RAKPROT
    if (rak9154Sensor.hasSensor()) {
        get_metrics = rak9154Sensor.getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
#endif
#ifdef HAS_RAKHUB
    // IPSO cache -> EnvironmentMetrics; chemistry off unless RAK_SENSORHUB_EXTENDED_ENV_METRICS.
    if (rakSensorHub.hasSensor()) {
        get_metrics = rakSensorHub.getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
#endif
#if ARCH_PORTDUINO
    if (commandSensor) {
        get_metrics = commandSensor->getMetrics(m);
        valid = valid || get_metrics;
        hasSensor = true;
    }
#endif
    return valid && hasSensor;
}

meshtastic_MeshPacket *EnvironmentTelemetryModule::allocReply()
{
    if (currentRequest) {
        if (isMultiHopBroadcastRequest() && !isSensorOrRouterRole()) {
            ignoreRequest = true;
            return NULL;
        }
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding EnvironmentTelemetry module");
            return NULL;
        }
        // Check for a request for environment metrics
        if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
            // From the last read, like the screen: no extra read on request
            if (!hasLatest) {
                LOG_INFO("No environment reading yet, no reply to request");
                return NULL;
            }
            LOG_INFO("Environment telemetry reply to request");
            return allocDataProtobuf(latest);
        }
    }
    return NULL;
}

void EnvironmentTelemetryModule::logTelemetry(const meshtastic_Telemetry &m)
{
    if (m.variant.environment_metrics.has_temperature || m.variant.environment_metrics.has_relative_humidity ||
        m.variant.environment_metrics.has_barometric_pressure)
        LOG_INFO("Send: barometric_pressure=%fkPa, relative_humidity=%f%RH, temperature=%fdegC",
                 m.variant.environment_metrics.barometric_pressure, m.variant.environment_metrics.relative_humidity,
                 m.variant.environment_metrics.temperature);

    if (m.variant.environment_metrics.has_voltage || m.variant.environment_metrics.has_current ||
        m.variant.environment_metrics.has_iaq || m.variant.environment_metrics.has_gas_resistance)
        LOG_INFO("Send: voltage=%f, current=%f, IAQ=%d, gas_resistance=%f", m.variant.environment_metrics.voltage,
                 m.variant.environment_metrics.current, m.variant.environment_metrics.iaq,
                 m.variant.environment_metrics.gas_resistance);

    if (m.variant.environment_metrics.has_distance || m.variant.environment_metrics.has_lux)
        LOG_INFO("Send: distance=%f, lux=%f", m.variant.environment_metrics.distance, m.variant.environment_metrics.lux);

    if (m.variant.environment_metrics.has_wind_speed || m.variant.environment_metrics.has_wind_direction)
        LOG_INFO("Send: wind speed=%fm/s, direction=%d degrees", m.variant.environment_metrics.wind_speed,
                 m.variant.environment_metrics.wind_direction);

    if (m.variant.environment_metrics.has_weight)
        LOG_INFO("Send: weight=%fkg", m.variant.environment_metrics.weight);

    if (m.variant.environment_metrics.has_radiation)
        LOG_INFO("Send: radiation=%fµR/h", m.variant.environment_metrics.radiation);

    if (m.variant.environment_metrics.has_soil_temperature || m.variant.environment_metrics.has_soil_moisture)
        LOG_INFO("Send: soil_temperature=%f, soil_moisture=%u", m.variant.environment_metrics.soil_temperature,
                 m.variant.environment_metrics.soil_moisture);

    if (m.variant.environment_metrics.has_adc_voltage_ch0 || m.variant.environment_metrics.has_adc_voltage_ch1 ||
        m.variant.environment_metrics.has_adc_voltage_ch2 || m.variant.environment_metrics.has_adc_voltage_ch3)
        LOG_INFO("Send: adc_ch0=%f, adc_ch1=%f, adc_ch2=%f, adc_ch3=%f", m.variant.environment_metrics.adc_voltage_ch0,
                 m.variant.environment_metrics.adc_voltage_ch1, m.variant.environment_metrics.adc_voltage_ch2,
                 m.variant.environment_metrics.adc_voltage_ch3);

    if (m.variant.environment_metrics.has_adc_voltage_ch4 || m.variant.environment_metrics.has_adc_voltage_ch5 ||
        m.variant.environment_metrics.has_adc_voltage_ch6 || m.variant.environment_metrics.has_adc_voltage_ch7)
        LOG_INFO("Send: adc_ch4=%f, adc_ch5=%f, adc_ch6=%f, adc_ch7=%f", m.variant.environment_metrics.adc_voltage_ch4,
                 m.variant.environment_metrics.adc_voltage_ch5, m.variant.environment_metrics.adc_voltage_ch6,
                 m.variant.environment_metrics.adc_voltage_ch7);

    if (m.variant.environment_metrics.has_lightning_strike_count_1h)
        LOG_INFO("Send: lightning=%u, distance=%fkm", m.variant.environment_metrics.lightning_strike_count_1h,
                 m.variant.environment_metrics.lightning_distance_km);
}

bool EnvironmentTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    if (phoneOnly) {
        // The phone gets every read, stored or not
        if (!hasLatest)
            return false;
        m = latest;
    } else {
        // One fetch then check the mask: on a file-backed store each read costs an open
        TelemetryReading<meshtastic_EnvironmentMetrics> reading;
        if (!history->newest(reading) || (reading.publishedMask & TELEMETRY_PUBLISHED_MESH))
            return false;
        m.which_variant = meshtastic_Telemetry_environment_metrics_tag;
        m.time = reading.time;
        m.variant.environment_metrics = reading.metrics;
    }

    logTelemetry(m);

    meshtastic_MeshPacket *p = allocDataProtobuf(m);
    if (!p)
        return false;
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR ? meshtastic_MeshPacket_Priority_RELIABLE
                                                                                   : meshtastic_MeshPacket_Priority_BACKGROUND;

    if (phoneOnly) {
        LOG_INFO("Send packet to phone");
        service->sendToPhone(p);
        return true;
    }

    LOG_INFO("Send packet to mesh");
    const ErrorCode res = service->sendToMesh(p, RX_SRC_LOCAL, true);
    if (res != ERRNO_OK) {
        LOG_WARN("Env: mesh refused the reading (%d), keep it for later", res);
        return false;
    }
    history->markNewestPublished(TELEMETRY_PUBLISHED_MESH);
    // ccToPhone above hands the phone this very reading, no separate phone send needed
    history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    notifySleepAfterSend();
    return true;
}

bool EnvironmentTelemetryModule::sendBufferedTelemetry()
{
    if (sendTelemetryBatch(*history, allocDataPacket(), TELEMETRY_PUBLISHED_MESH) == 0)
        return false;
    // TOFIX(3.0): the newest reading also goes out alone as Telemetry, for the clients, NodeDB caches, screens
    // and maps that only read TELEMETRY_APP. Drop it once TELEMETRY_HISTORY_APP is understood everywhere.
    // ccToPhone also hands it to a connected client, which never gets the batch.
    TelemetryReading<meshtastic_EnvironmentMetrics> newest;
    if (history->newest(newest)) {
        meshtastic_MeshPacket *p = allocDataProtobuf(toTelemetry(newest));
        if (p) {
            p->to = NODENUM_BROADCAST;
            p->decoded.want_response = false;
            p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
            if (service->sendToMesh(p, RX_SRC_LOCAL, true) == ERRNO_OK)
                history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
        }
    }
    notifySleepAfterSend();
    return true;
}

void EnvironmentTelemetryModule::notifySleepAfterSend()
{
    if (!isPowerSavingSensor())
        return;
    meshtastic_ClientNotification *notification = clientNotificationPool.allocZeroed();
    if (!notification)
        return;
    notification->level = meshtastic_LogRecord_Level_INFO;
    notification->type = meshtastic_ClientNotification_NotificationType_TELEMETRY_SENT_SLEEPING;
    notification->time = getValidTime(RTCQualityFromNet);
    snprintf(notification->message, sizeof(notification->message), "Sending telemetry and sleeping until the next read");
    service->sendClientNotification(notification);
}

AdminMessageHandleResult EnvironmentTelemetryModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                                 meshtastic_AdminMessage *request,
                                                                                 meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result = AdminMessageHandleResult::NOT_HANDLED;
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL

    for (TelemetrySensor *sensor : sensors) {
        result = sensor->handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }

    if (ina219Sensor.hasSensor()) {
        result = ina219Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (ina260Sensor.hasSensor()) {
        result = ina260Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (ina3221Sensor.hasSensor()) {
        result = ina3221Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (max17048Sensor.hasSensor()) {
        result = max17048Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
#endif
    return result;
}

#endif
