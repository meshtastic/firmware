// libUSB driver for the ch341a in i2c mode
//
// Copyright 2011 asbokid <ballymunboy@gmail.com>
#ifndef __CH341A_I2C_H__
#define __CH341A_I2C_H__

#include <assert.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BULK_WRITE_ENDPOINT 0x02 /* bEndpointAddress 0x02  EP 2 OUT (Bulk)*/
#define BULK_READ_ENDPOINT 0x82  /* bEndpointAddress 0x82  EP 2 IN  (Bulk)*/

#define DEFAULT_TIMEOUT 300 // 300mS for USB timeouts

#define IN_BUF_SZ 0x100
#define EEPROM_READ_BULKIN_BUF_SZ 0x20
#define EEPROM_READ_BULKOUT_BUF_SZ 0x65

#define CH341_CMD_I2C_STREAM 0xAA
#define CH341_CMD_I2C_STM_STA 0x74
#define CH341_CMD_I2C_STM_STO 0x75
#define CH341_CMD_I2C_STM_OUT 0x80
#define CH341_CMD_I2C_STM_IN 0xC0
#define CH341_CMD_I2C_STM_END 0x00

#define CH341_EEPROM_READ_CMD_SZ 0x65 /* Same size for all 24cXX read setup and next packets*/

struct EEPROM {
    char *name;
    uint32_t size;
    uint16_t page_size;
    uint8_t addr_size; // Length of address in bytes
    uint8_t i2c_addr_mask;
};

int32_t ch341readEEPROM_param(uint8_t *buffer, uint32_t offset, uint32_t bytestoread, uint32_t ic_size, uint32_t block_size,
                              uint8_t algorithm, uint32_t i2c_address, struct libusb_device_handle *handle);
#endif /* __CH341A_I2C_H__ */
