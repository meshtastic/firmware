//
// ch341eeprom programmer version 0.1 (Beta)
//
//  Programming tool for the 24Cxx serial EEPROMs using the Winchiphead CH341A IC
//
// (c) December 2011 asbokid <ballymunboy@gmail.com>
//  (c) August 2023 Mikhail Medvedev <e-ink-reader@yandex.ru>
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "ch341a_i2c.h"

// extern struct libusb_device_handle *handle;
unsigned char *readbuf;
uint32_t getnextpkt; // set by the callback function
uint32_t syncackpkt; // synch / ack flag used by BULK OUT cb function
uint32_t byteoffset;

// callback functions for async USB transfers
static void cbBulkIn(struct libusb_transfer *transfer);
static void cbBulkOut(struct libusb_transfer *transfer);

void ch341ReadCmdMarshall(uint8_t *buffer, uint32_t addr, struct EEPROM *eeprom_info, uint32_t i2c_address)
{
    uint8_t *ptr = buffer;
    uint8_t msb_addr;
    uint32_t size_kb;

    *ptr++ = CH341_CMD_I2C_STREAM;  // 0
    *ptr++ = CH341_CMD_I2C_STM_STA; // 1
    // Write address
    *ptr++ = CH341_CMD_I2C_STM_OUT | ((*eeprom_info).addr_size + 1); // 2: I2C bus adddress + EEPROM address
    if ((*eeprom_info).addr_size >= 2) {
        // 24C32 and more
        msb_addr = addr >> 16 & (*eeprom_info).i2c_addr_mask;
        *ptr++ = (i2c_address | msb_addr) << 1; // 3
        *ptr++ = (addr >> 8 & 0xFF);            // 4
        *ptr++ = (addr >> 0 & 0xFF);            // 5
    } else {
        // 24C16 and less
        msb_addr = addr >> 8 & (*eeprom_info).i2c_addr_mask;
        *ptr++ = (i2c_address | msb_addr) << 1; // 3
        *ptr++ = (addr >> 0 & 0xFF);            // 4
    }
    // Read
    *ptr++ = CH341_CMD_I2C_STM_STA;               // 6/5
    *ptr++ = CH341_CMD_I2C_STM_OUT | 1;           // 7/6
    *ptr++ = ((i2c_address | msb_addr) << 1) | 1; // 8/7: Read command

    // Configuration?
    *ptr++ = 0xE0; // 9/8
    *ptr++ = 0x00; // 10/9
    if ((*eeprom_info).addr_size < 2)
        *ptr++ = 0x10; // x/10
    memcpy(ptr, "\x00\x06\x04\x00\x00\x00\x00\x00\x00", 9);
    ptr += 9; // 10
    size_kb = (*eeprom_info).size / 1024;
    *ptr++ = size_kb & 0xFF;        // 19
    *ptr++ = (size_kb >> 8) & 0xFF; // 20
    memcpy(ptr, "\x00\x00\x11\x4d\x40\x77\xcd\xab\xba\xdc", 10);
    ptr += 10;

    // Frame 2
    *ptr++ = CH341_CMD_I2C_STREAM;
    memcpy(ptr,
           "\xe0\x00\x00\xc4\xf1\x12\x00\x11\x4d\x40\x77\xf0\xf1\x12\x00"
           "\xd9\x8b\x41\x7e\x00\xe0\xfd\x7f\xf0\xf1\x12\x00\x5a\x88\x41\x7e",
           31);
    ptr += 31;

    // Frame 3
    *ptr++ = CH341_CMD_I2C_STREAM;
    memcpy(ptr,
           "\xe0\x00\x00\x2a\x88\x41\x7e\x06\x04\x00\x00\x11\x4d\x40\x77"
           "\xe8\xf3\x12\x00\x14\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00",
           31);
    ptr += 31;

    // Finalize
    *ptr++ = CH341_CMD_I2C_STREAM;  // 0xAA
    *ptr++ = 0xDF;                  // ???
    *ptr++ = CH341_CMD_I2C_STM_IN;  // 0xC0
    *ptr++ = CH341_CMD_I2C_STM_STO; // 0x75
    *ptr++ = CH341_CMD_I2C_STM_END; // 0x00

    assert(ptr - buffer == CH341_EEPROM_READ_CMD_SZ);
}

// --------------------------------------------------------------------------
// ch341readEEPROM()
//      read n bytes from device (in packets of 32 bytes)
int32_t ch341readEEPROM_param(uint8_t *buffer, uint32_t offset, uint32_t bytestoread, uint32_t ic_size, uint32_t block_size,
                              uint8_t algorithm, uint32_t i2c_address, struct libusb_device_handle *handle)
{

    uint8_t ch341outBuffer[EEPROM_READ_BULKOUT_BUF_SZ];
    uint8_t ch341inBuffer[IN_BUF_SZ]; // 0x100 bytes
    int32_t ret = 0, readpktcount = 0;
    struct libusb_transfer *xferBulkIn, *xferBulkOut;
    struct timeval tv = {0, 100}; // our async polling interval
    struct EEPROM eeprom_info;
    eeprom_info.name = "24c01";
    eeprom_info.size = ic_size;
    eeprom_info.page_size = (uint16_t)block_size;
    eeprom_info.addr_size = 0x0f & algorithm;
    eeprom_info.i2c_addr_mask = (0xf0 & algorithm) / 16;

    xferBulkIn = libusb_alloc_transfer(0);
    xferBulkOut = libusb_alloc_transfer(0);

    if (!xferBulkIn || !xferBulkOut) {
        printf("Couldnt allocate USB transfer structures\n");
        return -1;
    }

    byteoffset = 0;

    memset(ch341inBuffer, 0, EEPROM_READ_BULKIN_BUF_SZ);
    ch341ReadCmdMarshall(ch341outBuffer, offset, &eeprom_info, i2c_address); // Fill output buffer

    libusb_fill_bulk_transfer(xferBulkIn, handle, BULK_READ_ENDPOINT, ch341inBuffer, EEPROM_READ_BULKIN_BUF_SZ, cbBulkIn, NULL,
                              DEFAULT_TIMEOUT);

    libusb_fill_bulk_transfer(xferBulkOut, handle, BULK_WRITE_ENDPOINT, ch341outBuffer, EEPROM_READ_BULKOUT_BUF_SZ, cbBulkOut,
                              NULL, DEFAULT_TIMEOUT);

    libusb_submit_transfer(xferBulkIn);
    libusb_submit_transfer(xferBulkOut);

    readbuf = buffer;

    while (1) {
        ret = libusb_handle_events_timeout(NULL, &tv);

        if (ret < 0 || getnextpkt == -1) { // indicates an error
            printf("ret from libusb_handle_timeout = %d\n", ret);
            printf("getnextpkt = %u\n", getnextpkt);
            if (ret < 0)
                printf("USB read error : %s\n", strerror(-ret));
            libusb_free_transfer(xferBulkIn);
            libusb_free_transfer(xferBulkOut);
            return -1;
        }
        if (getnextpkt == 1) { // callback function reports a new BULK IN packet received
            getnextpkt = 0;    // reset the flag
            readpktcount++;    // increment the read packet counter
            byteoffset += EEPROM_READ_BULKIN_BUF_SZ;
            if (byteoffset == bytestoread)
                break;

            libusb_submit_transfer(xferBulkIn); // re-submit request for next BULK IN packet of EEPROM data
            if (syncackpkt)
                syncackpkt = 0;
            // if 4th packet received, we are at end of 0x80 byte data block,
            // if it is not the last block, then resubmit request for data
            if (readpktcount == 4) {
                readpktcount = 0;

                ch341ReadCmdMarshall(ch341outBuffer, byteoffset, &eeprom_info, i2c_address); // Fill output buffer
                libusb_fill_bulk_transfer(xferBulkOut, handle, BULK_WRITE_ENDPOINT, ch341outBuffer, EEPROM_READ_BULKOUT_BUF_SZ,
                                          cbBulkOut, NULL, DEFAULT_TIMEOUT);

                libusb_submit_transfer(xferBulkOut); // update transfer struct (with new EEPROM page offset)
                                                     // and re-submit next transfer request to BULK OUT endpoint
            }
        }
    }
    libusb_free_transfer(xferBulkIn);
    libusb_free_transfer(xferBulkOut);
    return 0;
}

// Callback function for async bulk in comms
void cbBulkIn(struct libusb_transfer *transfer)
{
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        // copy read data to our EEPROM buffer
        memcpy(readbuf + byteoffset, transfer->buffer, transfer->actual_length);
        getnextpkt = 1;
        break;
    default:
        printf("\ncbBulkIn: error : %d\n", transfer->status);
        getnextpkt = -1;
    }
    return;
}

// Callback function for async bulk out comms
void cbBulkOut(struct libusb_transfer *transfer)
{
    syncackpkt = 1;
    return;
}
