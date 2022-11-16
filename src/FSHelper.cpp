#include "configuration.h"
#include "NodeDB.h"

#ifdef ARCH_ESP32

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

#include "USBMSC.h"
#include "USB.h"
#include "FFat.h"
#include "ff.h"
#include "diskio.h"
#include "esp_vfs_fat.h"

static const char   *MESHTASTIC_PROJECT_NAME = "Meshtastic";

static USBMSC   usb_msc;
static uint8_t  pdrv = 0; //The default drive number of ESP32 Flash is 0
static uint32_t block_count = 0;
static uint16_t block_size = 0;

static int32_t tinyusb_msc_read_cb(uint32_t lba,
                                   uint32_t offset,
                                   void *buffer,
                                   uint32_t bufsize)
{
    const uint32_t block_count = bufsize / block_size;
    disk_read(pdrv, (BYTE *)buffer, lba, block_count);
    return block_count * block_size;
}

static int32_t tinyusb_msc_write_cb(uint32_t lba,
                                    uint32_t offset,
                                    uint8_t *buffer,
                                    uint32_t bufsize)
{
    const uint32_t block_count = bufsize / block_size;
    disk_write(pdrv, (BYTE *)buffer, lba, block_count);
    return block_count * block_size;
}


// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tinyusb_msc_start_stop_cb(uint8_t power_condition, bool start, bool load_eject)
{
    (void) power_condition;
    if (load_eject) {
        if (!start) {
            // Eject but first flush.
            if (disk_ioctl(pdrv, CTRL_SYNC, NULL) != RES_OK) {
                return false;
            }
        }
    } else {
        if (!start) {
            // Stop the unit but don't eject.
            if (disk_ioctl(pdrv, CTRL_SYNC, NULL) != RES_OK) {
                return false;
            }
        }
    }
    return true;
}


void setupMCS()
{
    // Initialize FFat. After fsInit is initialized, check whether the returned result is correct.
    // If it is correct, go to the next step
    if (!FFat.begin(true)) {
        return ;
    }

    // Get the number of sectors and block size
    disk_ioctl(pdrv, GET_SECTOR_COUNT, &block_count);
    disk_ioctl(pdrv, GET_SECTOR_SIZE, &block_size);

    // Set callback
    usb_msc.onRead(tinyusb_msc_read_cb);
    usb_msc.onWrite(tinyusb_msc_write_cb);
    usb_msc.onStartStop(tinyusb_msc_start_stop_cb);

    // MSC is ready for read/write
    usb_msc.mediaPresent(true);
    // Set disk size, block size
    usb_msc.begin(block_count, block_size);

    USB.VID(USB_VID);
    // todo:Added device pid -> https://github.com/espressif/usb-pids/blob/main/allocated-pids.txt
    // USB.PID(xxx);
    USB.productName(MESHTASTIC_PROJECT_NAME);
    USB.firmwareVersion(DEVICESTATE_CUR_VER); 
    USB.serialNumber("__MAC__");        //Set MAC address as serial number

    if (USB.begin()) {
        DEBUG_MSG("USB mass storage device init suceed.\n");
    } else {
        DEBUG_MSG("USB mass storage device failed.\n");
    }
}


#else
void setupMCS() {}
#endif

#else
void setupMCS() {}

#endif /*ARCH_ESP32*/





