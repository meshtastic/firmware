// What bin/platformio-custom.py emits for a userPrefs.jsonc configuring a beacon by value. The
// by-value shape is the one no other suite reaches: a vendor build writes it straight into
// moduleConfig, so it never passes through the admin handler that validates everything else.

#pragma once

#define USERPREFS_MESH_BEACON_LISTEN_ENABLED true
#define USERPREFS_MESH_BEACON_BROADCAST_ENABLED true
#define USERPREFS_MESH_BEACON_MESSAGE "Join the county mesh"

// Below the 3600s floor, to prove a vendor build cannot ship a beacon that talks over the mesh.
#define USERPREFS_MESH_BEACON_INTERVAL_SECS 60

// Name longer than ChannelSettings.name (char[12]), so the strncpy bound is exercised.
#define USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME "OfferedCountyMesh"
#define USERPREFS_MESH_BEACON_OFFER_CHANNEL_PSK                                                                                  \
    {0x38, 0x4b, 0xbc, 0xc0, 0x1d, 0xc0, 0x22, 0xd1, 0x81, 0xbf, 0x36, 0xb8, 0x61, 0x21, 0xe1, 0xfb,                             \
     0x96, 0xb7, 0x2e, 0x55, 0xbf, 0x74, 0x22, 0x7e, 0x9d, 0x6a, 0xfb, 0x48, 0xd6, 0x4c, 0xb1, 0xa1}
#define USERPREFS_MESH_BEACON_OFFER_REGION meshtastic_Config_LoRaConfig_RegionCode_EU_868
#define USERPREFS_MESH_BEACON_OFFER_PRESET meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST
#define USERPREFS_MESH_BEACON_OFFER_FREQUENCY_SLOT 1

// The destination's RF settings ride on the target that uses them; with no channel index it
// transmits on the primary channel.
#define USERPREFS_MESH_BEACON_TARGET_0_REGION meshtastic_Config_LoRaConfig_RegionCode_EU_868
#define USERPREFS_MESH_BEACON_TARGET_0_PRESET meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW
#define USERPREFS_MESH_BEACON_TARGET_0_FREQUENCY_SLOT 1
