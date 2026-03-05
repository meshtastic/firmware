#pragma once

#if HAS_WIFI && defined(ARCH_ESP32)

#include <string>

// Start an async HTTP GET request. Only one request at a time.
// Returns true if the request was started, false on error.
bool app_http_request(const char *url);

// Poll for the async HTTP response.
// Returns true if the request is complete (result is set).
// Returns false if still pending (result is unchanged).
bool app_http_response(std::string &result);

// Cancel any in-flight request and free resources. Safe to call anytime.
void app_http_cleanup();

// Check if WiFi is connected and available.
bool app_http_is_connected();

#endif // HAS_WIFI && ARCH_ESP32
