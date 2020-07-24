#include "BluetoothSoftwareUpdate.h"

// NRF52 wants these constants as byte arrays
// Generated here https://yupana-engineering.com/online-uuid-to-c-array-converter - but in REVERSE BYTE ORDER

// "cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30"
const ble_uuid128_t update_service_uuid =
    BLE_UUID128_INIT(0x30, 0xee, 0x44, 0x31, 0x2e, 0x44, 0xbb, 0xbd, 0x0d, 0x4c, 0x4c, 0xa8, 0x0b, 0x9a, 0x0b, 0xcb);

// "e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e" write|read
const ble_uuid128_t update_size_uuid =
    BLE_UUID128_INIT(0x1e, 0x8e, 0xea, 0xdb, 0xe1, 0xf0, 0xa1, 0x95, 0x6f, 0x4a, 0x01, 0xa3, 0xc0, 0xd9, 0x4d, 0xe7);

// "e272ebac-d463-4b98-bc84-5cc1a39ee517" write
const ble_uuid128_t update_data_uuid =
    BLE_UUID128_INIT(0x17, 0xe5, 0x9e, 0xa3, 0xc1, 0x5c, 0x84, 0xbc, 0x98, 0x4b, 0x63, 0xd4, 0xac, 0xeb, 0x72, 0xe2);

// "4826129c-c22a-43a3-b066-ce8f0d5bacc6" write
const ble_uuid128_t update_crc32_uuid =
    BLE_UUID128_INIT(0xc6, 0xac, 0x5b, 0x0d, 0x8f, 0xce, 0x66, 0xb0, 0xa3, 0x43, 0x2a, 0xc2, 0x9c, 0x12, 0x26, 0x48);

// "5e134862-7411-4424-ac4a-210937432c77" read|notify
const ble_uuid128_t update_result_uuid =
    BLE_UUID128_INIT(0x77, 0x2c, 0x43, 0x37, 0x09, 0x21, 0x4a, 0xac, 0x24, 0x44, 0x11, 0x74, 0x62, 0x48, 0x13, 0x5e);

const struct ble_gatt_svc_def gatt_update_svcs[] = {
    {
        /*** Service: Security test. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &update_service_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){{
                                            .uuid = &update_size_uuid.u,
                                            .access_cb = update_size_callback,
                                            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN | BLE_GATT_CHR_F_READ |
                                                     BLE_GATT_CHR_F_READ_AUTHEN,
                                        },
                                        {
                                            .uuid = &update_data_uuid.u,
                                            .access_cb = update_data_callback,
                                            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
                                        },
                                        {
                                            .uuid = &update_crc32_uuid.u,
                                            .access_cb = update_crc32_callback,
                                            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
                                        },
                                        {
                                            .uuid = &update_result_uuid.u,
                                            .access_cb = update_size_callback,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_NOTIFY,
                                        },
                                        {
                                            0, /* No more characteristics in this service. */
                                        }},
    },

    {
        0, /* No more services. */
    },
};
