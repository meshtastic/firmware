#include "configuration.h"

#if defined(ARCH_RP2040) && HAS_BLUETOOTH

#include "BluetoothCommon.h"
#include "BluetoothStatus.h"
#include "NodeDB.h"
#include "PicoWBluetooth.h"
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "mesh/PhoneAPI.h"

#include <Arduino.h>
#include <atomic>
#include <lwip_wrap.h> // lwip_callback(): run a function on the lwIP task, where BTstack lives

#include <ble/att_db.h>
#include <ble/att_db_util.h>
#include <ble/att_server.h>
#include <ble/le_device_db.h>
#include <ble/sm.h>
#include <bluetooth.h>
#include <bluetooth_data_types.h>
#include <btstack_event.h>
#include <btstack_util.h>
#include <gap.h>
#include <hci.h>
#include <l2cap.h>

extern "C" bool __isLWIPThread();

PicoWBluetooth *picoWBluetooth = nullptr;

namespace
{

// Run fn on the lwIP task and wait for it (direct call when already there).
void onLwip(void (*fn)(void *), void *arg)
{
    if (__isLWIPThread())
        fn(arg);
    else
        lwip_callback(fn, arg, nullptr);
}

// ---- GATT handles ----
uint16_t toRadioHandle = 0, fromRadioHandle = 0, fromNumHandle = 0, logRadioHandle = 0, batteryHandle = 0;

// ---- connection state shared between the BT task and the main task ----
std::atomic<uint16_t> conHandle{HCI_CON_HANDLE_INVALID};
std::atomic<bool> connected{false};
std::atomic<bool> evtConnected{false}, evtDisconnected{false}, evtPasskey{false}, evtPairingDone{false}, evtStackWorking{false},
    btRestarting{false};
std::atomic<uint32_t> passkeyToShow{0};
std::atomic<uint8_t> pairingStatus{0};
std::atomic<uint8_t> disconnectReason{0};
std::atomic<uint8_t> reencryptStatus{0};
std::atomic<bool> evtReencrypt{false};
std::atomic<uint16_t> peerMtu{23};
std::atomic<uint32_t> lastClientActivityMs{0}; // last ATT read/write from the phone
std::atomic<bool> dropRequested{false};
std::atomic<uint32_t> dropIssuedMs{0};
std::atomic<bool> advAck{false}; // controller acknowledged an advertise enable/disable command

// ---- to phone: one pre-fetched FromRadio packet (main task writes, BT task reads) ----
uint8_t toPhoneBuf[meshtastic_FromRadio_size];
std::atomic<uint16_t> toPhoneLen{0};
std::atomic<uint32_t> toPhoneSetMs{0};
std::atomic<bool> emptyReadSeen{false};

// ---- from phone: prepared (long) write staging, BT task only ----
uint8_t prepBuf[meshtastic_ToRadio_size];
uint16_t prepLen = 0;
bool prepValid = false;

// ---- from phone: ring of ToRadio packets (BT task writes, main task reads) ----
constexpr int FROM_PHONE_SLOTS = 8;
struct ToRadioSlot {
    uint16_t len;
    uint8_t data[meshtastic_ToRadio_size];
};
ToRadioSlot fromPhone[FROM_PHONE_SLOTS];
std::atomic<uint8_t> fromPhoneHead{0}, fromPhoneTail{0};

// ---- fromNum notification ----
std::atomic<uint32_t> lastFromNum{0};
std::atomic<bool> notifyPending{false};
std::atomic<uint32_t> notifyRequestedMs{0};
uint8_t batteryLevel = 0;

btstack_packet_callback_registration_t hciReg, smReg;
uint8_t advData[31], scanRspData[31];
uint8_t advDataLen = 0, scanRspLen = 0;
uint8_t meshSvcUuid[16], toRadioUuid[16], fromRadioUuid[16], fromNumUuid[16], logRadioUuid[16];
char deviceNameCopy[32];
bool initialized = false;

uint8_t hexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}
// "6ba1b218-15a8-461f-9fa8-5dcae273eafd" -> 16 big-endian bytes (what att_db_util wants)
void parseUuid(const char *s, uint8_t out[16])
{
    int n = 0;
    while (*s && n < 16) {
        if (*s == '-') {
            s++;
            continue;
        }
        out[n++] = (hexNibble(s[0]) << 4) | hexNibble(s[1]);
        s += 2;
    }
}

bool pairingRequiresPin()
{
    return config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
}

// ---------------- BT task callbacks ----------------
uint16_t attRead(hci_con_handle_t con, uint16_t handle, uint16_t offset, uint8_t *buffer, uint16_t bufferSize)
{
    (void)con;
    lastClientActivityMs = millis();
    if (handle == fromRadioHandle) {
        uint16_t len = toPhoneLen.load();
        if (!buffer)
            return len; // size query
        if (len == 0) {
            emptyReadSeen = true;
            return 0;
        }
        if (offset >= len)
            return 0;
        uint16_t n = len - offset;
        if (n > bufferSize)
            n = bufferSize;
        memcpy(buffer, toPhoneBuf + offset, n);
        if (offset + n >= len)
            toPhoneLen = 0; // fully delivered, main task may pre-fetch the next one
        return n;
    }
    if (handle == fromNumHandle) {
        uint8_t v[4];
        little_endian_store_32(v, 0, lastFromNum.load());
        if (!buffer)
            return 4;
        uint16_t n = (offset < 4) ? 4 - offset : 0;
        if (n > bufferSize)
            n = bufferSize;
        memcpy(buffer, v + offset, n);
        return n;
    }
    if (handle == batteryHandle) {
        if (!buffer)
            return 1;
        if (offset > 0 || bufferSize < 1)
            return 0;
        buffer[0] = batteryLevel;
        return 1;
    }
    return 0; // logRadio and everything else: nothing to read
}

int attWrite(hci_con_handle_t con, uint16_t handle, uint16_t mode, uint16_t offset, uint8_t *buffer, uint16_t size)
{
    (void)con;
    (void)offset;
    lastClientActivityMs = millis();
    // Prepared (long) writes: a ToRadio packet larger than MTU-3 arrives as ACTIVE chunks at increasing offsets,
    // followed by EXECUTE (commit) or CANCEL; VALIDATE and EXECUTE/CANCEL come with handle 0.
    switch (mode) {
    case ATT_TRANSACTION_MODE_ACTIVE:
        if (handle != toRadioHandle)
            return 0;
        if (offset != prepLen) // chunks must be contiguous from offset 0: no gaps, no stale bytes
            return ATT_ERROR_INVALID_OFFSET;
        if (offset + size > sizeof(prepBuf))
            return ATT_ERROR_INVALID_OFFSET;
        memcpy(prepBuf + offset, buffer, size);
        prepLen = offset + size;
        prepValid = true;
        return 0;
    case ATT_TRANSACTION_MODE_VALIDATE:
        // BTstack ignores the EXECUTE result, so a full queue has to be reported here for the phone to see it
        if (prepValid && (uint8_t)(fromPhoneHead.load() - fromPhoneTail.load()) >= FROM_PHONE_SLOTS) {
            prepLen = 0;
            prepValid = false;
            return ATT_ERROR_INSUFFICIENT_RESOURCES;
        }
        return 0;
    case ATT_TRANSACTION_MODE_EXECUTE:
        if (!prepValid)
            return 0;
        buffer = prepBuf;
        size = prepLen;
        prepLen = 0;
        prepValid = false;
        break; // enqueue the assembled packet below
    case ATT_TRANSACTION_MODE_CANCEL:
        prepLen = 0;
        prepValid = false;
        return 0;
    default:
        if (handle != toRadioHandle)
            return 0; // CCC descriptors etc.: accepted, we notify whenever connected
        break;
    }
    uint8_t head = fromPhoneHead.load(), tail = fromPhoneTail.load();
    if ((uint8_t)(head - tail) >= FROM_PHONE_SLOTS)
        return ATT_ERROR_INSUFFICIENT_RESOURCES; // main task hasn't drained the queue yet
    ToRadioSlot &s = fromPhone[head % FROM_PHONE_SLOTS];
    if (size > sizeof(s.data))
        size = sizeof(s.data);
    memcpy(s.data, buffer, size);
    s.len = size;
    fromPhoneHead = head + 1;
    return 0;
}

void applyAdvertisingSettings()
{
    bd_addr_t nullAddr;
    memset(nullAddr, 0, sizeof(nullAddr));
    gap_advertisements_set_params(0x0030, 0x0030, 0, 0, nullAddr, 0x07, 0x00);
    gap_advertisements_set_data(advDataLen, advData);
    gap_scan_response_set_data(scanRspLen, scanRspData);
    gap_advertisements_enable(1);
}

void onLeConnected(uint16_t handle)
{
    if (handle == HCI_CON_HANDLE_INVALID)
        return; // a failed connection attempt (the disconnect event with its reason follows)
    conHandle = handle;
    connected = true;
    evtConnected = true;
    lastClientActivityMs = millis();
}

void packetHandler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (type != HCI_EVENT_PACKET)
        return;
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_LE_META:
        // status != 0 means the connection attempt failed; the handle in the event is then meaningless
        if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
            if (hci_subevent_le_connection_complete_get_status(packet) == ERROR_CODE_SUCCESS)
                onLeConnected(hci_subevent_le_connection_complete_get_connection_handle(packet));
        } else if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V1) {
            if (hci_subevent_le_enhanced_connection_complete_v1_get_status(packet) == ERROR_CODE_SUCCESS)
                onLeConnected(hci_subevent_le_enhanced_connection_complete_v1_get_connection_handle(packet));
        }
        break;
    case HCI_EVENT_META_GAP:
        // BTstack re-emits LE connections as a GAP event and stops forwarding the raw HCI one once address
        // resolution is active (i.e. as soon as a bond with an IRK exists).
        if (hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE &&
            gap_subevent_le_connection_complete_get_status(packet) == ERROR_CODE_SUCCESS)
            onLeConnected(gap_subevent_le_connection_complete_get_connection_handle(packet));
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (hci_event_disconnection_complete_get_connection_handle(packet) == conHandle.load()) {
            disconnectReason = hci_event_disconnection_complete_get_reason(packet);
            connected = false;
            conHandle = HCI_CON_HANDLE_INVALID;
            evtDisconnected = true;
        }
        break;
    case SM_EVENT_REENCRYPTION_COMPLETE:
        reencryptStatus = sm_event_reencryption_complete_get_status(packet);
        evtReencrypt = true;
        break;
    case HCI_EVENT_COMMAND_COMPLETE:
        if (hci_event_command_complete_get_command_opcode(packet) == HCI_OPCODE_HCI_LE_SET_ADVERTISE_ENABLE)
            advAck = true;
        break;
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            evtStackWorking = true;
            if (btRestarting.exchange(false))
                applyAdvertisingSettings();
        }
        break;
    case SM_EVENT_JUST_WORKS_REQUEST:
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;
    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        sm_numeric_comparison_confirm(sm_event_numeric_comparison_request_get_handle(packet));
        break;
    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
        passkeyToShow = sm_event_passkey_display_number_get_passkey(packet);
        evtPasskey = true;
        break;
    case SM_EVENT_PAIRING_COMPLETE:
        pairingStatus = sm_event_pairing_complete_get_status(packet);
        evtPairingDone = true;
        if (pairingStatus != ERROR_CODE_SUCCESS)
            dropRequested = true; // a link whose pairing failed/was denied stays up on iOS and blocks advertising
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        peerMtu = att_event_mtu_exchange_complete_get_MTU(packet);
        break;
    case ATT_EVENT_CAN_SEND_NOW:
        if (notifyPending.exchange(false) && connected) {
            uint8_t v[4];
            little_endian_store_32(v, 0, lastFromNum.load());
            att_server_notify(conHandle.load(), fromNumHandle, v, sizeof(v));
        }
        break;
    default:
        break;
    }
}

void requestNotify(uint32_t fromNum)
{
    lastFromNum = fromNum;
    if (!connected)
        return;
    // Coalesce: one outstanding can-send-now request at a time (each one is a post to the core's small lwIP work
    // queue, and the config stream would otherwise issue dozens in a burst).
    if (notifyPending.exchange(true))
        return;
    notifyRequestedMs = millis();
    onLwip([](void *) { att_server_request_can_send_now_event(conHandle.load()); }, nullptr);
}

// If a can-send-now request got lost, the coalescing flag would block every later notification: re-post.
void retryStaleNotify()
{
    if (!connected || !notifyPending.load())
        return;
    if (millis() - notifyRequestedMs.load() < 300)
        return;
    notifyRequestedMs = millis();
    onLwip([](void *) { att_server_request_can_send_now_event(conHandle.load()); }, nullptr);
}

void buildGattDb()
{
    parseUuid(MESH_SERVICE_UUID, meshSvcUuid);
    parseUuid(TORADIO_UUID, toRadioUuid);
    parseUuid(FROMRADIO_UUID, fromRadioUuid);
    parseUuid(FROMNUM_UUID, fromNumUuid);
    parseUuid(LOGRADIO_UUID, logRadioUuid);
    strncpy(deviceNameCopy, getDeviceName(), sizeof(deviceNameCopy) - 1);
    deviceNameCopy[sizeof(deviceNameCopy) - 1] = 0;

    // With a PIN the characteristics need an authenticated (MITM) link, which forces the phone to pair.
    uint8_t sec = pairingRequiresPin() ? ATT_SECURITY_AUTHENTICATED : ATT_SECURITY_NONE;

    att_db_util_init();
    att_db_util_add_service_uuid16(0x1800);
    att_db_util_add_characteristic_uuid16(0x2A00, ATT_PROPERTY_READ, ATT_SECURITY_NONE, ATT_SECURITY_NONE,
                                          (uint8_t *)deviceNameCopy, strlen(deviceNameCopy));
    uint8_t appearance[2] = {0, 0};
    att_db_util_add_characteristic_uuid16(0x2A01, ATT_PROPERTY_READ, ATT_SECURITY_NONE, ATT_SECURITY_NONE, appearance,
                                          sizeof(appearance));

    att_db_util_add_service_uuid128(meshSvcUuid);
    toRadioHandle =
        att_db_util_add_characteristic_uuid128(toRadioUuid, ATT_PROPERTY_WRITE | ATT_PROPERTY_DYNAMIC, sec, sec, NULL, 0);
    fromRadioHandle =
        att_db_util_add_characteristic_uuid128(fromRadioUuid, ATT_PROPERTY_READ | ATT_PROPERTY_DYNAMIC, sec, sec, NULL, 0);
    fromNumHandle = att_db_util_add_characteristic_uuid128(
        fromNumUuid, ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY | ATT_PROPERTY_DYNAMIC, sec, sec, NULL, 0);
    logRadioHandle = att_db_util_add_characteristic_uuid128(
        logRadioUuid, ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY | ATT_PROPERTY_DYNAMIC, sec, sec, NULL, 0);

    att_db_util_add_service_uuid16(0x180F);
    batteryHandle = att_db_util_add_characteristic_uuid16(0x2A19, ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY | ATT_PROPERTY_DYNAMIC,
                                                          ATT_SECURITY_NONE, ATT_SECURITY_NONE, NULL, 0);
}

void buildAdvData()
{
    // Flags + complete list of 128-bit service UUIDs (little-endian in advertising data)
    uint8_t *p = advData;
    *p++ = 2;
    *p++ = BLUETOOTH_DATA_TYPE_FLAGS;
    *p++ = 0x06;
    *p++ = 17;
    *p++ = BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS;
    for (int i = 15; i >= 0; i--)
        *p++ = meshSvcUuid[i];
    advDataLen = p - advData;

    // Scan response: complete local name
    size_t nameLen = strlen(deviceNameCopy);
    if (nameLen > 29)
        nameLen = 29;
    p = scanRspData;
    *p++ = nameLen + 1;
    *p++ = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(p, deviceNameCopy, nameLen);
    p += nameLen;
    scanRspLen = p - scanRspData;
}

// ---------------- main-task side ----------------
class BluetoothPhoneAPI : public PhoneAPI, public concurrency::OSThread
{
  public:
    BluetoothPhoneAPI() : OSThread("PicoWBLE")
    {
        api_type = TYPE_BLE;
        setInterval(250);
    }

  protected:
    int32_t runOnce() override
    {
        picoWBondStoreFlush();

        if (evtConnected.exchange(false)) {
            LOG_INFO("BLE client connected (handle %u), heap free %u", (unsigned)conHandle.load(),
                     (unsigned)rp2040.getFreeHeap());
            // A bonded phone writes wantConfig within milliseconds of connecting, before this thread sees the
            // event: keep the ring buffer, drop only the stale to-phone packet and the notify flag.
            toPhoneLen = 0;
            notifyPending = false;
            dropIssuedMs = 0;
            sessionStartMs = millis();
            meshtastic::BluetoothStatus st(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
            bluetoothStatus->updateStatus(&st);
        }
        if (evtStackWorking.exchange(false))
            LOG_INFO("BLE: controller up");
        if (advAck.exchange(false))
            advAckSinceDisconnect = true;
        if (evtPasskey.exchange(false)) {
            uint32_t pk = passkeyToShow.load();
            LOG_INFO("*** Enter passkey %06u on the phone ***", (unsigned)pk);
            powerFSM.trigger(EVENT_BLUETOOTH_PAIR);
            char buf[8];
            snprintf(buf, sizeof(buf), "%06u", (unsigned)pk);
            std::string pkStr(buf);
            meshtastic::BluetoothStatus st(pkStr);
            bluetoothStatus->updateStatus(&st);
        }
        if (evtPairingDone.exchange(false)) {
            uint8_t s = pairingStatus.load();
            if (s == ERROR_CODE_SUCCESS)
                LOG_INFO("BLE pairing complete, bonded devices now: %d", picoWBluetooth ? picoWBluetooth->countBonds() : -1);
            else
                LOG_WARN("BLE pairing failed, status 0x%02x", s);
        }
        if (evtReencrypt.exchange(false)) {
            uint8_t s = reencryptStatus.load();
            if (s == ERROR_CODE_SUCCESS)
                LOG_INFO("BLE link re-encrypted with the stored bond");
            else
                LOG_WARN("BLE re-encryption with the stored bond failed, status 0x%02x (phone forgot us, or we forgot it)", s);
        }
        if (evtDisconnected.exchange(false)) {
            LOG_INFO("BLE client disconnected (reason 0x%02x)", disconnectReason.load());
            close(); // PhoneAPI: back to STATE_SEND_NOTHING
            toPhoneLen = 0;
            emptyReadSeen = false;
            fromPhoneTail.store(fromPhoneHead.load());
            notifyPending = false;
            dropIssuedMs = 0;
            disconnectedAtMs = millis() | 1;
            advAckSinceDisconnect = false;
            meshtastic::BluetoothStatus st(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
            bluetoothStatus->updateStatus(&st);
            // advertising stops on connect; resume it unless the radio is meant to be off
            if (picoWBluetooth && config.bluetooth.enabled && !config.network.wifi_enabled)
                picoWBluetooth->startAdvertising();
        }
        // BTstack occasionally never re-sends the advertise-enable after a disconnect (no controller ack arrives);
        // the phone can then not find the radio until a reboot. Power-cycle the controller through BTstack
        // instead and re-apply the advertising settings once it reports WORKING again.
        if (!connected && disconnectedAtMs != 0 && !advAckSinceDisconnect && config.bluetooth.enabled &&
            !config.network.wifi_enabled && millis() - disconnectedAtMs > 2500) {
            disconnectedAtMs = 0;
            LOG_ERROR("BLE: advertising did not resume after the disconnect, restarting the BT controller");
            btRestarting = true;
            onLwip(
                [](void *) {
                    hci_power_control(HCI_POWER_OFF);
                    hci_power_control(HCI_POWER_ON);
                },
                nullptr);
        }
        if (advAckSinceDisconnect)
            disconnectedAtMs = 0;

        if (connected) {
            retryStaleNotify();
            // The phone reads until it sees an empty packet, then waits for a fromNum notification. The prefetch
            // is asynchronous, so a read can land in the gap and leave the phone waiting on a notification for a
            // packet we already hold. Nudge it again if a staged packet sits unread.
            if (toPhoneLen.load() != 0 && millis() - toPhoneSetMs.load() > 700 && millis() - lastClientActivityMs > 700) {
                toPhoneSetMs = millis();
                requestNotify(lastFromNum.load() + 1);
            }
            // A phone that has a packet waiting but hasn't read for a minute is gone; a quiet link with nothing
            // pending is fine and is left to the BLE supervision timeout.
            bool ignoringUs = toPhoneLen.load() != 0 && millis() - lastClientActivityMs > 60000;
            if (dropIssuedMs.load() == 0 && (dropRequested.exchange(false) || ignoringUs)) {
                LOG_WARN("BLE: dropping %s client so we can advertise again", ignoringUs ? "unresponsive" : "unpaired");
                dropIssuedMs = millis() | 1;
                uint16_t h = conHandle.load();
                onLwip([](void *p) { gap_disconnect(*static_cast<uint16_t *>(p)); }, &h);
            } else if (dropIssuedMs.load() != 0 && millis() - dropIssuedMs.load() > 3000) {
                LOG_ERROR("BLE: controller never reported the disconnect, resetting link state locally");
                dropIssuedMs = 0;
                connected = false;
                conHandle = HCI_CON_HANDLE_INVALID;
                evtDisconnected = true;
            }
        }
        if (!connected) {
            // Belt and braces: re-assert advertising every 30 s while idle, in case the controller dropped it.
            if (picoWBluetooth && config.bluetooth.enabled && !config.network.wifi_enabled && millis() - lastAdvKick > 30000) {
                lastAdvKick = millis();
                picoWBluetooth->startAdvertising();
            }
            setInterval(250);
            return 250;
        }

        // Packets from the phone
        while (fromPhoneTail.load() != fromPhoneHead.load()) {
            ToRadioSlot &s = fromPhone[fromPhoneTail.load() % FROM_PHONE_SLOTS];
            handleToRadio(s.data, s.len);
            fromPhoneTail = fromPhoneTail.load() + 1;
        }

        // Pre-fetch the next packet for the phone
        if (toPhoneLen.load() == 0 && available()) {
            uint8_t tmp[meshtastic_FromRadio_size];
            size_t n = getFromRadio(tmp);
            if (n > 0) {
                memcpy(toPhoneBuf, tmp, n);
                toPhoneSetMs = millis();
                toPhoneLen = (uint16_t)n;
                if (emptyReadSeen.exchange(false))
                    requestNotify(lastFromNum.load()); // the phone gave up on an empty read: wake it
            }
        }

        setInterval(10);
        return 10;
    }

    void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);
        requestNotify(fromRadioNum);
    }

    bool checkIsConnected() override { return connected.load(); }

  private:
    uint32_t sessionStartMs = 0, disconnectedAtMs = 0, lastAdvKick = 0;
    bool advAckSinceDisconnect = false;
};

BluetoothPhoneAPI *bluetoothPhoneAPI = nullptr;

} // namespace

// ---------------- public API (main task) ----------------
void PicoWBluetooth::setup()
{
    if (initialized) {
        startAdvertising();
        return;
    }
    LOG_INFO("Init Pico W BLE (BTstack), name %s, pairing mode %d", getDeviceName(), (int)config.bluetooth.mode);
    buildGattDb();
    buildAdvData();
    if (!bluetoothPhoneAPI)
        bluetoothPhoneAPI = new BluetoothPhoneAPI();
    picoWBondStoreLoad();

    onLwip(
        [](void *) {
            picoWBondStoreAttachTlv();
            l2cap_init();
            sm_init();
            // Legacy pairing only (no LE Secure Connections in this BTstack build): passkey display gives MITM
            // protection + bonding; NO_PIN means no pairing at all.
            if (pairingRequiresPin()) {
                sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
                sm_set_authentication_requirements(SM_AUTHREQ_MITM_PROTECTION | SM_AUTHREQ_BONDING);
                if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
                    sm_use_fixed_passkey_in_display_role(config.bluetooth.fixed_pin);
            } else {
                sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
                sm_set_authentication_requirements(0);
            }
            att_server_init(att_db_util_get_address(), attRead, attWrite);
            att_server_register_packet_handler(packetHandler);
            hciReg.callback = packetHandler;
            hci_add_event_handler(&hciReg);
            smReg.callback = packetHandler;
            sm_add_event_handler(&smReg);
            gap_set_local_name(deviceNameCopy);
            applyAdvertisingSettings();
            hci_power_control(HCI_POWER_ON);
        },
        nullptr);
    initialized = true;
    LOG_INFO("BLE controller starting, bonded devices in flash: %d", countBonds());
}

void PicoWBluetooth::startAdvertising()
{
    if (!initialized)
        return;
    LOG_INFO("BLE advertising as %s", deviceNameCopy);
    onLwip(
        [](void *) {
            gap_advertisements_enable(0);
            gap_advertisements_enable(1);
        },
        nullptr);
}

void PicoWBluetooth::shutdown()
{
    if (!initialized)
        return;
    LOG_INFO("BLE advertising off");
    onLwip(
        [](void *) {
            gap_advertisements_enable(0);
            if (connected)
                gap_disconnect(conHandle.load());
        },
        nullptr);
}

bool PicoWBluetooth::isConnected()
{
    return connected.load();
}

void PicoWBluetooth::clearBonds()
{
    LOG_INFO("Clear BLE bonds");
    onLwip(
        [](void *) {
            int n = le_device_db_max_count();
            for (int i = 0; i < n; i++) {
                int addrType = BD_ADDR_TYPE_UNKNOWN;
                bd_addr_t addr;
                le_device_db_info(i, &addrType, addr, NULL);
                if (addrType != BD_ADDR_TYPE_UNKNOWN)
                    le_device_db_remove(i);
            }
        },
        nullptr);
}

int PicoWBluetooth::countBonds()
{
    if (!initialized)
        return 0;
    static int count;
    onLwip(
        [](void *) {
            count = 0;
            int n = le_device_db_max_count();
            for (int i = 0; i < n; i++) {
                int addrType = BD_ADDR_TYPE_UNKNOWN;
                bd_addr_t addr;
                le_device_db_info(i, &addrType, addr, NULL);
                if (addrType != BD_ADDR_TYPE_UNKNOWN)
                    count++;
            }
        },
        nullptr);
    return count;
}

void PicoWBluetooth::setBatteryLevel(uint8_t level)
{
    batteryLevel = level;
}

#endif // ARCH_RP2040 && HAS_BLUETOOTH
