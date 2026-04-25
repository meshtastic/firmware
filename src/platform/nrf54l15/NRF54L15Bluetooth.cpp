// NRF54L15Bluetooth.cpp — Zephyr BLE GATT peripheral for Meshtastic nRF54L15
//
// GATT profile (identical UUIDs to the nRF52 / NimBLE implementations):
//   Service:   6ba1b218-15a8-461f-9fa8-5dcae273eafd
//   fromNum:   ed9da18c-a800-4f66-a670-aa7547e34453  READ | NOTIFY
//   fromRadio: 2c55e69e-4993-11ed-b878-0242ac120002  READ
//   toRadio:   f75c76d2-129e-4dad-a1dd-7866124401e7  WRITE
//   logRadio:  5a3d6e49-06e6-4423-9944-e9de8cdf9547  READ | NOTIFY | INDICATE
//
// Threading model:
//   - BT RX thread: connected_cb / disconnected_cb / GATT read_/write_ callbacks
//   - Meshtastic OSThread scheduler (cooperative, main thread): BleDeferredThread
//     polls pendingToRadio and runs the zombie-connection watchdog every 100 ms
//   - PhoneAPI::onNowHasData: sends fromNum notify synchronously from whichever
//     thread pushed the packet (bt_gatt_notify is thread-safe in Zephyr)
//   - active_conn protected by ble_mutex where needed

#include "NRF54L15Bluetooth.h"
#include "BluetoothCommon.h"
#include "BluetoothStatus.h"
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "mesh/PhoneAPI.h"
#include "mesh/mesh-pb-constants.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

// ── UUID definitions (little-endian per Bluetooth spec) ───────────────────────
// Syntax: replace hyphens with commas, prefix 0x — matches BT_UUID_128_ENCODE doc.

#define MESH_SVC_UUID_VAL BT_UUID_128_ENCODE(0x6ba1b218, 0x15a8, 0x461f, 0x9fa8, 0x5dcae273eafd)
#define FROMNUM_UUID_VAL BT_UUID_128_ENCODE(0xed9da18c, 0xa800, 0x4f66, 0xa670, 0xaa7547e34453)
#define FROMRADIO_UUID_VAL BT_UUID_128_ENCODE(0x2c55e69e, 0x4993, 0x11ed, 0xb878, 0x0242ac120002)
#define TORADIO_UUID_VAL BT_UUID_128_ENCODE(0xf75c76d2, 0x129e, 0x4dad, 0xa1dd, 0x7866124401e7)
#define LOGRADIO_UUID_VAL BT_UUID_128_ENCODE(0x5a3d6e49, 0x06e6, 0x4423, 0x9944, 0xe9de8cdf9547)

static const struct bt_uuid_128 mesh_svc_uuid = BT_UUID_INIT_128(MESH_SVC_UUID_VAL);
static const struct bt_uuid_128 fromnum_uuid = BT_UUID_INIT_128(FROMNUM_UUID_VAL);
static const struct bt_uuid_128 fromradio_uuid = BT_UUID_INIT_128(FROMRADIO_UUID_VAL);
static const struct bt_uuid_128 toradio_uuid = BT_UUID_INIT_128(TORADIO_UUID_VAL);
static const struct bt_uuid_128 logradio_uuid = BT_UUID_INIT_128(LOGRADIO_UUID_VAL);

// ── Module state ─────────────────────────────────────────────────────────────

static struct bt_conn *active_conn = nullptr;
static K_MUTEX_DEFINE(ble_mutex);

static bool bt_initialized = false; // bt_enable() called at most once
static bool ble_enabled = false;    // set by setup(), cleared by shutdown()

// Forward declarations — BT_GATT_SERVICE_DEFINE(mesh_svc, ...) is below, but
// read_fromradio() (defined earlier) needs to reference the service to notify
// on fromNum after each non-empty read.
#define FROMNUM_ATTR_IDX 2
#define LOGRADIO_ATTR_IDX 9
extern const struct bt_gatt_service_static mesh_svc;

static void start_advertising(); // forward declaration (defined in advertising section below)

// Work item for advertising restart after disconnect.
//
// disconnected_cb runs on the BT RX thread (the same thread that processes
// HCI Command Complete events).  Calling bt_le_adv_start() → bt_hci_cmd_send_sync()
// directly from that thread deadlocks: the thread blocks on k_sem_take waiting
// for Command Complete, but it is the very thread that would process it.
// After 10 s the host panics with "Controller unresponsive, opcode 0x2006 timeout".
//
// Fix: submit a k_work item.  The system workqueue runs bt_adv_restart_work_fn
// on its own thread → no deadlock.
static struct k_work adv_restart_work;

static void adv_restart_work_fn(struct k_work *work)
{
    if (ble_enabled) {
        start_advertising();
    }
}

// CCC state: 0=off, BT_GATT_CCC_NOTIFY=notify, BT_GATT_CCC_INDICATE=indicate
static uint16_t fromnum_ccc_val = 0;
static uint16_t logradio_ccc_val = 0;

// Scratch buffers — only one BLE operation at a time
static uint8_t fromRadioBytes[meshtastic_FromRadio_size];
static size_t fromRadioLen = 0;
static uint8_t toRadioBytes[meshtastic_ToRadio_size];
static uint8_t lastToRadio[MAX_TO_FROM_RADIO_SIZE];
static uint32_t fromNumValue = 0;

// Deferred ToRadio processing
//
// write_toradio() runs on the BT RX workqueue thread (6 KB stack).  Calling
// phoneAPI->handleToRadio() directly triggers handleStartConfig →
// getFiles("/", 10) → nanopb encode, which overflows the stack on the exact
// "Client wants config" write.  Instead we copy the payload into a pending
// buffer under a mutex and let BleDeferredThread (running on the Meshtastic
// OSThread scheduler, 24 KB stack) do the actual call outside the lock.
//
// The mutex makes the producer/consumer handoff race-free — producer may
// overwrite a pending buffer the consumer hasn't read yet (dropped packet),
// but partial reads / torn writes are impossible.
K_MUTEX_DEFINE(pendingToRadioMutex);
static uint8_t pendingToRadioBuf[MAX_TO_FROM_RADIO_SIZE];
static size_t pendingToRadioLen = 0;
static bool pendingToRadio = false;

// Zombie-connection watchdog state.
//
// The nRF54L15 Zephyr 4.2.1 SW-LL occasionally fails to forward an
// LE Disconnection Complete event to the host: when iOS tears down the link
// (either explicitly by the user or via supervision timeout), the LL layer
// drops the connection but disconnected_cb never fires, active_conn stays
// non-null and advertising never restarts — the device vanishes from scans
// until power cycle.  Track the connected timestamp and the last time we
// observed ATT traffic; a long ATT idle on an "active" connection means we
// are zombied.  A cold reboot is the only path that reliably recovers (any
// bt_hci_cmd_send_sync after this state, e.g. bt_le_adv_start or
// bt_conn_disconnect, hangs in k_sem_take and later panics with "Controller
// unresponsive, opcode 0x2006 timeout").
static uint32_t connect_time_ms = 0;
static uint32_t last_att_time_ms = 0;

// ── BluetoothPhoneAPI ─────────────────────────────────────────────────────────

class BluetoothPhoneAPI : public PhoneAPI
{
    virtual void onNowHasData(uint32_t fromRadioNum) override;
    virtual bool checkIsConnected() override;

  public:
    BluetoothPhoneAPI() { api_type = TYPE_BLE; }
};

static BluetoothPhoneAPI *phoneAPI = nullptr;

// ── CCC change callbacks ──────────────────────────────────────────────────────

static void fromnum_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    fromnum_ccc_val = value;
    LOG_INFO("BLE fromNum CCC: %u", value);
}

static void logradio_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    logradio_ccc_val = value;
    LOG_INFO("BLE logRadio CCC: %u", value);
}

// ── GATT attribute callbacks ──────────────────────────────────────────────────

static ssize_t read_fromnum(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    LOG_INFO("GATT read_fromnum: fromNum=%u offset=%u", fromNumValue, offset);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &fromNumValue, sizeof(fromNumValue));
}

static ssize_t read_fromradio(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    if (offset == 0) {
        // First chunk: pull the next packet from the queue.
        // Subsequent chunks (offset > 0) are ATT_READ_BLOB continuations of the
        // same value and must reuse fromRadioBytes untouched.
        fromRadioLen = phoneAPI ? phoneAPI->getFromRadio(fromRadioBytes) : 0;
        LOG_DEBUG("GATT read_fromradio len=%u", (unsigned)fromRadioLen);
    }
    last_att_time_ms = k_uptime_get_32();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, fromRadioBytes, fromRadioLen);
}

static ssize_t read_logradio(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    // logRadio is write-only from the device side (notify/indicate).
    // Return an empty read so GATT discovery doesn't fail with NOT_PERMITTED.
    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t write_toradio(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
                             uint16_t offset, uint8_t flags)
{
    // Writes >MTU-3 arrive here with offset=0 and flags=BT_GATT_WRITE_FLAG_EXECUTE
    // after Zephyr reassembles the ATT Prepare Write fragments
    // (CONFIG_BT_ATT_PREPARE_COUNT>0).  Single writes arrive with flags=0.
    LOG_DEBUG("GATT write_toradio len=%u flags=0x%x", len, flags);
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len > sizeof(toRadioBytes)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    // Deduplicate — drop packet if identical to the last one we processed
    if (len <= MAX_TO_FROM_RADIO_SIZE && memcmp(lastToRadio, buf, len) != 0) {
        memcpy(lastToRadio, buf, len);
        if (len < MAX_TO_FROM_RADIO_SIZE) {
            memset(lastToRadio + len, 0, MAX_TO_FROM_RADIO_SIZE - len);
        }
        // Defer handleToRadio() to BleDeferredThread (24 KB stack).
        // Running it here on bt_workq (6 KB) overflows during handleStartConfig.
        // Always overwrite pending — we already dedup'd above via lastToRadio,
        // so any new write here is genuinely new data that must be delivered.
        k_mutex_lock(&pendingToRadioMutex, K_FOREVER);
        memcpy(pendingToRadioBuf, buf, len);
        pendingToRadioLen = len;
        pendingToRadio = true;
        k_mutex_unlock(&pendingToRadioMutex);
    }
    last_att_time_ms = k_uptime_get_32();
    return (ssize_t)len;
}

// ── GATT service definition (static, linked at compile time) ──────────────────
//
// Attribute indices (0-based):
//   [0]  Primary Service declaration
//   [1]  fromNum characteristic declaration
//   [2]  fromNum value            ← notify target (FROMNUM_ATTR_IDX)
//   [3]  fromNum CCC descriptor
//   [4]  fromRadio characteristic declaration
//   [5]  fromRadio value
//   [6]  toRadio characteristic declaration
//   [7]  toRadio value
//   [8]  logRadio characteristic declaration
//   [9]  logRadio value           ← notify target (LOGRADIO_ATTR_IDX)
//   [10] logRadio CCC descriptor

// All user characteristics require authenticated encryption (MITM passkey)
// before the client can read/write. This mirrors the nrf52 SECMODE_ENC_WITH_MITM
// service permission. The stack returns "Insufficient Authentication" on the
// first access attempt, prompting the client to pair with the configured PIN.
#define MESH_PERM_READ  (BT_GATT_PERM_READ | BT_GATT_PERM_READ_AUTHEN)
#define MESH_PERM_WRITE (BT_GATT_PERM_WRITE | BT_GATT_PERM_WRITE_AUTHEN)

BT_GATT_SERVICE_DEFINE(
    mesh_svc, BT_GATT_PRIMARY_SERVICE(&mesh_svc_uuid.uuid),

    // fromNum: READ | NOTIFY — packet-counter triggers phone to read fromRadio
    BT_GATT_CHARACTERISTIC(&fromnum_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, MESH_PERM_READ, read_fromnum, NULL,
                           &fromNumValue),
    BT_GATT_CCC(fromnum_ccc_changed, MESH_PERM_READ | MESH_PERM_WRITE),

    // fromRadio: READ — phone polls this after receiving a fromNum notification
    BT_GATT_CHARACTERISTIC(&fromradio_uuid.uuid, BT_GATT_CHRC_READ, MESH_PERM_READ, read_fromradio, NULL, NULL),

    // toRadio: WRITE — phone sends protobuf packets to the device
    BT_GATT_CHARACTERISTIC(&toradio_uuid.uuid, BT_GATT_CHRC_WRITE, MESH_PERM_WRITE, NULL, write_toradio, NULL),

    // logRadio: READ | NOTIFY | INDICATE — log stream to phone when connected
    BT_GATT_CHARACTERISTIC(&logradio_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE,
                           MESH_PERM_READ, read_logradio, NULL, NULL),
    BT_GATT_CCC(logradio_ccc_changed, MESH_PERM_READ | MESH_PERM_WRITE), );

// ── Advertising ───────────────────────────────────────────────────────────────
//
// Use legacy advertising (bt_le_adv_start / HCI 0x2006 path).
//
// History: we previously used bt_le_ext_adv_create (true extended advertising)
// because bt_le_adv_start() with CONFIG_BT_EXT_ADV=y was translated internally
// to the extended HCI path with LEGACY-bit (0x2036), which produced
// non-connectable PDUs on the nRF54L15 SW-LL.  The true extended path
// (0x203x, AUX_ADV_IND) was connectable but caused two problems:
//   1. iOS CoreBluetooth does not reliably complete GATT after connecting via
//      extended advertising (zero ATT PDUs observed in all test sessions).
//   2. After each connection the controller auto-stops the advertising set, and
//      the subsequent bt_le_ext_adv_delete() sends LE Remove Advertising Set
//      (0x203c) which times out → kernel oops at hci_core.c:506.
//
// With CONFIG_BT_EXT_ADV=n the host uses pure legacy HCI commands — the same
// path Nordic NCS uses in all nRF54L15 examples (peripheral_uart, peripheral_lbs)
// and which is universally iOS-compatible.  The legacy data payload is 31 bytes:
//   FLAGS (3B) + UUID128 (18B) = 21B in adv; NAME in scan-response (17B).

static void start_advertising()
{
    // IMPORTANT: BT_DATA_BYTES() uses C99 compound literals that GCC C++ treats
    // as temporaries; with -Os the compiler may elide writes, leaving stack
    // uninitialized.  Use static const arrays for stable data (flags, UUID)
    // and a runtime pointer for the dynamic device name.
    static const uint8_t adv_flags_val[] = {BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR};
    static const uint8_t adv_uuid128_val[] = {MESH_SVC_UUID_VAL};

    const char *name = bt_get_name();
    uint8_t name_len = (uint8_t)strlen(name);

    // Primary advertising data: FLAGS + Meshtastic service UUID128 (21 bytes total)
    struct bt_data ad[] = {
        {BT_DATA_FLAGS, sizeof(adv_flags_val), adv_flags_val},
        {BT_DATA_UUID128_ALL, sizeof(adv_uuid128_val), adv_uuid128_val},
    };
    // Scan response: device name (discovered after scan request)
    struct bt_data sd[] = {
        {BT_DATA_NAME_COMPLETE, name_len, (const uint8_t *)name},
    };

    // BT_LE_ADV_OPT_CONN         = connectable legacy ADV_IND + stops after first
    //                              connection (replaces deprecated CONNECTABLE|ONE_TIME
    //                              in Zephyr 4.2.1; BT_LE_ADV_OPT_CONN = BIT(0)|BIT(1))
    // BT_LE_ADV_OPT_USE_IDENTITY = use static random identity address (stable across reboots)
    // Advertising restart after disconnect is via adv_restart_work (system workqueue)
    // so calling bt_le_adv_start() from the BT RX thread context is avoided.
    int err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY, BT_GAP_ADV_FAST_INT_MIN_2,
                                              BT_GAP_ADV_FAST_INT_MAX_2, NULL),
                              ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    if (err == -EALREADY) {
        return;
    }
    if (err) {
        LOG_WARN("BLE adv start failed: %d", err);
    } else {
        LOG_INFO("BLE advertising as '%s'", bt_get_name());
    }
}

static void stop_advertising()
{
    bt_le_adv_stop();
}

// ── Connection callbacks ──────────────────────────────────────────────────────

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_WARN("BLE connection failed, err=0x%02x", err);
        return;
    }

    k_mutex_lock(&ble_mutex, K_FOREVER);
    active_conn = bt_conn_ref(conn);
    k_mutex_unlock(&ble_mutex);

    memset(lastToRadio, 0, sizeof(lastToRadio));
    connect_time_ms = k_uptime_get_32();
    last_att_time_ms = connect_time_ms;

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INFO("BLE connected: %s", addr);

    meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
    bluetoothStatus->updateStatus(&newStatus);

    // nRF54L15-DK has no screen — cannot display a PIN to the user.
    // Requesting BT_SECURITY_L2 causes the OS to show a pairing dialog that
    // the user dismisses, triggering disconnect + advertising restart failure.
    // Skip security negotiation; the Meshtastic app works over plain GATT.
    // (Security can be re-enabled once a display or NFC OOB path is available.)
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    LOG_INFO("BLE disconnected, reason=0x%02x", reason);

    k_mutex_lock(&ble_mutex, K_FOREVER);
    if (active_conn) {
        bt_conn_unref(active_conn);
        active_conn = nullptr;
    }
    k_mutex_unlock(&ble_mutex);

    fromnum_ccc_val = 0;
    logradio_ccc_val = 0;
    connect_time_ms = 0;
    last_att_time_ms = 0;

    if (phoneAPI) {
        phoneAPI->close();
    }
    memset(lastToRadio, 0, sizeof(lastToRadio));

    meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
    bluetoothStatus->updateStatus(&newStatus);

    // Schedule advertising restart via work queue — NOT from this callback directly.
    // disconnected_cb runs on the BT RX thread; calling bt_le_adv_start() here
    // would deadlock (see adv_restart_work comment above).
    if (ble_enabled) {
        k_work_submit(&adv_restart_work);
    }
}

#if defined(CONFIG_BT_SMP)
static void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
        // Phone has a stale bond (device was wiped/reflashed).  Unpair the stale
        // entry so the phone re-pairs cleanly on the next connection attempt.
        LOG_WARN("BLE stale bond detected (key missing) — unpairing");
        bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
        bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    } else if (err) {
        LOG_WARN("BLE security change failed: level=%d err=%d", (int)level, (int)err);
    } else {
        LOG_INFO("BLE security level %d established", (int)level);
    }
}
#endif /* CONFIG_BT_SMP */

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
#if defined(CONFIG_BT_SMP)
    .security_changed = security_changed_cb,
#endif
};

// ── Pairing / auth callbacks ──────────────────────────────────────────────────

#if defined(CONFIG_BT_SMP)
static uint32_t configuredPasskey;

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char passkey_str[7];
    snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);
    configuredPasskey = passkey;
    LOG_INFO("BLE pairing PIN: %s", passkey_str);
    powerFSM.trigger(EVENT_BLUETOOTH_PAIR);

    std::string textkey(passkey_str);
    meshtastic::BluetoothStatus pairingStatus(textkey);
    bluetoothStatus->updateStatus(&pairingStatus);
}

static void auth_cancel(struct bt_conn *conn)
{
    LOG_WARN("BLE pairing cancelled");
}

static struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .passkey_entry = NULL,
    .cancel = auth_cancel,
};

static void pairing_complete_cb(struct bt_conn *conn, bool bonded)
{
    LOG_INFO("BLE pairing complete, bonded=%d", (int)bonded);
    meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::CONNECTED);
    bluetoothStatus->updateStatus(&newStatus);
}

static void pairing_failed_cb(struct bt_conn *conn, enum bt_security_err reason)
{
    LOG_WARN("BLE pairing failed, reason=%d", (int)reason);
    meshtastic::BluetoothStatus newStatus(meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED);
    bluetoothStatus->updateStatus(&newStatus);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete_cb,
    .pairing_failed = pairing_failed_cb,
};
#endif /* CONFIG_BT_SMP */

// ── BluetoothPhoneAPI methods ─────────────────────────────────────────────────

void BluetoothPhoneAPI::onNowHasData(uint32_t fromRadioNum)
{
    PhoneAPI::onNowHasData(fromRadioNum);
    fromNumValue = fromRadioNum;

    if (!(fromnum_ccc_val & BT_GATT_CCC_NOTIFY))
        return;

    // active_conn may be torn down on another thread while we're dispatching
    // this notify — take a reference under the BT host's own lock so the conn
    // object can't be freed mid-call.
    struct bt_conn *conn = active_conn ? bt_conn_ref(active_conn) : nullptr;
    if (!conn)
        return;
    bt_gatt_notify(conn, &mesh_svc.attrs[FROMNUM_ATTR_IDX], &fromNumValue, sizeof(fromNumValue));
    bt_conn_unref(conn);
}

bool BluetoothPhoneAPI::checkIsConnected()
{
    return active_conn != nullptr;
}

// ── Deferred ToRadio processor + zombie-connection watchdog ──────────────────
//
// write_toradio() runs on the BT RX workqueue thread (CONFIG_BT_RX_STACK_SIZE)
// and cannot execute phoneAPI->handleToRadio() directly: handleStartConfig
// recurses through nanopb encode + state machine init and overflows the RX
// stack.  This thread runs on the Meshtastic OSThread scheduler (24 KB stack),
// picks up the pending ToRadio buffer flagged by write_toradio(), and calls
// handleToRadio() with plenty of headroom.
//
// Real-time fromNum notifications are sent synchronously from
// BluetoothPhoneAPI::onNowHasData() (called by PhoneAPI when new data is
// queued).
//
// Zombie-connection detection has two tiers:
//
//   (1) Liveness probe.  After IDLE_BEFORE_PROBE_MS without ATT traffic, send
//       a bt_gatt_notify to fromNum every PROBE_INTERVAL_MS.  If the
//       controller replies -ENOTCONN the LL link is definitely dead but the
//       host didn't forward LE Disconnection Complete → reboot.  We avoid
//       probing during normal activity so iOS isn't woken up unnecessarily
//       (each probe wakes iOS → triggers a zero-byte FromRadio drain).
//
//   (2) Hard watchdog.  Absolute HARD_WATCHDOG_MS ceiling on ATT idle as a
//       fallback if probes somehow don't detect the zombie.
class BleDeferredThread : public concurrency::OSThread
{
    static constexpr uint32_t IDLE_BEFORE_PROBE_MS = 30000; //  30 s: start probing
    static constexpr uint32_t PROBE_INTERVAL_MS = 5000;     //   5 s: between probes
    static constexpr uint32_t HARD_WATCHDOG_MS = 60000;     //   1 min: last resort

    uint32_t last_probe_ms = 0;

  public:
    BleDeferredThread() : concurrency::OSThread("BleDeferred") {}

  protected:
    int32_t runOnce() override
    {
        // Snapshot the pending ToRadio buffer under the mutex, then release
        // the lock before calling into handleToRadio (which can be slow and
        // must not block the BT RX thread producer).
        uint8_t buf[MAX_TO_FROM_RADIO_SIZE];
        size_t n = 0;
        bool have_pending = false;
        k_mutex_lock(&pendingToRadioMutex, K_FOREVER);
        if (pendingToRadio) {
            memcpy(buf, pendingToRadioBuf, pendingToRadioLen);
            n = pendingToRadioLen;
            pendingToRadio = false;
            have_pending = true;
        }
        k_mutex_unlock(&pendingToRadioMutex);
        if (have_pending && phoneAPI) {
            phoneAPI->handleToRadio(buf, n);
        }

        // Take a reference to active_conn so it can't be freed underneath us
        // if disconnected_cb fires on another thread while we're dispatching.
        struct bt_conn *conn = active_conn ? bt_conn_ref(active_conn) : nullptr;
        if (!conn || connect_time_ms == 0) {
            if (conn)
                bt_conn_unref(conn);
            last_probe_ms = 0;
            return 100;
        }

        uint32_t now = k_uptime_get_32();
        uint32_t att_idle = now - last_att_time_ms;

        // Liveness probe — only when ATT has been quiet for a while.
        if (att_idle > IDLE_BEFORE_PROBE_MS && (now - last_probe_ms) >= PROBE_INTERVAL_MS &&
            (fromnum_ccc_val & BT_GATT_CCC_NOTIFY)) {
            last_probe_ms = now;
            int err = bt_gatt_notify(conn, &mesh_svc.attrs[FROMNUM_ATTR_IDX], &fromNumValue, sizeof(fromNumValue));
            if (err == -ENOTCONN) {
                LOG_WARN("BLE zombie (probe ENOTCONN); rebooting");
                bt_conn_unref(conn);
                k_sleep(K_MSEC(50)); // flush log
                sys_reboot(SYS_REBOOT_COLD);
            }
        }
        bt_conn_unref(conn);

        // Hard ceiling — last-resort reboot if probes miss the zombie.
        if (att_idle > HARD_WATCHDOG_MS && (now - connect_time_ms) > HARD_WATCHDOG_MS) {
            LOG_WARN("BLE zombie (hard watchdog %us); rebooting", HARD_WATCHDOG_MS / 1000);
            k_sleep(K_MSEC(50));
            sys_reboot(SYS_REBOOT_COLD);
        }
        return 100;
    }
};

static BleDeferredThread *bleDeferredThread = nullptr;

// ── BT stack pre-initializer (call from main thread before OSThreads start) ──
//
// bt_enable() requires substantially more stack than a Meshtastic OSThread
// (PowerFSMThread) provides — calling it there causes a stack overflow.
// Call this from nrf54l15Setup() (main Zephyr thread, CONFIG_MAIN_STACK_SIZE)
// so that by the time NRF54L15Bluetooth::setup() runs from PowerFSMThread,
// bt_initialized is already true and bt_enable() is skipped.

void nrf54l15_bt_preinit()
{
    if (!bt_initialized) {
        int err = bt_enable(NULL);
        if (err) {
            LOG_ERROR("BLE pre-init failed: %d", err);
            return;
        }
        bt_initialized = true;
        LOG_INFO("BLE stack pre-initialized on main thread");

        // Phase 7: load bonding keys from LittleFS (/lfs/bt_settings).
        // LittleFS is already mounted by fsInit() before nrf54l15Setup() runs.
        // On first boot the file doesn't exist — settings_load() returns 0 (OK).
        // On subsequent boots, previously bonded peers are restored so the
        // phone can reconnect without re-pairing.
        err = settings_load();
        if (err) {
            LOG_WARN("settings_load failed: %d (OK on first boot)", err);
        } else {
            LOG_INFO("BT settings loaded from /lfs/bt_settings");
        }
    }
}

// ── NRF54L15Bluetooth public methods ─────────────────────────────────────────

// Shared init: idempotent setup of work item, OSThread, auth callbacks, bt_enable,
// and device name. Leaves advertising control to the caller.
static bool nrf54l15_bt_init_common()
{
    k_work_init(&adv_restart_work, adv_restart_work_fn);

    if (!bleDeferredThread) {
        bleDeferredThread = new BleDeferredThread();
    }

    if (!phoneAPI) {
        phoneAPI = new BluetoothPhoneAPI();
    }

#if defined(CONFIG_BT_SMP)
    if (config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN) {
        bt_conn_auth_cb_register(&auth_cb);
        bt_conn_auth_info_cb_register(&auth_info_cb);

        // FIXED_PIN — register the configured passkey so the mobile app prompts
        // the user for that specific number instead of a random display-only PIN.
        // RANDOM_PIN keeps the default behavior: Zephyr generates a fresh passkey
        // on each pairing attempt and fires auth_passkey_display with it.
        if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN) {
            configuredPasskey = config.bluetooth.fixed_pin;
            int rc = bt_passkey_set(configuredPasskey);
            if (rc) {
                LOG_WARN("bt_passkey_set(%u) failed: %d", configuredPasskey, rc);
            } else {
                LOG_INFO("BLE fixed PIN: %06u", configuredPasskey);
            }
        } else {
            bt_passkey_set(BT_PASSKEY_INVALID); // random per-pair
        }
    }
#endif /* CONFIG_BT_SMP */

    if (!bt_initialized) {
        int err = bt_enable(NULL);
        if (err) {
            LOG_ERROR("BLE enable failed: %d", err);
            return false;
        }
        bt_initialized = true;
        LOG_INFO("BLE stack enabled");
    }

    bt_set_name(getDeviceName());
    return true;
}

void NRF54L15Bluetooth::setup()
{
    LOG_INFO("NRF54L15Bluetooth::setup()");
    if (!nrf54l15_bt_init_common()) {
        return;
    }
    ble_enabled = true;
    start_advertising();
}

void NRF54L15Bluetooth::shutdown()
{
    LOG_INFO("NRF54L15Bluetooth::shutdown()");
    ble_enabled = false;
    stop_advertising();

    k_mutex_lock(&ble_mutex, K_FOREVER);
    struct bt_conn *conn = active_conn ? bt_conn_ref(active_conn) : nullptr;
    k_mutex_unlock(&ble_mutex);

    if (conn) {
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(conn);
    }
}

void NRF54L15Bluetooth::startDisabled()
{
    // Initialize BT stack but leave advertising off until resumeAdvertising().
    if (!nrf54l15_bt_init_common()) {
        return;
    }
    ble_enabled = false;
    LOG_INFO("BLE initialized, advertising stopped (startDisabled)");
}

void NRF54L15Bluetooth::resumeAdvertising()
{
    ble_enabled = true;
    start_advertising();
}

void NRF54L15Bluetooth::clearBonds()
{
    LOG_INFO("BLE clear bonds");
    bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
}

bool NRF54L15Bluetooth::isConnected()
{
    return active_conn != nullptr;
}

int NRF54L15Bluetooth::getRssi()
{
    return 0; // TODO: Zephyr has no direct bt_conn_get_rssi; use HCI RSSI read command
}

void NRF54L15Bluetooth::sendLog(const uint8_t *logMessage, size_t length)
{
    if (!active_conn || length > 512 || logradio_ccc_val == 0) {
        return;
    }
    // Send as notify regardless of whether client subscribed to NOTIFY or INDICATE —
    // bt_gatt_indicate() requires a params struct with a callback; notify is simpler
    // and the app accepts both. Change to indicate if compatibility issues arise.
    bt_gatt_notify(active_conn, &mesh_svc.attrs[LOGRADIO_ATTR_IDX], logMessage, (uint16_t)length);
}
