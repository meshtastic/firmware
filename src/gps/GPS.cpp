#include "GPS.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "sleep.h"

// If we have a serial GPS port it will not be null
#ifdef GPS_SERIAL_NUM
HardwareSerial _serial_gps_real(GPS_SERIAL_NUM);
HardwareSerial *GPS::_serial_gps = &_serial_gps_real;
#elif defined(NRF52840_XXAA) || defined(NRF52833_XXAA)
// Assume NRF52840
HardwareSerial *GPS::_serial_gps = &Serial1;
#else
HardwareSerial *GPS::_serial_gps = NULL;
#endif

GPS *gps;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

bool GPS::getACK(uint8_t c, uint8_t i)
{
    uint8_t b;
    uint8_t ack = 0;
    const uint8_t ackP[2] = {c, i};
    uint8_t buf[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    unsigned long startTime = millis();

    for (int j = 2; j < 6; j++) {
        buf[8] += buf[j];
        buf[9] += buf[8];
    }

    for (int j = 0; j < 2; j++) {
        buf[6 + j] = ackP[j];
        buf[8] += buf[6 + j];
        buf[9] += buf[8];
    }

    while (1) {
        if (ack > 9) {
            return true;
        }
        if (millis() - startTime > 1000) {
            return false;
        }
        if (_serial_gps->available()) {
            b = _serial_gps->read();
            if (b == buf[ack]) {
                ack++;
            } else {
                ack = 0;
            }
        }
    }
}

/**
 * @brief
 * @note   New method, this method can wait for the specified class and message ID, and return the payload
 * @param  *buffer: The message buffer, if there is a response payload message, it will be returned through the buffer parameter
 * @param  size:    size of buffer
 * @param  requestedClass:  request class constant
 * @param  requestedID:     request message ID constant
 * @retval length of payload message
 */
int GPS::getAck(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID)
{
    uint16_t ubxFrameCounter = 0;
    uint32_t startTime = millis();
    uint16_t needRead;

    while (millis() - startTime < 800) {
        while (_serial_gps->available()) {
            int c = _serial_gps->read();
            switch (ubxFrameCounter) {
            case 0:
                // ubxFrame 'μ'
                if (c == 0xB5) {
                    ubxFrameCounter++;
                }
                break;
            case 1:
                // ubxFrame 'b'
                if (c == 0x62) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                // Class
                if (c == requestedClass) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                // Message ID
                if (c == requestedID) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                // Payload lenght lsb
                needRead = c;
                ubxFrameCounter++;
                break;
            case 5:
                // Payload lenght msb
                needRead |= (c << 8);
                ubxFrameCounter++;
                break;
            case 6:
                // Check for buffer overflow
                if (needRead >= size) {
                    ubxFrameCounter = 0;
                    break;
                }
                if (_serial_gps->readBytes(buffer, needRead) != needRead) {
                    ubxFrameCounter = 0;
                } else {
                    // return payload lenght
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
    }
    return 0;
}

bool GPS::setupGPS()
{
    if (_serial_gps && !didSerialInit) {
        didSerialInit = true;

#ifdef ARCH_ESP32
        // In esp32 framework, setRxBufferSize needs to be initialized before Serial
        _serial_gps->setRxBufferSize(2048); // the default is 256
#endif

        // if the overrides are not dialled in, set them from the board definitions, if they exist

#if defined(GPS_RX_PIN)
        if (!config.position.rx_gpio)
            config.position.rx_gpio = GPS_RX_PIN;
#endif
#if defined(GPS_TX_PIN)
        if (!config.position.tx_gpio)
            config.position.tx_gpio = GPS_TX_PIN;
#endif

// ESP32 has a special set of parameters vs other arduino ports
#if defined(ARCH_ESP32)
        if (config.position.rx_gpio)
            _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, config.position.rx_gpio, config.position.tx_gpio);
#else
        _serial_gps->begin(GPS_BAUDRATE);
#endif

        /*
         * T-Beam-S3-Core will be preset to use gps Probe here, and other boards will not be changed first
         */
        gnssModel = probe();

        if (gnssModel == GNSS_MODEL_MTK) {
            /*
             * t-beam-s3-core uses the same L76K GNSS module as t-echo.
             * Unlike t-echo, L76K uses 9600 baud rate for communication by default.
             * */
            // _serial_gps->begin(9600);    //The baud rate of 9600 has been initialized at the beginning of setupGPS, this line
            // is the redundant part delay(250);

            // Initialize the L76K Chip, use GPS + GLONASS
            _serial_gps->write("$PCAS04,5*1C\r\n");
            delay(250);
            // only ask for RMC and GGA
            _serial_gps->write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
            delay(250);
            // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
            _serial_gps->write("$PCAS11,3*1E\r\n");
            delay(250);
        } else if (gnssModel == GNSS_MODEL_UBLOX) {

            /*
                tips: NMEA Only should not be set here, otherwise initializing Ublox gnss module again after
                setting will not output command messages in UART1, resulting in unrecognized module information

                // Set the UART port to output NMEA only
                byte _message_nmea[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xC0, 0x08, 0x00, 0x00,
                                        0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x91, 0xAF};
                _serial_gps->write(_message_nmea, sizeof(_message_nmea));
                if (!getACK(0x06, 0x00)) {
                    LOG_WARN("Unable to enable NMEA Mode.\n");
                    return true;
                }
            */

            // ublox-M10S can be compatible with UBLOX traditional protocol, so the following sentence settings are also valid

            // disable GGL
            byte _message_GGL[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01,
                                   0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x05, 0x3A};
            _serial_gps->write(_message_GGL, sizeof(_message_GGL));
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA GGL.\n");
                return true;
            }

            // disable GSA
            byte _message_GSA[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02,
                                   0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x06, 0x41};
            _serial_gps->write(_message_GSA, sizeof(_message_GSA));
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA GSA.\n");
                return true;
            }

            // disable GSV
            byte _message_GSV[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03,
                                   0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x07, 0x48};
            _serial_gps->write(_message_GSV, sizeof(_message_GSV));
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA GSV.\n");
                return true;
            }

            // disable VTG
            byte _message_VTG[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05,
                                   0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x09, 0x56};
            _serial_gps->write(_message_VTG, sizeof(_message_VTG));
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to disable NMEA VTG.\n");
                return true;
            }

            // enable RMC
            byte _message_RMC[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x04,
                                   0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x09, 0x54};
            _serial_gps->write(_message_RMC, sizeof(_message_RMC));
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to enable NMEA RMC.\n");
                return true;
            }

            // enable GGA
            byte _message_GGA[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x00,
                                   0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x05, 0x38};
            _serial_gps->write(_message_GGA, sizeof(_message_GGA));
            if (!getACK(0x06, 0x01)) {
                LOG_WARN("Unable to enable NMEA GGA.\n");
            }
        }
    }

    return true;
}

bool GPS::setup()
{
    // Master power for the GPS
#ifdef PIN_GPS_EN
    digitalWrite(PIN_GPS_EN, 1);
    pinMode(PIN_GPS_EN, OUTPUT);
#endif

#ifdef HAS_PMU
    if (config.position.gps_enabled) {
        setGPSPower(true);
    }
#endif

#ifdef PIN_GPS_RESET
    digitalWrite(PIN_GPS_RESET, 1); // assert for 10ms
    pinMode(PIN_GPS_RESET, OUTPUT);
    delay(10);
    digitalWrite(PIN_GPS_RESET, 0);
#endif
    setAwake(true); // Wake GPS power before doing any init
    bool ok = setupGPS();

    if (ok) {
        notifySleepObserver.observe(&notifySleep);
        notifyDeepSleepObserver.observe(&notifyDeepSleep);
        notifyGPSSleepObserver.observe(&notifyGPSSleep);
    }

    if (config.position.gps_enabled == false && config.position.fixed_position == false) {
        setAwake(false);
        doGPSpowersave(false);
    }
    return ok;
}

GPS::~GPS()
{
    // we really should unregister our sleep observer
    notifySleepObserver.unobserve(&notifySleep);
    notifyDeepSleepObserver.unobserve(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);
}

bool GPS::hasLock()
{
    return hasValidLocation;
}

bool GPS::hasFlow()
{
    return hasGPS;
}

// Allow defining the polarity of the WAKE output.  default is active high
#ifndef GPS_WAKE_ACTIVE
#define GPS_WAKE_ACTIVE 1
#endif

void GPS::wake()
{
#ifdef PIN_GPS_WAKE
    digitalWrite(PIN_GPS_WAKE, GPS_WAKE_ACTIVE);
    pinMode(PIN_GPS_WAKE, OUTPUT);
#endif
}

void GPS::sleep()
{
#ifdef PIN_GPS_WAKE
    digitalWrite(PIN_GPS_WAKE, GPS_WAKE_ACTIVE ? 0 : 1);
    pinMode(PIN_GPS_WAKE, OUTPUT);
#endif
}

/// Record that we have a GPS
void GPS::setConnected()
{
    if (!hasGPS) {
        hasGPS = true;
        shouldPublish = true;
    }
}

void GPS::setNumSatellites(uint8_t n)
{
    if (n != numSatellites) {
        numSatellites = n;
        shouldPublish = true;
    }
}

/**
 * Switch the GPS into a mode where we are actively looking for a lock, or alternatively switch GPS into a low power mode
 *
 * calls sleep/wake
 */
void GPS::setAwake(bool on)
{
    if (!wakeAllowed && on) {
        LOG_WARN("Inhibiting because !wakeAllowed\n");
        on = false;
    }

    if (isAwake != on) {
        LOG_DEBUG("WANT GPS=%d\n", on);
        if (on) {
            lastWakeStartMsec = millis();
            wake();
        } else {
            lastSleepStartMsec = millis();
            sleep();
        }

        isAwake = on;
    }
}

/** Get how long we should stay looking for each aquisition in msecs
 */
uint32_t GPS::getWakeTime() const
{
    uint32_t t = config.position.gps_attempt_time;

    if (t == UINT32_MAX)
        return t; // already maxint
    return t * 1000;
}

/** Get how long we should sleep between aqusition attempts in msecs
 */
uint32_t GPS::getSleepTime() const
{
    uint32_t t = config.position.gps_update_interval;
    bool gps_enabled = config.position.gps_enabled;

    // We'll not need the GPS thread to wake up again after first acq. with fixed position.
    if (!gps_enabled || config.position.fixed_position)
        t = UINT32_MAX; // Sleep forever now

    if (t == UINT32_MAX)
        return t; // already maxint

    return t * 1000;
}

void GPS::publishUpdate()
{
    if (shouldPublish) {
        shouldPublish = false;

        // In debug logs, identify position by @timestamp:stage (stage 2 = publish)
        LOG_DEBUG("publishing pos@%x:2, hasVal=%d, GPSlock=%d\n", p.timestamp, hasValidLocation, hasLock());

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status = meshtastic::GPSStatus(hasValidLocation, isConnected(), isPowerSaving(), p);
        newStatus.notifyObservers(&status);
    }
}

int32_t GPS::runOnce()
{
    // Repeaters have no need for GPS
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        disable();

    if (whileIdle()) {
        // if we have received valid NMEA claim we are connected
        setConnected();
    } else {
        if ((config.position.gps_enabled == 1) && (gnssModel == GNSS_MODEL_UBLOX)) {
            // reset the GPS on next bootup
            if (devicestate.did_gps_reset && (millis() > 60000) && !hasFlow()) {
                LOG_DEBUG("GPS is not communicating, trying factory reset on next bootup.\n");
                devicestate.did_gps_reset = false;
                nodeDB.saveDeviceStateToDisk();
                disable(); // Stop the GPS thread as it can do nothing useful until next reboot.
            }
        }
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();

    auto sleepTime = getSleepTime();
    if (!isAwake && sleepTime != UINT32_MAX && (now - lastSleepStartMsec) > sleepTime) {
        // We now want to be awake - so wake up the GPS
        setAwake(true);
    }

    // While we are awake
    if (isAwake) {
        // LOG_DEBUG("looking for location\n");
        if ((now - lastWhileActiveMsec) > 5000) {
            lastWhileActiveMsec = now;
            whileActive();
        }

        // If we've already set time from the GPS, no need to ask the GPS
        bool gotTime = (getRTCQuality() >= RTCQualityGPS);
        if (!gotTime && lookForTime()) { // Note: we count on this && short-circuiting and not resetting the RTC time
            gotTime = true;
            shouldPublish = true;
        }

        bool gotLoc = lookForLocation();
        if (gotLoc && !hasValidLocation) { // declare that we have location ASAP
            LOG_DEBUG("hasValidLocation RISING EDGE\n");
            hasValidLocation = true;
            shouldPublish = true;
        }

        // We've been awake too long - force sleep
        now = millis();
        auto wakeTime = getWakeTime();
        bool tooLong = wakeTime != UINT32_MAX && (now - lastWakeStartMsec) > wakeTime;

        // Once we get a location we no longer desperately want an update
        // LOG_DEBUG("gotLoc %d, tooLong %d, gotTime %d\n", gotLoc, tooLong, gotTime);
        if ((gotLoc && gotTime) || tooLong) {

            if (tooLong) {
                // we didn't get a location during this ack window, therefore declare loss of lock
                if (hasValidLocation) {
                    LOG_DEBUG("hasValidLocation FALLING EDGE (last read: %d)\n", gotLoc);
                }
                p = meshtastic_Position_init_default;
                hasValidLocation = false;
            }

            setAwake(false);
            shouldPublish = true; // publish our update for this just finished acquisition window
        }
    }

    // If state has changed do a publish
    publishUpdate();

    if (!(fixeddelayCtr >= 20) && config.position.fixed_position && hasValidLocation) {
        fixeddelayCtr++;
        // LOG_DEBUG("Our delay counter is %d\n", fixeddelayCtr);
        if (fixeddelayCtr >= 20) {
            doGPSpowersave(false);
            forceWake(false);
        }
    }
    // 9600bps is approx 1 byte per msec, so considering our buffer size we never need to wake more often than 200ms
    // if not awake we can run super infrquently (once every 5 secs?) to see if we need to wake.
    return isAwake ? GPS_THREAD_INTERVAL : 5000;
}

void GPS::forceWake(bool on)
{
    if (on) {
        LOG_DEBUG("Allowing GPS lock\n");
        // lastSleepStartMsec = 0; // Force an update ASAP
        wakeAllowed = true;
    } else {
        wakeAllowed = false;

        // Note: if the gps was already awake, we DO NOT shut it down, because we want to allow it to complete its lock
        // attempt even if we are in light sleep.  Once the attempt succeeds (or times out) we'll then shut it down.
        // setAwake(false);
    }
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareSleep(void *unused)
{
    LOG_INFO("GPS prepare sleep!\n");
    forceWake(false);

    return 0;
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareDeepSleep(void *unused)
{
    LOG_INFO("GPS deep sleep!\n");

    // For deep sleep we also want abandon any lock attempts (because we want minimum power)
    getSleepTime();
    setAwake(false);

    return 0;
}

GnssModel_t GPS::probe()
{
    // return immediately if the model is set by the variant.h file
#ifdef GPS_UBLOX
    return GNSS_MODEL_UBLOX;
#elif defined(GPS_L76K)
    return GNSS_MODEL_MTK;
#else
    // we use autodetect, only T-BEAM S3 for now...
    uint8_t buffer[256];
    /*
     * The GNSS module information variable is temporarily placed inside the function body,
     * if it needs to be used elsewhere, it can be moved to the outside
     * */
    struct uBloxGnssModelInfo info;

    memset(&info, 0, sizeof(struct uBloxGnssModelInfo));

    // Close all NMEA sentences , Only valid for MTK platform
    _serial_gps->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(20);

    // Get version information
    _serial_gps->write("$PCAS06,0*1B\r\n");
    uint32_t startTimeout = millis() + 500;
    while (millis() < startTimeout) {
        if (_serial_gps->available()) {
            String ver = _serial_gps->readStringUntil('\r');
            // Get module info , If the correct header is returned,
            // it can be determined that it is the MTK chip
            int index = ver.indexOf("$");
            if (index != -1) {
                ver = ver.substring(index);
                if (ver.startsWith("$GPTXT,01,01,02")) {
                    LOG_INFO("L76K GNSS init succeeded, using L76K GNSS Module\n");
                    return GNSS_MODEL_MTK;
                }
            }
        }
    }

    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
    _serial_gps->write(cfg_rate, sizeof(cfg_rate));
    // Check that the returned response class and message ID are correct
    if (!getAck(buffer, 256, 0x06, 0x08)) {
        LOG_WARN("Failed to find UBlox & MTK GNSS Module\n");
        return GNSS_MODEL_UNKONW;
    }

    //  Get Ublox gnss module hardware and software info
    uint8_t cfg_get_hw[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
    _serial_gps->write(cfg_get_hw, sizeof(cfg_get_hw));

    uint16_t len = getAck(buffer, 256, 0x0A, 0x04);
    if (len) {

        uint16_t position = 0;
        for (int i = 0; i < 30; i++) {
            info.swVersion[i] = buffer[position];
            position++;
        }
        for (int i = 0; i < 10; i++) {
            info.hwVersion[i] = buffer[position];
            position++;
        }

        while (len >= position + 30) {
            for (int i = 0; i < 30; i++) {
                info.extension[info.extensionNo][i] = buffer[position];
                position++;
            }
            info.extensionNo++;
            if (info.extensionNo > 9)
                break;
        }

        LOG_DEBUG("Module Info : \n");
        LOG_DEBUG("Soft version: %s\n", info.swVersion);
        LOG_DEBUG("Hard version: %s\n", info.hwVersion);
        LOG_DEBUG("Extensions:%d\n", info.extensionNo);
        for (int i = 0; i < info.extensionNo; i++) {
            LOG_DEBUG("  %s\n", info.extension[i]);
        }

        memset(buffer, 0, sizeof(buffer));

        // tips: extensionNo field is 0 on some 6M GNSS modules
        for (int i = 0; i < info.extensionNo; ++i) {
            if (!strncmp(info.extension[i], "OD=", 3)) {
                strncpy((char *)buffer, &(info.extension[i][3]), sizeof(buffer));
                LOG_DEBUG("GetModel:%s\n", (char *)buffer);
            }
        }
    }

    if (strlen((char *)buffer)) {
        LOG_INFO("UBlox GNSS init succeeded, using UBlox %s GNSS Module\n", buffer);
    } else {
        LOG_INFO("UBlox GNSS init succeeded, using UBlox GNSS Module\n");
    }

    return GNSS_MODEL_UBLOX;
#endif
}

#if HAS_GPS
#include "NMEAGPS.h"
#endif

GPS *createGps()
{

#if !HAS_GPS
    return nullptr;
#else
    if (config.position.gps_enabled) {
#ifdef GPS_ALTITUDE_HAE
        LOG_DEBUG("Using HAE altitude model\n");
#else
        LOG_DEBUG("Using MSL altitude model\n");
#endif
        if (GPS::_serial_gps) {
            // Some boards might have only the TX line from the GPS connected, in that case, we can't configure it at all.  Just
            // assume NMEA at 9600 baud.
            GPS *new_gps = new NMEAGPS();
            new_gps->setup();
            return new_gps;
        }
    } else {
        GPS *new_gps = new NMEAGPS();
        new_gps->setup();
        return new_gps;
    }
    return nullptr;
#endif
}
