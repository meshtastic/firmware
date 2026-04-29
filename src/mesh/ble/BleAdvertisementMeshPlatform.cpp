#include "configuration.h"
#include "mesh/ble/BleAdvertisementMeshPlatform.h"

#if HAS_BLE_MESH_ADVERTISING

#include "DebugConfiguration.h"
#include "mesh/ble/BleAdvertisementMesh.h"

#if defined(ARCH_ESP32) && defined(NIMBLE_TWO) && defined(CONFIG_BT_NIMBLE_EXT_ADV) && CONFIG_BT_NIMBLE_EXT_ADV &&                 \
    defined(CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES) && CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES > 1
#define BLE_ADV_MESH_ESP32_NIMBLE_BACKEND 1
#else
#define BLE_ADV_MESH_ESP32_NIMBLE_BACKEND 0
#endif

#if BLE_ADV_MESH_ESP32_NIMBLE_BACKEND
#include <NimBLEDevice.h>
#include <NimBLEExtAdvertising.h>
#if defined(CONFIG_BT_NIMBLE_ROLE_OBSERVER) && CONFIG_BT_NIMBLE_ROLE_OBSERVER
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>
#endif
#endif
#include <string.h>
#include <string>

namespace
{
constexpr uint16_t BLE_ADV_MESH_COMPANY_ID = BLE_MESH_ADVERTISEMENT_COMPANY_ID;
constexpr uint8_t BLE_ADV_MESH_INSTANCE = 1;
constexpr uint32_t BLE_ADV_MESH_BURST_MS = 120;

void putLe16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

#if BLE_ADV_MESH_ESP32_NIMBLE_BACKEND

#if defined(CONFIG_BT_NIMBLE_ROLE_OBSERVER) && CONFIG_BT_NIMBLE_ROLE_OBSERVER
class BleAdvMeshScanCallbacks : public NimBLEScanCallbacks
{
  public:
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        handleAdvertisement(advertisedDevice);
    }

    void onDiscovered(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        handleAdvertisement(advertisedDevice);
    }

  private:
    void handleAdvertisement(const NimBLEAdvertisedDevice *advertisedDevice)
    {
        if (!advertisedDevice || !advertisedDevice->haveManufacturerData() || !bleAdvertisementMesh) {
            return;
        }

        uint8_t dataCount = advertisedDevice->getManufacturerDataCount();
        for (uint8_t i = 0; i < dataCount; i++) {
            std::string manufacturerData = advertisedDevice->getManufacturerData(i);
            if (manufacturerData.size() < 2 + BleAdvertisementMeshCodec::FRAME_HEADER_SIZE) {
                continue;
            }

            const auto *data = reinterpret_cast<const uint8_t *>(manufacturerData.data());
            uint16_t companyId = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
            if (companyId != BLE_ADV_MESH_COMPANY_ID) {
                continue;
            }

            bleAdvertisementMesh->onAdvertisement(data + 2, manufacturerData.size() - 2);
        }
    }
};

BleAdvMeshScanCallbacks scanCallbacks;
#endif

bool advertiseBleAdvMeshFrame(const uint8_t *frame, size_t frameLength)
{
    if (!frame || frameLength == 0 || frameLength > BleAdvertisementMeshCodec::MAX_FRAME_SIZE) {
        return false;
    }

    uint8_t manufacturerData[2 + BleAdvertisementMeshCodec::MAX_FRAME_SIZE] = {0};
    putLe16(manufacturerData, BLE_ADV_MESH_COMPANY_ID);
    memcpy(&manufacturerData[2], frame, frameLength);

    NimBLEExtAdvertisement advertisement;
    advertisement.setLegacyAdvertising(true);
    advertisement.setConnectable(false);
    advertisement.setScannable(false);
    advertisement.setMinInterval(160);
    advertisement.setMaxInterval(240);
    if (!advertisement.setManufacturerData(manufacturerData, frameLength + 2)) {
        return false;
    }

    NimBLEExtAdvertising *advertising = NimBLEDevice::getAdvertising();
    if (!advertising) {
        return false;
    }

    advertising->stop(BLE_ADV_MESH_INSTANCE);
    advertising->removeInstance(BLE_ADV_MESH_INSTANCE);
    return advertising->setInstanceData(BLE_ADV_MESH_INSTANCE, advertisement) &&
           advertising->start(BLE_ADV_MESH_INSTANCE, BLE_ADV_MESH_BURST_MS, 0);
}

void startBleAdvMeshScan()
{
#if defined(CONFIG_BT_NIMBLE_ROLE_OBSERVER) && CONFIG_BT_NIMBLE_ROLE_OBSERVER
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) {
        LOG_WARN("BLE advertisement mesh scan unavailable");
        return;
    }

    scan->setScanCallbacks(&scanCallbacks, true);
    scan->setActiveScan(false);
    scan->setInterval(500);
    scan->setWindow(80);
    scan->setDuplicateFilter(0);
    scan->setMaxResults(0);
    if (!scan->start(0, false, true)) {
        LOG_WARN("BLE advertisement mesh scan start failed");
    }
#else
    LOG_WARN("BLE advertisement mesh receive disabled: NimBLE observer role is not enabled");
#endif
}

#else

bool advertiseBleAdvMeshFrame(const uint8_t *, size_t)
{
    LOG_WARN("BLE advertisement mesh transmit disabled on this platform");
    return false;
}

void startBleAdvMeshScan()
{
    LOG_WARN("BLE advertisement mesh scan disabled on this platform");
}

#endif
} // namespace

void initBleAdvertisementMeshPlatform()
{
    if (bleAdvertisementMesh) {
        return;
    }

    LOG_INFO("Init BLE advertisement mesh bearer");
    bleAdvertisementMesh = new BleAdvertisementMesh(advertiseBleAdvMeshFrame);
    startBleAdvMeshScan();
}

#endif // HAS_BLE_MESH_ADVERTISING
