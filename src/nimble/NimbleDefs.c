#include "NimbleDefs.h"

// NRF52 wants these constants as byte arrays
// Generated here https://yupana-engineering.com/online-uuid-to-c-array-converter - but in REVERSE BYTE ORDER
const ble_uuid128_t mesh_service_uuid =
    BLE_UUID128_INIT(0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f, 0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b);

static const ble_uuid128_t toradio_uuid =
    BLE_UUID128_INIT(0xe7, 0x01, 0x44, 0x12, 0x66, 0x78, 0xdd, 0xa1, 0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7);

static const ble_uuid128_t fromradio_uuid =
    BLE_UUID128_INIT(0xd5, 0x54, 0xe4, 0xc5, 0x25, 0xc5, 0x31, 0xa5, 0x55, 0x4a, 0x02, 0xee, 0xc2, 0xbc, 0xa2, 0x8b);

const ble_uuid128_t fromnum_uuid =
    BLE_UUID128_INIT(0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6, 0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed);

const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service: Security test. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &mesh_service_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){{
                                            .uuid = &toradio_uuid.u,
                                            .access_cb = toradio_callback,
                                            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
                                        },
                                        {
                                            .uuid = &fromradio_uuid.u,
                                            .access_cb = fromradio_callback,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN,
                                        },
                                        {
                                            .uuid = &fromnum_uuid.u,
                                            .access_cb = fromnum_callback,
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
