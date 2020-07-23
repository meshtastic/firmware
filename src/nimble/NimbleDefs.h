#pragma once

// Keep nimble #defs from messing up the build
#ifndef max
#define max max
#define min min
#endif

#include "esp_nimble_hci.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

int toradio_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

int fromradio_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

int fromnum_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

extern const struct ble_gatt_svc_def gatt_svr_svcs[];

extern const ble_uuid128_t mesh_service_uuid, fromnum_uuid;

#ifdef __cplusplus
};
#endif