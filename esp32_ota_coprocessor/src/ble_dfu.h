#pragma once

// Run the full Nordic Secure DFU flow as BLE central:
//   1. Scan for "DfuTarg" (nRF52 in DFU bootloader mode)
//   2. Connect and discover FE59 service
//   3. Send init packet (.dat) via Control Point + Packet characteristics
//   4. Send firmware binary (.bin) in max-size segments
//   5. Execute → nRF52 validates, boots new firmware
//
// Blocks until completion or unrecoverable error.
// dat_path / bin_path: LittleFS paths to the downloaded firmware files.
// Returns true on success.
bool runBleNordicDfu(const char *dat_path, const char *bin_path);
