#pragma once

// Connect to the WiFi AP (C5-A peer).
// Returns true when connected, false on timeout.
bool wifiConnect(const char *ssid, const char *pass, uint32_t timeout_ms);

// Download the file at `url` and save it to LittleFS at `dest_path`.
// Overwrites any existing file. Returns true on success.
bool downloadFile(const char *url, const char *dest_path);

// Download both firmware files (init packet + binary) needed for Nordic DFU.
bool downloadFirmware(const char *url_dat, const char *path_dat,
                      const char *url_bin, const char *path_bin);
