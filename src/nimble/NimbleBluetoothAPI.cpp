#include "NimbleBluetoothAPI.h"
#include "PhoneAPI.h"
#include "configuration.h"
#include "nimble/NimbleDefs.h"

// This scratch buffer is used for various bluetooth reads/writes - but it is safe because only one bt operation can be in
// proccess at once
static uint8_t trBytes[max(FromRadio_size, ToRadio_size)];
static uint32_t fromNum;

uint16_t fromNumValHandle;

/// We only allow one BLE connection at a time
int16_t curConnectionHandle = -1;

PhoneAPI *bluetoothPhoneAPI;

void BluetoothPhoneAPI::onNowHasData(uint32_t fromRadioNum)
{
    PhoneAPI::onNowHasData(fromRadioNum);

    fromNum = fromRadioNum;
    if (curConnectionHandle >= 0 && fromNumValHandle) {
        DEBUG_MSG("BLE notify fromNum\n");
        auto res = ble_gattc_notify(curConnectionHandle, fromNumValHandle);
        assert(res == 0);
    } else {
        DEBUG_MSG("No BLE notify\n");
    }
}

int toradio_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    auto om = ctxt->om;
    uint16_t len = 0;

    auto rc = ble_hs_mbuf_to_flat(om, trBytes, sizeof(trBytes), &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    /// DEBUG_MSG("toRadioWriteCb data %p, len %u\n", trBytes, len);

    bluetoothPhoneAPI->handleToRadio(trBytes, len);
    return 0;
}

int fromradio_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /// DEBUG_MSG("BLE fromRadio called\n");
    size_t numBytes = bluetoothPhoneAPI->getFromRadio(trBytes);

    // Someone is going to read our value as soon as this callback returns.  So fill it with the next message in the queue
    // or make empty if the queue is empty
    auto rc = os_mbuf_append(ctxt->om, trBytes, numBytes);
    assert(rc == 0);

    return 0; // success
}

int fromnum_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // DEBUG_MSG("BLE fromNum called\n");
    auto rc = os_mbuf_append(ctxt->om, &fromNum,
                             sizeof(fromNum)); // FIXME - once we report real numbers we will need to consider endianness
    assert(rc == 0);

    return 0; // success
}
