#pragma once
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS

#include "GPSStatus.h"
#include "Observer.h"
#include "TinyGPS++.h"
#include "concurrency/OSThread.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "modules/PositionModule.h"

// Allow defining the polarity of the ENABLE output.  default is active high
#ifndef GPS_EN_ACTIVE
#define GPS_EN_ACTIVE 1
#endif

struct uBloxGnssModelInfo {
    char swVersion[30];
    char hwVersion[10];
    uint8_t extensionNo;
    char extension[10][30];
};

typedef enum {
    GNSS_MODEL_ATGM336H,
    GNSS_MODEL_MTK,
    GNSS_MODEL_UBLOX,
    GNSS_MODEL_UC6580,
    GNSS_MODEL_UNKNOWN,
    GNSS_MODEL_MTK_L76B
} GnssModel_t;

typedef enum {
    GNSS_RESPONSE_NONE,
    GNSS_RESPONSE_NAK,
    GNSS_RESPONSE_FRAME_ERRORS,
    GNSS_RESPONSE_OK,
} GPS_RESPONSE;

enum GPSPowerState : uint8_t {
    GPS_ACTIVE,    // Awake and want a position
    GPS_IDLE,      // Awake, but not wanting another position yet
    GPS_SOFTSLEEP, // Physically powered on, but soft-sleeping
    GPS_HARDSLEEP, // Physically powered off, but scheduled to wake
    GPS_OFF        // Powered off indefinitely
};

// Generate a string representation of DOP
const char *getDOPString(uint32_t dop);

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class GPS : private concurrency::OSThread
{
    TinyGPSPlus reader;
    uint8_t fixQual = 0; // fix quality from GPGGA
    uint32_t lastChecksumFailCount = 0;

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // (20210908) TinyGps++ can only read the GPGSA "FIX TYPE" field
    // via optional feature "custom fields", currently disabled (bug #525)
    TinyGPSCustom gsafixtype; // custom extract fix type from GPGSA
    TinyGPSCustom gsapdop;    // custom extract PDOP from GPGSA
    uint8_t fixType = 0;      // fix type from GPGSA
#endif
  private:
    const int serialSpeeds[6] = {9600, 4800, 38400, 57600, 115200, 9600};

    uint32_t rx_gpio = 0;
    uint32_t tx_gpio = 0;
    uint32_t en_gpio = 0;

    int speedSelect = 0;
    int probeTries = 2;

    /**
     * hasValidLocation - indicates that the position variables contain a complete
     *   GPS location, valid and fresh (< gps_update_interval + position_broadcast_secs)
     */
    bool hasValidLocation = false; // default to false, until we complete our first read

    bool isInPowersave = false;

    bool shouldPublish = false; // If we've changed GPS state, this will force a publish the next loop()

    bool hasGPS = false; // Do we have a GPS we are talking to

    bool GPSInitFinished = false; // Init thread finished?
    bool GPSInitStarted = false;  // Init thread finished?

    GPSPowerState powerState = GPS_OFF; // GPS_ACTIVE if we want a location right now

    uint8_t numSatellites = 0;

    CallbackObserver<GPS, void *> notifyDeepSleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareDeepSleep);

  public:
    /** If !NULL we will use this serial port to construct our GPS */
#if defined(RPI_PICO_WAVESHARE)
    static SerialUART *_serial_gps;
#else
    static HardwareSerial *_serial_gps;
#endif
    static uint8_t _message_PMREQ[];
    static uint8_t _message_PMREQ_10[];
    static const uint8_t _message_CFG_RXM_PSM[];
    static const uint8_t _message_CFG_RXM_ECO[];
    static const uint8_t _message_CFG_PM2[];
    static const uint8_t _message_GNSS_7[];
    static const uint8_t _message_GNSS_8[];
    static const uint8_t _message_JAM_6_7[];
    static const uint8_t _message_JAM_8[];
    static const uint8_t _message_NAVX5[];
    static const uint8_t _message_NAVX5_8[];
    static const uint8_t _message_NMEA[];
    static const uint8_t _message_DISABLE_TXT_INFO[];
    static const uint8_t _message_1HZ[];
    static const uint8_t _message_GLL[];
    static const uint8_t _message_GSA[];
    static const uint8_t _message_GSV[];
    static const uint8_t _message_VTG[];
    static const uint8_t _message_RMC[];
    static const uint8_t _message_AID[];
    static const uint8_t _message_GGA[];
    static const uint8_t _message_PMS[];
    static const uint8_t _message_SAVE[];

    // VALSET Commands for M10
    static const uint8_t _message_VALSET_PM[];
    static const uint8_t _message_VALSET_PM_RAM[];
    static const uint8_t _message_VALSET_PM_BBR[];
    static const uint8_t _message_VALSET_ITFM_RAM[];
    static const uint8_t _message_VALSET_ITFM_BBR[];
    static const uint8_t _message_VALSET_DISABLE_NMEA_RAM[];
    static const uint8_t _message_VALSET_DISABLE_NMEA_BBR[];
    static const uint8_t _message_VALSET_DISABLE_TXT_INFO_RAM[];
    static const uint8_t _message_VALSET_DISABLE_TXT_INFO_BBR[];
    static const uint8_t _message_VALSET_ENABLE_NMEA_RAM[];
    static const uint8_t _message_VALSET_ENABLE_NMEA_BBR[];
    static const uint8_t _message_VALSET_DISABLE_SBAS_RAM[];
    static const uint8_t _message_VALSET_DISABLE_SBAS_BBR[];

    // CASIC commands for ATGM336H
    static const uint8_t _message_CAS_CFG_RST_FACTORY[];
    static const uint8_t _message_CAS_CFG_NAVX_CONF[];
    static const uint8_t _message_CAS_CFG_RATE_1HZ[];

    meshtastic_Position p = meshtastic_Position_init_default;

    GPS() : concurrency::OSThread("GPS") {}

    virtual ~GPS();

    /** We will notify this observable anytime GPS state has changed meaningfully */
    Observable<const meshtastic::GPSStatus *> newStatus;

    /**
     * Returns true if we succeeded
     */
    virtual bool setup();

    // re-enable the thread
    void enable();

    // Disable the thread
    int32_t disable() override;

    // toggle between enabled/disabled
    void toggleGpsMode();

    // Change the power state of the GPS - for power saving / shutdown
    void setPowerState(GPSPowerState newState, uint32_t sleepMs = 0);

    /// Returns true if we have acquired GPS lock.
    virtual bool hasLock();

    /// Returns true if there's valid data flow with the chip.
    virtual bool hasFlow();

    /// Return true if we are connected to a GPS
    bool isConnected() const { return hasGPS; }

    bool isPowerSaving() const { return config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED; }

    // Empty the input buffer as quickly as possible
    void clearBuffer();

    // Create a ublox packet for editing in memory
    uint8_t makeUBXPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg);
    uint8_t makeCASPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg);

    // scratch space for creating ublox packets
    uint8_t UBXscratch[250] = {0};

    int rebootsSeen = 0;

    int getACK(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID, uint32_t waitMillis);
    GPS_RESPONSE getACK(uint8_t c, uint8_t i, uint32_t waitMillis);
    GPS_RESPONSE getACK(const char *message, uint32_t waitMillis);

    GPS_RESPONSE getACKCas(uint8_t class_id, uint8_t msg_id, uint32_t waitMillis);

    virtual bool factoryReset();

    // Creates an instance of the GPS class.
    // Returns the new instance or null if the GPS is not present.
    static GPS *createGps();

    // Wake the GPS hardware - ready for an update
    void up();

    // Let the GPS hardware save power between updates
    void down();

  protected:
    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */

    /// Record that we have a GPS
    void setConnected();

    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     *
     * Return true if we received a valid message from the GPS
     */
    virtual bool whileActive();

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */
    virtual bool lookForTime();

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */
    virtual bool lookForLocation();

  private:
    /// Prepare the GPS for the cpu entering deep sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareDeepSleep(void *unused);

    // Calculate checksum
    void UBXChecksum(uint8_t *message, size_t length);
    void CASChecksum(uint8_t *message, size_t length);

    /** Set power with EN pin, if relevant
     */
    void writePinEN(bool on);

    /** Set the value of the STANDBY pin, if relevant
     */
    void writePinStandby(bool standby);

    /** Set GPS power with PMU, if relevant
     */
    void setPowerPMU(bool on);

    /** Set UBLOX power, if relevant
     */
    void setPowerUBLOX(bool on, uint32_t sleepMs = 0);

    /**
     * Tell users we have new GPS readings
     */
    void publishUpdate();

    virtual int32_t runOnce() override;

    // Get GNSS model
    String getNMEA();
    GnssModel_t probe(int serialSpeed);

    // delay counter to allow more sats before fixed position stops GPS thread
    uint8_t fixeddelayCtr = 0;

    const char *powerStateToString();

  protected:
    GnssModel_t gnssModel = GNSS_MODEL_UNKNOWN;
};

extern GPS *gps;
#endif // Exclude GPS