#pragma once

// CASIC binary message definitions
// Reference: https://www.icofchina.com/d/file/xiazai/2020-09-22/20f1b42b3a11ac52089caf3603b43fb5.pdf
// ATGM33H-5N: https://www.icofchina.com/pro/mokuai/2016-08-01/4.html
// (https://www.icofchina.com/d/file/xiazai/2016-12-05/b5c57074f4b1fcc62ba8c7868548d18a.pdf)

// NEMA (Class ID - 0x4e) message IDs
#define CAS_NEMA_GGA 0x00
#define CAS_NEMA_GLL 0x01
#define CAS_NEMA_GSA 0x02
#define CAS_NEMA_GSV 0x03
#define CAS_NEMA_RMC 0x04
#define CAS_NEMA_VTG 0x05
#define CAS_NEMA_GST 0x07
#define CAS_NEMA_ZDA 0x08
#define CAS_NEMA_DHV 0x0D

// Size of a CAS-ACK-(N)ACK message (14 bytes)
#define CAS_ACK_NACK_MSG_SIZE 0x0E

// CFG-RST (0x06, 0x02)
// Factory reset
const uint8_t GPS::_message_CAS_CFG_RST_FACTORY[] = {
    0xFF, 0x03, // Fields to clear
    0x01,       // Reset Mode: Controlled Software reset
    0x03        // Startup Mode: Factory
};

// CFG_RATE (0x06, 0x01)
// 1HZ update rate, this should always be the case after
// factory reset but update it regardless
const uint8_t GPS::_message_CAS_CFG_RATE_1HZ[] = {
    0xE8, 0x03, // Update Rate: 0x03E8 = 1000ms
    0x00, 0x00  // Reserved
};

// CFG-NAVX (0x06, 0x07)
// Initial ATGM33H-5N configuration, Updates for Dynamic Mode, Fix Mode, and SV system
// Qwirk: The ATGM33H-5N-31 should only support GPS+BDS, however it will happily enable
//  and use GPS+BDS+GLONASS iff the correct CFG_NAVX command is used.
const uint8_t GPS::_message_CAS_CFG_NAVX_CONF[] = {
    0x03, 0x01, 0x00, 0x00, // Update Mask: Dynamic Mode, Fix Mode, Nav Settings
    0x03,                   // Dynamic Mode: Automotive
    0x03,                   // Fix Mode: Auto 2D/3D
    0x00,                   // Min SV
    0x00,                   // Max SVs
    0x00,                   // Min CNO
    0x00,                   // Reserved1
    0x00,                   // Init 3D fix
    0x00,                   // Min Elevation
    0x00,                   // Dr Limit
    0x07,                   // Nav System: 2^0 = GPS, 2^1 = BDS 2^2 = GLONASS: 2^3
                            // 3=GPS+BDS, 7=GPS+BDS+GLONASS
    0x00, 0x00,             // Rollover Week
    0x00, 0x00, 0x00, 0x00, // Fix Altitude
    0x00, 0x00, 0x00, 0x00, // Fix Height Error
    0x00, 0x00, 0x00, 0x00, // PDOP Maximum
    0x00, 0x00, 0x00, 0x00, // TDOP Maximum
    0x00, 0x00, 0x00, 0x00, // Position Accuracy Max
    0x00, 0x00, 0x00, 0x00, // Time Accuracy Max
    0x00, 0x00, 0x00, 0x00  // Static Hold Threshold
};