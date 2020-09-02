#pragma once

#include "nimble/NimbleDefs.h"

void reinitUpdateService();

void bluetoothRebootCheck();

#ifdef __cplusplus
extern "C" {
#endif

int update_size_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
int update_data_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
int update_result_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
int update_crc32_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

extern const struct ble_gatt_svc_def gatt_update_svcs[];

extern const ble_uuid128_t update_result_uuid;

extern int16_t updateResultHandle;

#ifdef __cplusplus
};
#endif