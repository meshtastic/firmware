#include "configuration.h"

#if defined(ARCH_RP2040) && HAS_BLUETOOTH
// BTstack keeps its bond database (TLV) in two "flash banks". The pico-sdk default puts them in the last 8 KB of
// flash, which on this board is inside the LittleFS partition and the EEPROM emulation: bonds and the filesystem
// would overwrite each other. Instead the banks live in RAM here, are loaded from two LittleFS files at first use,
// and are written back from the main task (never from the Bluetooth task, and never while the scheduler could
// pre-empt a flash erase).
#include "FSCommon.h"
#include "PicoWBluetooth.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"

#include <FreeRTOS.h>
#include <task.h>
extern "C" {
#include <ble/le_device_db_tlv.h>
#include <btstack_tlv.h>
#include <btstack_tlv_flash_bank.h>
#include <hal_flash_bank.h>
}

namespace
{
constexpr uint32_t BANK_SIZE = 4096;
constexpr int BANK_COUNT = 2;
uint8_t mirror[BANK_COUNT][BANK_SIZE];
bool dirty[BANK_COUNT];
bool loaded = false;
uint32_t lastDirtyMs = 0;

const char *bankPath(int b)
{
    return b == 0 ? "/prefs/btbank0.bin" : "/prefs/btbank1.bin";
}

bool schedulerRunning()
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}
void enterCritical()
{
    if (schedulerRunning())
        taskENTER_CRITICAL();
}
void exitCritical()
{
    if (schedulerRunning())
        taskEXIT_CRITICAL();
}

// The core configures BTstack's TLV from initVariant(), before the scheduler starts and before the filesystem is
// usable. Until the main task has loaded the saved banks the store simply looks empty; PicoWBluetooth::setup()
// then loads the files and re-points BTstack's TLV at the loaded banks (picoWBondStoreLoad).
void ensureLoaded()
{
    if (loaded)
        return;
    loaded = true;
    memset(mirror, 0xff, sizeof(mirror));
}

uint32_t bankGetSize(void *)
{
    return BANK_SIZE;
}
uint32_t bankGetAlignment(void *)
{
    return 1;
}
void bankErase(void *, int bank)
{
    ensureLoaded();
    enterCritical();
    memset(mirror[bank], 0xff, BANK_SIZE);
    dirty[bank] = true;
    lastDirtyMs = millis();
    exitCritical();
}
void bankRead(void *, int bank, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    ensureLoaded();
    if (offset + size > BANK_SIZE)
        return;
    memcpy(buffer, mirror[bank] + offset, size);
}
void bankWrite(void *, int bank, uint32_t offset, const uint8_t *data, uint32_t size)
{
    ensureLoaded();
    if (offset + size > BANK_SIZE)
        return;
    enterCritical();
    for (uint32_t i = 0; i < size; i++)
        mirror[bank][offset + i] &= data[i]; // flash semantics: writes only clear bits
    dirty[bank] = true;
    lastDirtyMs = millis();
    exitCritical();
}

const hal_flash_bank_t ramBank = {bankGetSize, bankGetAlignment, bankErase, bankRead, bankWrite};
btstack_tlv_flash_bank_t tlvContext;
bool filesLoaded = false;
} // namespace

// Main task, before the controller is powered on: load the saved banks from LittleFS. The Bluetooth stack is not
// running yet, so the mirror can be filled in place (no 4 KB temporaries: the lwIP task's stack is only 4 KB).
void picoWBondStoreLoad()
{
    if (filesLoaded)
        return;
    filesLoaded = true;
    ensureLoaded();
    concurrency::LockGuard g(spiLock);
    if (!FSCom.begin())
        return;
    for (int b = 0; b < BANK_COUNT; b++) {
        File f = FSCom.open(bankPath(b), FILE_O_READ);
        if (!f)
            continue;
        memset(mirror[b], 0xff, BANK_SIZE);
        size_t n = f.read(mirror[b], BANK_SIZE);
        f.close();
        if (n != BANK_SIZE)
            memset(mirror[b], 0xff, BANK_SIZE);
        dirty[b] = false; // what the pre-scheduler TLV init scribbled is superseded
    }
}

// Bluetooth task: re-scan the (now populated) banks, since the TLV instance the core created at boot saw them empty.
void picoWBondStoreAttachTlv()
{
    const btstack_tlv_t *tlv = btstack_tlv_flash_bank_init_instance(&tlvContext, &ramBank, nullptr);
    btstack_tlv_set_instance(tlv, &tlvContext);
    le_device_db_tlv_configure(tlv, &tlvContext);
}

extern "C" const hal_flash_bank_t *__wrap_pico_flash_bank_instance(void)
{
    return &ramBank;
}

// Main task: write dirty banks to LittleFS, debounced so a pairing (several TLV writes) becomes one file write.
void picoWBondStoreFlush()
{
    if (!filesLoaded || millis() - lastDirtyMs < 1000)
        return;
    static uint32_t lastFailMs = 0;
    if (lastFailMs && millis() - lastFailMs < 10000)
        return; // back off after a failed write instead of hammering the filesystem
    for (int b = 0; b < BANK_COUNT; b++) {
        if (!dirty[b])
            continue;
        static uint8_t snapshot[BANK_SIZE];
        enterCritical();
        memcpy(snapshot, mirror[b], BANK_SIZE);
        dirty[b] = false;
        exitCritical();
        concurrency::LockGuard g(spiLock);
        bool saved = false;
        File f = FSCom.open(bankPath(b), FILE_O_WRITE);
        if (f) {
            saved = f.write(snapshot, BANK_SIZE) == BANK_SIZE;
            f.close();
        }
        if (saved) {
            LOG_INFO("BLE bond store saved (%s)", bankPath(b));
        } else {
            LOG_ERROR("BLE bond store: can't write %s, will retry", bankPath(b));
            lastFailMs = millis() ? millis() : 1;
            enterCritical();
            dirty[b] = true; // retry on a later flush (a newer write may already have re-dirtied it)
            exitCritical();
        }
    }
}
#endif
