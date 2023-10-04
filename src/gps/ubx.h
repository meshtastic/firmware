uint8_t GPS::_message_PMREQ[] PROGMEM = {
    0x00, 0x00, // 4 bytes duration of request task
    0x00, 0x00, // (milliseconds)
    0x02, 0x00, // Task flag bitfield
    0x00, 0x00  // byte index 1 = sleep mode
};

const uint8_t GPS::_message_CFG_RXM_PSM[] PROGMEM = {
    0x08, // Reserved
    0x01  // Power save mode
};

const uint8_t GPS::_message_CFG_RXM_ECO[] PROGMEM = {
    0x08, // Reserved
    0x04  // eco mode
};

const uint8_t GPS::_message_CFG_PM2[] PROGMEM = {
    0x01, 0x06, 0x00, 0x00, // version, Reserved
    0x0E, 0x81, 0x43, 0x01, // flags
    0xE8, 0x03, 0x00, 0x00, // update period 1000 ms
    0x10, 0x27, 0x00, 0x00, // search period 10s
    0x00, 0x00, 0x00, 0x00, // Grod offset 0
    0x01, 0x00,             // onTime 1 second
    0x00, 0x00,             // min search time 0
    0x2C, 0x01,             // reserved
    0x00, 0x00, 0x4F, 0xC1, // reserved
    0x03, 0x00, 0x87, 0x02, // reserved
    0x00, 0x00, 0xFF, 0x00, // reserved
    0x01, 0x00, 0xD6, 0x4D  // reserved
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
const uint8_t GPS::_message_GNSS[] = {
    0x00, // msgVer (0 for this version)
    0x00, // numTrkChHw (max number of hardware channels, read only, so it's always 0)
    0xff, // numTrkChUse (max number of channels to use, 0xff = max available)
    0x03, // numConfigBlocks (number of GNSS systems), most modules support maximum 3 GNSS systems
    // GNSS config format: gnssId, resTrkCh, maxTrkCh, reserved1, flags
    0x00, 0x08, 0x10, 0x00, 0x01, 0x00, 0x01, 0x01, // GPS
    0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, // SBAS
    0x06, 0x08, 0x0e, 0x00, 0x01, 0x00, 0x01, 0x01  // GLONASS
};

// Enable interference resistance, because we are using LoRa, WiFi and Bluetooth on same board,
// and we need to reduce interference from them
const uint8_t GPS::_message_JAM[] = {
    // bbThreshold (Broadband jamming detection threshold) is set to 0x3F (63 in decimal)
    // cwThreshold (CW jamming detection threshold) is set to 0x10 (16 in decimal)
    // algorithmBits (Reserved algorithm settings) is set to 0x16B156 as recommended
    // enable (Enable interference detection) is set to 1 (enabled)
    0x3F, 0x10, 0xB1, 0x56, // config: Interference config word
    // generalBits (General settings) is set to 0x31E as recommended
    // antSetting (Antenna setting, 0=unknown, 1=passive, 2=active) is set to 0 (unknown)
    // ToDo: Set to 1 (passive) or 2 (active) if known, for example from UBX-MON-HW, or from board info
    // enable2 (Set to 1 to scan auxiliary bands, u-blox 8 / u-blox M8 only, otherwise ignored) is set to 1
    // (enabled)
    0x1E, 0x03, 0x00, 0x01 // config2: Extra settings for jamming/interference monitor
};

// Configure navigation engine expert settings:
const uint8_t GPS::_message_NAVX5[] = {
    0x00, 0x00, // msgVer (0 for this version)
    // minMax flag = 1: apply min/max SVs settings
    // minCno flag = 1: apply minimum C/N0 setting
    // initial3dfix flag = 0: apply initial 3D fix settings
    // aop flag = 1: apply aopCfg (useAOP flag) settings (AssistNow Autonomous)
    0x1B, 0x00, // mask1 (First parameters bitmask)
    // adr flag = 0: apply ADR sensor fusion on/off setting (useAdr flag)
    // If firmware is not ADR/UDR, enabling this flag will fail configuration
    // ToDo: check this with UBX-MON-VER
    0x00, 0x00, 0x00, 0x00,             // mask2 (Second parameters bitmask)
    0x00, 0x00,                         // Reserved
    0x03,                               // minSVs (Minimum number of satellites for navigation) = 3
    0x10,                               // maxSVs (Maximum number of satellites for navigation) = 16
    0x06,                               // minCNO (Minimum satellite signal level for navigation) = 6 dBHz
    0x00,                               // Reserved
    0x00,                               // iniFix3D (Initial fix must be 3D) = 0 (disabled)
    0x00, 0x00,                         // Reserved
    0x00,                               // ackAiding (Issue acknowledgements for assistance message input) = 0 (disabled)
    0x00, 0x00,                         // Reserved
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reserved
    0x00,                               // Reserved
    0x01,                               // aopCfg (AssistNow Autonomous configuration) = 1 (enabled)
    0x00, 0x00,                         // Reserved
    0x00, 0x00,                         // Reserved
    0x00, 0x00, 0x00, 0x00,             // Reserved
    0x00, 0x00, 0x00,                   // Reserved
    0x01,                               // useAdr (Enable/disable ADR sensor fusion) = 1 (enabled)
};

// Set GPS update rate to 1Hz
// Lowering the update rate helps to save power.
// Additionally, for some new modules like the M9/M10, an update rate lower than 5Hz
// is recommended to avoid a known issue with satellites disappearing.
const uint8_t GPS::_message_1HZ[] = {
    0xE8, 0x03, // Measurement Rate (1000ms for 1Hz)
    0x01, 0x00, // Navigation rate, always 1 in GPS mode
    0x01, 0x00, // Time reference
};

// Disable GGL. GGL - Geographic position (latitude and longitude), which provides the current geographical
// coordinates.
const uint8_t GPS::_message_GGL[] = {
    0xF0, 0x01,            // NMEA ID for GLL
    0x01,                  // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
    0x00,                  // Disable
    0x01, 0x01, 0x01, 0x01 // Reserved
};

// Enable GSA. GSA - GPS DOP and active satellites, used for detailing the satellites used in the positioning and
// the DOP (Dilution of Precision)
const uint8_t GPS::_message_GSA[] = {
    0xF0, 0x02,            // NMEA ID for GSA
    0x01,                  // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
    0x01,                  // Enable
    0x01, 0x01, 0x01, 0x01 // Reserved
};

// Disable GSV. GSV - Satellites in view, details the number and location of satellites in view.
const uint8_t GPS::_message_GSV[] = {
    0xF0, 0x03,            // NMEA ID for GSV
    0x01,                  // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
    0x00,                  // Disable
    0x01, 0x01, 0x01, 0x01 // Reserved
};

// Disable VTG. VTG - Track made good and ground speed, which provides course and speed information relative to
// the ground.
const uint8_t GPS::_message_VTG[] = {
    0xF0, 0x05,            // NMEA ID for VTG
    0x01,                  // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
    0x00,                  // Disable
    0x01, 0x01, 0x01, 0x01 // Reserved
};

// Enable RMC. RMC - Recommended Minimum data, the essential gps pvt (position, velocity, time) data.
const uint8_t GPS::_message_RMC[] = {
    0xF0, 0x04,            // NMEA ID for RMC
    0x01,                  // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
    0x01,                  // Enable
    0x01, 0x01, 0x01, 0x01 // Reserved
};

// Enable GGA. GGA - Global Positioning System Fix Data, which provides 3D location and accuracy data.
const uint8_t GPS::_message_GGA[] = {
    0xF0, 0x00,            // NMEA ID for GGA
    0x01,                  // I/O Target 0=I/O, 1=UART1, 2=UART2, 3=USB, 4=SPI
    0x01,                  // Enable
    0x01, 0x01, 0x01, 0x01 // Reserved
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
const uint8_t GPS::_message_PMS[] = {
    0x00,       // Version (0)
    0x03,       // Power setup value
    0x00, 0x00, // period: not applicable, set to 0
    0x00, 0x00, // onTime: not applicable, set to 0
    0x97, 0x6F  // reserved, generated by u-center
};

const uint8_t GPS::_message_SAVE[] = {
    0x00, 0x00, 0x00, 0x00, // clearMask: no sections cleared
    0xFF, 0xFF, 0x00, 0x00, // saveMask: save all sections
    0x00, 0x00, 0x00, 0x00, // loadMask: no sections loaded
    0x0F                    // deviceMask: BBR, Flash, EEPROM, and SPI Flash
};
