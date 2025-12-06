/**************************************************************************/
/*!
    @file     BLEDfu.cpp
    @author   hathach (tinyusb.org)

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2018, Adafruit Industries (adafruit.com)
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**************************************************************************/

#include "BLEDfuSecure.h"
#include "bluefruit.h"

#define DFU_REV_APPMODE 0x0001

const uint16_t UUID16_SVC_DFU_OTA = 0xFE59;

const uint8_t UUID128_CHR_DFU_CONTROL[16] = {0x50, 0xEA, 0xDA, 0x30, 0x88, 0x83, 0xB8, 0x9F,
                                             0x60, 0x4F, 0x15, 0xF3, 0x03, 0x00, 0xC9, 0x8E};

extern "C" void bootloader_util_app_start(uint32_t start_addr);

static uint16_t crc16(const uint8_t *data_p, uint8_t length)
{
    uint16_t crc = 0xFFFF;

    while (length--) {
        uint8_t x = crc >> 8 ^ *data_p++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t)(x << 12)) ^ ((uint16_t)(x << 5)) ^ ((uint16_t)x);
    }
    return crc;
}

static void bledfu_control_wr_authorize_cb(uint16_t conn_hdl, BLECharacteristic *chr, ble_gatts_evt_write_t *request)
{
    if ((request->handle == chr->handles().value_handle) && (request->op != BLE_GATTS_OP_PREP_WRITE_REQ) &&
        (request->op != BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) && (request->op != BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)) {
        BLEConnection *conn = Bluefruit.Connection(conn_hdl);

        ble_gatts_rw_authorize_reply_params_t reply = {.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE};

        if (!chr->indicateEnabled(conn_hdl)) {
            reply.params.write.gatt_status = BLE_GATT_STATUS_ATTERR_CPS_CCCD_CONFIG_ERROR;
            sd_ble_gatts_rw_authorize_reply(conn_hdl, &reply);
            return;
        }

        reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
        sd_ble_gatts_rw_authorize_reply(conn_hdl, &reply);

        enum { START_DFU = 1 };
        if (request->data[0] == START_DFU) {
            // Peer data information so that bootloader could re-connect after reboot
            typedef struct {
                ble_gap_addr_t addr;
                ble_gap_irk_t irk;
                ble_gap_enc_key_t enc_key;
                uint8_t sys_attr[8];
                uint16_t crc16;
            } peer_data_t;

            VERIFY_STATIC(offsetof(peer_data_t, crc16) == 60);

            /* Save Peer data
             * Peer data address is defined in bootloader linker @0x20007F80
             * - If bonded : save Security information
             * - Otherwise : save Address for direct advertising
             *
             * TODO may force bonded only for security reason
             */
            peer_data_t *peer_data = (peer_data_t *)(0x20007F80UL);
            varclr(peer_data);

            // Get CCCD
            uint16_t sysattr_len = sizeof(peer_data->sys_attr);
            sd_ble_gatts_sys_attr_get(conn_hdl, peer_data->sys_attr, &sysattr_len, BLE_GATTS_SYS_ATTR_FLAG_SYS_SRVCS);

            // Get Bond Data or using Address if not bonded
            peer_data->addr = conn->getPeerAddr();

            if (conn->secured()) {
                bond_keys_t bkeys;
                if (conn->loadBondKey(&bkeys)) {
                    peer_data->addr = bkeys.peer_id.id_addr_info;
                    peer_data->irk = bkeys.peer_id.id_info;
                    peer_data->enc_key = bkeys.own_enc;
                }
            }

            // Calculate crc
            peer_data->crc16 = crc16((uint8_t *)peer_data, offsetof(peer_data_t, crc16));

            // Initiate DFU Sequence and reboot into DFU OTA mode
            Bluefruit.Advertising.restartOnDisconnect(false);
            conn->disconnect();

            NRF_POWER->GPREGRET = 0xB1;
            NVIC_SystemReset();
        }
    }
}

BLEDfuSecure::BLEDfuSecure(void) : BLEService(UUID16_SVC_DFU_OTA), _chr_control(UUID128_CHR_DFU_CONTROL) {}

err_t BLEDfuSecure::begin(void)
{
    // Invoke base class begin()
    VERIFY_STATUS(BLEService::begin());

    _chr_control.setProperties(CHR_PROPS_WRITE | CHR_PROPS_INDICATE);
    _chr_control.setMaxLen(23);
    _chr_control.setWriteAuthorizeCallback(bledfu_control_wr_authorize_cb);
    VERIFY_STATUS(_chr_control.begin());

    return ERROR_NONE;
}
