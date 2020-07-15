#pragma once

#include <Arduino.h>

/**
 * Common lib functions for all platforms that have bluetooth
 */

#define MESH_SERVICE_UUID "6ba1b218-15a8-461f-9fa8-5dcae273eafd"

#define TORADIO_UUID "f75c76d2-129e-4dad-a1dd-7866124401e7"
#define FROMRADIO_UUID "8ba2bcc2-ee02-4a55-a531-c525c5e454d5"
#define FROMNUM_UUID "ed9da18c-a800-4f66-a670-aa7547e34453"

// NRF52 wants these constants without the hypen and I'm lazy
#define MESH_SERVICE_UUID_16 ((const uint8_t *)"6ba1b21815a8461f9fa85dcae273eafd")
#define TORADIO_UUID_16 ((const uint8_t *)"f75c76d2129e4dada1dd7866124401e7")
#define FROMRADIO_UUID_16 ((const uint8_t *)"8ba2bcc2ee024a55a531c525c5e454d5")
#define FROMNUM_UUID_16 ((const uint8_t *)"ed9da18ca8004f66a670aa7547e34453")

/// Given a level between 0-100, update the BLE attribute
void updateBatteryLevel(uint8_t level);