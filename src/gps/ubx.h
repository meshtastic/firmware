uint8_t GPS::_message_PMREQ[] PROGMEM = {
    0x00, 0x00, // 4 bytes duration of request task
    0x00, 0x00, // (milliseconds)
    0x02, 0x00, // Task flag bitfield
    0x00, 0x00, // byte index 1 = sleep mode
};

uint8_t GPS::_message_PMREQ_10[] PROGMEM = {
    0x00, 0x00,                                    // 4 bytes duration of request task
    0x00, 0x00,                                    // (milliseconds)
    0x02, 0x00,                                    // Task flag bitfield
    0x00, 0x00,                                    // byte index 1 = sleep mode
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // wakeupSources
};

const uint8_t GPS::_message_CFG_RXM_PSM[] PROGMEM = {
    0x08, // Reserved
    0x01  // Power save mode
};

// only for Neo-6
const uint8_t GPS::_message_CFG_RXM_ECO[] PROGMEM = {
    0x08, // Reserved
    0x04  // eco mode
};

const uint8_t GPS::_message_CFG_PM2[] PROGMEM = {
    0x01,                   // version
    0x00,                   // Reserved 1, set to 0x06 by u-Center
    0x00,                   // Reserved 2
    0x00,                   // Reserved 1
    0x00, 0x11, 0x03, 0x00, // flags-> cyclic mode, wait for normal fix ok, do not wake to update RTC, doNotEnterOff,
                            // LimitPeakCurrent
    0xE8, 0x03, 0x00, 0x00, // update period 1000 ms
    0x10, 0x27, 0x00, 0x00, // search period 10s
    0x00, 0x00, 0x00, 0x00, // Grid offset 0
    0x01, 0x00,             // onTime 1 second
    0x00, 0x00,             // min search time 0
    0x00, 0x00,             // 0x2C, 0x01,  // reserved 4
    0x00, 0x00,             // 0x00, 0x00,  // reserved 5
    0x00, 0x00, 0x00, 0x00, // 0x4F, 0xC1, 0x03, 0x00, // reserved 6
    0x00, 0x00, 0x00, 0x00, // 0x87, 0x02, 0x00, 0x00, // reserved 7
    0x00,                   // 0xFF,        // reserved 8
    0x00,                   // 0x00,        // reserved 9
    0x00, 0x00,             // 0x00, 0x00,  // reserved 10
    0x00, 0x00, 0x00, 0x00  // 0x64, 0x40, 0x01, 0x00  // reserved 11
};

const uint8_t GPS::_message_GNSS_7[] = {
    0x00, // msgVer (0 for this version)
    0x00, // numTrkChHw (max number of hardware channels, read only, so it's always 0)
    0xff, // numTrkChUse (max number of channels to use, 0xff = max available)
    0x02, // numConfigBlocks (number of GNSS systems), most modules support maximum 3 GNSS systems
    // GNSS config format: gnssId, resTrkCh, maxTrkCh, reserved1, flags
    0x00, 0x08, 0x10, 0x00, 0x01, 0x00, 0x00, 0x01, // GPS
    0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x01  // SBAS
};

// It's not critical if the module doesn't acknowledge this configuration.
// The module should operate adequately with its factory or previously saved settings.
// It appears that there is a firmware bug in some GPS modules: When an attempt is made
// to overwrite a saved state with identical values, no ACK/NAK is received, contrary to
// what is specified in the Ublox documentation.
// There is also a possibility that the module may be GPS-only.

// For M8 GPS, GLONASS, Galileo, SBAS, QZSS
const uint8_t GPS::_message_GNSS_8[] = {
    0x00,                                           // msgVer (0 for this version)
    0x00,                                           // numTrkChHw (max number of hardware channels, read only, so it's always 0)
    0xff,                                           // numTrkChUse (max number of channels to use, 0xff = max available)
    0x05,                                           // numConfigBlocks (number of GNSS systems)
                                                    // GNSS config format: gnssId, resTrkCh, maxTrkCh, reserved1, flags
    0x00, 0x08, 0x10, 0x00, 0x01, 0x00, 0x01, 0x01, // GPS
    0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, // SBAS
    0x02, 0x04, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, // Galileo
    0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, // QZSS
    0x06, 0x08, 0x0E, 0x00, 0x01, 0x00, 0x01, 0x01  // GLONASS
};
/*
// For M8 GPS, GLONASS, BeiDou, SBAS, QZSS
const uint8_t GPS::_message_GNSS_8_B[] = {
 0x00, // msgVer (0 for this version)
 0x00, // numTrkChHw (max number of hardware channels, read only, so it's always 0)
 0xff, // numTrkChUse (max number of channels to use, 0xff = max available) read only for protocol >23
 0x05, // numConfigBlocks (number of GNSS systems)
       // GNSS config format: gnssId, resTrkCh, maxTrkCh, reserved1, flags
 0x00, 0x08, 0x10, 0x00, 0x01, 0x00, 0x01, 0x01, // GPS
 0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, // SBAS
 0x03, 0x08, 0x10, 0x00, 0x01, 0x00, 0x01, 0x01, // BeiDou
 0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, // QZSS
 0x06, 0x08, 0x0E, 0x00, 0x01, 0x00, 0x01, 0x01  // GLONASS
};
*/

// For M8 we want to enable NMEA version 4.10 messages to allow for Galileo and or BeiDou
const uint8_t GPS::_message_NMEA[]{
    0x00,                              // filter flags
    0x41,                              // NMEA Version
    0x00,                              // Max number of SVs to report per TaklerId
    0x02,                              // flags
    0x00, 0x00, 0x00, 0x00,            // gnssToFilter
    0x00,                              // svNumbering
    0x00,                              // mainTalkerId
    0x00,                              // gsvTalkerId
    0x01,                              // Message version
    0x00, 0x00,                        // bdsTalkerId 2 chars 0=default
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // Reserved
};
// Enable jamming/interference monitor

// For Neo-6, Max-7 and Neo-7
const uint8_t GPS::_message_JAM_6_7[] = {
    0xf3, 0xac, 0x62, 0xad, // config1 bbThreshold = 3, cwThreshold = 15, enable = 1, reserved bits 0x16B156
    0x1e, 0x03, 0x00, 0x00  // config2 antennaSetting Unknown = 0, reserved 3, = 0x00,0x00, reserved 2 = 0x31E
};

// For M8
const uint8_t GPS::_message_JAM_8[] = {
    0xf3, 0xac, 0x62, 0xad, // config1 bbThreshold = 3, cwThreshold = 15, enable1 = 1, reserved bits 0x16B156
    0x1e, 0x43, 0x00, 0x00  // config2 antennaSetting Unknown = 0, enable2 = 1, generalBits = 0x31E
};

// Configure navigation engine expert settings:
// there are many variations of what were Reserved fields for the Neo-6 in later versions
// ToDo: check UBX-MON-VER for module type and protocol version

// For the Neo-6
const uint8_t GPS::_message_NAVX5[] = {
    0x00, 0x00,             // msgVer (0 for this version)
    0x4c, 0x66,             // mask1
    0x00, 0x00, 0x00, 0x00, // Reserved 0
    0x00,                   // Reserved 1
    0x00,                   // Reserved 2
    0x03,                   // minSVs (Minimum number of satellites for navigation) = 3
    0x10,                   // maxSVs (Maximum number of satellites for navigation) = 16
    0x06,                   // minCNO (Minimum satellite signal level for navigation) = 6 dBHz
    0x00,                   // Reserved 5
    0x00,                   // iniFix3D (Initial fix must be 3D) (0 = false 1 = true)
    0x00,                   // Reserved 6
    0x00,                   // Reserved 7
    0x00,                   // Reserved 8
    0x00, 0x00,             // wknRollover 0 = firmware default
    0x00, 0x00, 0x00, 0x00, // Reserved 9
    0x00,                   // Reserved 10
    0x00,                   // Reserved 11
    0x00,                   // usePPP (Precice Point Positioning) (0 = false, 1 = true)
    0x01,                   // useAOP  (AssistNow Autonomous configuration) = 1 (enabled)
    0x00,                   // Reserved 12
    0x00,                   // Reserved 13
    0x00, 0x00,             // aopOrbMaxErr = 0 to reset to firmware default
    0x00,                   // Reserved 14
    0x00,                   // Reserved 15
    0x00, 0x00,             // Reserved 3
    0x00, 0x00, 0x00, 0x00  // Reserved 4
};
// For the M8
const uint8_t GPS::_message_NAVX5_8[] = {
    0x02, 0x00,             // msgVer (2 for this version)
    0x4c, 0x66,             // mask1
    0x00, 0x00, 0x00, 0x00, // mask2
    0x00, 0x00,             // Reserved 1
    0x03,                   // minSVs (Minimum number of satellites for navigation) = 3
    0x10,                   // maxSVs (Maximum number of satellites for navigation) = 16
    0x06,                   // minCNO (Minimum satellite signal level for navigation) = 6 dBHz
    0x00,                   // Reserved 2
    0x00,                   // iniFix3D (Initial fix must be 3D) (0 = false 1 = true)
    0x00, 0x00,             // Reserved 3
    0x00,                   // ackAiding
    0x00, 0x00,             // wknRollover 0 = firmware default
    0x00,                   // sigAttenCompMode
    0x00,                   // Reserved 4
    0x00, 0x00,             // Reserved 5
    0x00, 0x00,             // Reserved 6
    0x00,                   // usePPP (Precice Point Positioning) (0 = false, 1 = true)
    0x01,                   // aopCfg  (AssistNow Autonomous configuration) = 1 (enabled)
    0x00, 0x00,             // Reserved 7
    0x00, 0x00,             // aopOrbMaxErr = 0 to reset to firmware default
    0x00, 0x00, 0x00, 0x00, // Reserved 8
    0x00, 0x00, 0x00,       // Reserved 9
    0x00                    // useAdr
};

// Set GPS update rate to 1Hz
// Lowering the update rate helps to save power.
// Additionally, for some new modules like the M9/M10, an update rate lower than 5Hz
// is recommended to avoid a known issue with satellites disappearing.
// The module defaults for M8, M9, M10 are the same as we use here so no update is necessary
const uint8_t GPS::_message_1HZ[] = {
    0xE8, 0x03, // Measurement Rate (1000ms for 1Hz)
    0x01, 0x00, // Navigation rate, always 1 in GPS mode
    0x01, 0x00  // Time reference
};

// Disable GLL. GLL - Geographic position (latitude and longitude), which provides the current geographical
// coordinates.
const uint8_t GPS::_message_GLL[] = {
    0xF0, 0x01, // NMEA ID for GLL
    0x00,       // Rate for DDC
    0x00,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// Enable GSA. GSA - GPS DOP and active satellites, used for detailing the satellites used in the positioning and
// the DOP (Dilution of Precision)
const uint8_t GPS::_message_GSA[] = {
    0xF0, 0x02, // NMEA ID for GSA
    0x00,       // Rate for DDC
    0x01,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// Disable GSV. GSV - Satellites in view, details the number and location of satellites in view.
const uint8_t GPS::_message_GSV[] = {
    0xF0, 0x03, // NMEA ID for GSV
    0x00,       // Rate for DDC
    0x00,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// Disable VTG. VTG - Track made good and ground speed, which provides course and speed information relative to
// the ground.
const uint8_t GPS::_message_VTG[] = {
    0xF0, 0x05, // NMEA ID for VTG
    0x00,       // Rate for DDC
    0x00,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// Enable RMC. RMC - Recommended Minimum data, the essential gps pvt (position, velocity, time) data.
const uint8_t GPS::_message_RMC[] = {
    0xF0, 0x04, // NMEA ID for RMC
    0x00,       // Rate for DDC
    0x01,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// Enable GGA. GGA - Global Positioning System Fix Data, which provides 3D location and accuracy data.
const uint8_t GPS::_message_GGA[] = {
    0xF0, 0x00, // NMEA ID for GGA
    0x00,       // Rate for DDC
    0x01,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// Disable UBX-AID-ALPSRV as it may confuse TinyGPS. The Neo-6 seems to send this message
// whether the AID Autonomous is enabled or not
const uint8_t GPS::_message_AID[] = {
    0x0B, 0x32, // NMEA ID for UBX-AID-ALPSRV
    0x00,       // Rate for DDC
    0x00,       // Rate for UART1
    0x00,       // Rate for UART2
    0x00,       // Rate for USB
    0x00,       // Rate for SPI
    0x00        // Reserved
};

// The Power Management configuration allows the GPS module to operate in different power modes for optimized
// power consumption. The modes supported are: 0x00 = Full power: The module operates at full power with no power
// saving. 0x01 = Balanced: The module dynamically adjusts the tracking behavior to balance power consumption.
// 0x02 = Interval: The module operates in a periodic mode, cycling between tracking and power saving states.
// 0x03 = Aggressive with 1 Hz: The module operates in a power saving mode with a 1 Hz update rate.
// 0x04 = Aggressive with 2 Hz: The module operates in a power saving mode with a 2 Hz update rate.
// 0x05 = Aggressive with 4 Hz: The module operates in a power saving mode with a 4 Hz update rate.
// The 'period' field specifies the position update and search period. It is only valid when the powerSetupValue
// is set to Interval; otherwise, it must be set to '0'. The 'onTime' field specifies the duration of the ON phase
// and must be smaller than the period. It is only valid when the powerSetupValue is set to Interval; otherwise,
// it must be set to '0'.
// This command applies to M8 and higher products
const uint8_t GPS::_message_PMS[] = {
    0x00,       // Version (0)
    0x03,       // Power setup value 3 = Agresssive 1Hz
    0x00, 0x00, // period: not applicable, set to 0
    0x00, 0x00, // onTime: not applicable, set to 0
    0x00, 0x00  // reserved, generated by u-center
};

const uint8_t GPS::_message_SAVE[] = {
    0x00, 0x00, 0x00, 0x00, // clearMask: no sections cleared
    0xFF, 0xFF, 0x00, 0x00, // saveMask: save all sections
    0x00, 0x00, 0x00, 0x00, // loadMask: no sections loaded
    0x17                    // deviceMask: BBR, Flash, EEPROM, and SPI Flash
};