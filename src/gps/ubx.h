const uint8_t GPS::_message_PMREQ[] PROGMEM = {
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
    0x0e, 0x81, 0x42, 0x01, // flags
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

const uint8_t GPS::_message_1HZ[] = {
    0xE8, 0x03, // Measurement Rate (1000ms for 1Hz)
    0x01, 0x00, // Navigation rate, always 1 in GPS mode
    0x01, 0x00, // Time reference
};