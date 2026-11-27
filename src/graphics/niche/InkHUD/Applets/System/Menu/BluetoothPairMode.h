#pragma once

#include "mesh/generated/meshtastic/config.pb.h"

// Pair Mode toggle for the InkHUD Bluetooth menu.
// Flips between RANDOM_PIN and FIXED_PIN. NO_PIN is not part of the toggle and is returned unchanged.
inline meshtastic_Config_BluetoothConfig_PairingMode
nextBluetoothPairMode(meshtastic_Config_BluetoothConfig_PairingMode mode)
{
    switch (mode) {
    case meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN:
        return meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    case meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN:
        return meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    default:
        return mode;
    }
}

// Menu label for the current pairing mode.
inline const char *bluetoothPairModeLabel(meshtastic_Config_BluetoothConfig_PairingMode mode)
{
    switch (mode) {
    case meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN:
        return "Pair Mode: Fixed";
    case meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN:
        return "Pair Mode: No PIN";
    default:
        return "Pair Mode: Random";
    }
}
