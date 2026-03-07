# BLE Mesh Implementation Plan

## Overview

Add BLE as a mesh transport alongside LoRa, enabling Meshtastic nodes to relay packets over Bluetooth Low Energy. Follow the same handler pattern used by UDP multicast ([UdpMulticastHandler](src/mesh/udp/UdpMulticastHandler.h)) — a standalone handler class with `onSend()` hooked into `Router::send()` and received packets injected via `router->enqueueReceivedMessage()`. No changes to `RadioInterface` or Router architecture needed.

BLE mesh is useful for:

- Dense indoor deployments where LoRa range is excessive
- Extending mesh reach through devices that lack LoRa hardware
- Hybrid LoRa+BLE networks (gateway nodes bridge both transports)
- Lower latency links between nearby nodes

---

## Architecture

### Pattern: Same as UDP Multicast

```
                                    Router::send()
                                        │
                    ┌───────────────────┼───────────────────┐
                    │                   │                   │
                    ▼                   ▼                   ▼
            iface->send(p)    udpHandler->onSend(p)   bleMeshHandler->onSend(p)
               (LoRa)              (UDP)                (BLE mesh)

                                Router::enqueueReceivedMessage()
                                        ▲
                    ┌───────────────────┼───────────────────┐
                    │                   │                   │
             LoRa RX ISR     udpHandler->onReceive()  bleMeshHandler->onReceive()
```

The existing UDP pattern ([Router.cpp:378-382](src/mesh/Router.cpp#L378-L382)):

```cpp
#if HAS_UDP_MULTICAST
    if (udpHandler && config.network.enabled_protocols & ...) {
        udpHandler->onSend(const_cast<meshtastic_MeshPacket *>(p));
    }
#endif
```

BLE mesh hooks in identically, right next to it.

---

## Phase 1: Protobuf & Configuration

### 1.1 Transport mechanism enum

**File:** protobufs repo (`mesh.proto`)

Add a new transport type alongside the existing ones:

```protobuf
enum TransportMechanism {
    // ... existing 0-7 ...
    TRANSPORT_BLE_MESH = 8;
}
```

### 1.2 BLE Mesh configuration

Add BLE mesh config to the device config protobuf:

```protobuf
message BleMeshConfig {
    bool enabled = 1;

    // Operating mode
    enum Mode {
        OFF = 0;
        ADVERTISE_AND_SCAN = 1;  // connectionless (extended adv)
        GATT = 2;                // connection-based
        HYBRID = 3;              // both
    }
    Mode mode = 2;

    // Scan/advertising interval in ms (latency vs power tradeoff)
    uint32_t interval_ms = 3;  // default: 500ms

    // TX power level (platform-mapped)
    int8_t tx_power = 4;  // dBm

    // Max BLE mesh peers to track
    uint32_t max_peers = 5;  // default: 8
}
```

---

## Phase 2: BLE Mesh Handler — Core Class

### 2.1 Base handler (platform-agnostic)

**New file:** `src/mesh/BLEMeshHandler.h`

Follow the `UdpMulticastHandler` pattern — a simple class, not a `RadioInterface`:

```cpp
#pragma once
#if HAS_BLE_MESH

#include "configuration.h"
#include "mesh/Router.h"

class BLEMeshHandler {
public:
    BLEMeshHandler() : isRunning(false) {}

    virtual void start() = 0;
    virtual void stop() = 0;

    // Called from Router::send() — broadcast packet over BLE
    virtual bool onSend(const meshtastic_MeshPacket *mp) = 0;

protected:
    bool isRunning;

    // Shared RX path: decode and enqueue into router
    void deliverToRouter(const uint8_t *data, size_t len) {
        meshtastic_MeshPacket mp;
        if (!pb_decode_from_bytes(data, len, &meshtastic_MeshPacket_msg, &mp))
            return;
        if (mp.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
            return;

        mp.transport_mechanism =
            meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_MESH;
        mp.rx_snr = 0;
        mp.rx_rssi = 0;

        UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
        if (router)
            router->enqueueReceivedMessage(p.release());
    }

    // Shared TX path: encode packet to bytes
    size_t encodeForBLE(const meshtastic_MeshPacket *mp, uint8_t *buf, size_t bufLen) {
        return pb_encode_to_bytes(buf, bufLen, &meshtastic_MeshPacket_msg, mp);
    }
};

#endif // HAS_BLE_MESH
```

### 2.2 Wire format

Same as UDP multicast — protobuf-encoded `meshtastic_MeshPacket` (already encrypted). This means:

- Packets are encoded with `pb_encode_to_bytes()` on TX
- Decoded with `pb_decode_from_bytes()` on RX
- Encryption already applied by Router before `onSend()` is called
- Same deduplication via `PacketHistory` (keyed on `sender, id`)

### 2.3 Echo prevention

Same pattern as UDP ([UdpMulticastHandler.h:102](src/mesh/udp/UdpMulticastHandler.h#L102)):

```cpp
if (mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_MESH) {
    LOG_ERROR("Attempt to send BLE-sourced packet over BLE");
    return false;
}
```

---

## Phase 3: ESP32 Implementation (NimBLE)

### Platform: ESP32, ESP32-S3, ESP32-C3, ESP32-C6

**New files:** `src/nimble/NimbleBLEMesh.h` / `NimbleBLEMesh.cpp`

```cpp
#pragma once
#if HAS_BLE_MESH && defined(ARCH_ESP32)

#include "mesh/BLEMeshHandler.h"
#include "nimble/NimbleBluetooth.h"

class NimbleBLEMesh : public BLEMeshHandler {
public:
    void start() override;
    void stop() override;
    bool onSend(const meshtastic_MeshPacket *mp) override;

private:
    // --- Advertising mode ---
    void startScanning();
    void stopScanning();
    static void onScanResult(const ble_gap_disc_desc *desc, void *arg);
    void broadcastViaAdv(const uint8_t *data, size_t len);

    // --- GATT mode ---
    void setupMeshService();
    static int onMeshCharWrite(uint16_t conn_handle,
                               const ble_gatt_error *error,
                               ble_gatt_attr *attr, void *arg);
    void sendViaGATT(const uint8_t *data, size_t len);

    // --- Peer tracking ---
    struct BLEPeer {
        NodeNum nodeNum;
        ble_addr_t addr;
        int8_t rssi;
        uint32_t lastSeenMs;
        uint16_t connHandle;  // 0 if not connected
    };
    std::vector<BLEPeer> peers;
    void updatePeer(const ble_addr_t &addr, NodeNum nodeNum, int8_t rssi);
    void pruneStale();
};

#endif
```

### 3.1 Advertising mode (connectionless)

Encode mesh packets into BLE extended advertisements with Meshtastic manufacturer data.

**TX flow:**

1. `onSend()` called from `Router::send()`
2. Encode packet via `encodeForBLE()`
3. Pack into extended advertisement manufacturer data
4. Broadcast for configurable number of intervals

**RX flow:**

1. NimBLE scan callback fires
2. Check manufacturer ID for Meshtastic magic
3. Extract protobuf bytes
4. Call `deliverToRouter()`

**Advertisement structure:**

```
AD Type: 0xFF (Manufacturer Specific)
Company ID: 0x????? (Meshtastic BLE SIG ID or 0xFFFF)
Mesh Version: 1 byte
Protobuf payload: variable (up to ~244 bytes with extended adv)
```

**Key NimBLE APIs:**

- `ble_gap_ext_adv_set_data()` / `ble_gap_ext_adv_start()` — extended adv TX
- `ble_gap_disc()` or `ble_gap_ext_disc()` — scanning
- Scan callback receives `ble_gap_disc_desc` or `ble_gap_ext_disc_desc`

### 3.2 GATT mode (connection-based)

Add a new GATT service for mesh relay, separate from the phone API service:

**New Mesh Relay Service:**

```
Mesh Relay Service UUID: (new, distinct from phone service in BluetoothCommon.h)
├── MeshTX (WRITE_NO_RSP) — peers write mesh packets to us
├── MeshRX (NOTIFY)       — we notify peers of new mesh packets
└── MeshPeer (READ)       — our NodeNum for discovery
```

**TX:** Write to MeshTX on all connected peers (broadcast) or specific next-hop peer (directed).
**RX:** MeshTX write callback → `deliverToRouter()`.

### 3.3 Coexistence with phone BLE

Both services registered on the same NimBLE host:

- Phone API service: existing UUIDs ([BluetoothCommon.h](src/BluetoothCommon.h))
- Mesh relay service: new UUIDs
- Separate connection handles for phone vs mesh peers
- Use extended adv set 0 for phone, set 1 for mesh (NimBLE supports multiple adv sets)
- Reserve 1 connection for phone, remaining for mesh peers

### 3.4 ESP32 variant considerations

| Variant  | BLE | Extended Adv | Max Conn | Strategy                                |
| -------- | --- | ------------ | -------- | --------------------------------------- |
| ESP32    | 4.2 | No           | 9        | GATT-only (legacy adv too small at 31B) |
| ESP32-S3 | 5.0 | Yes          | 9        | Full adv+GATT                           |
| ESP32-C3 | 5.0 | Yes          | 3        | Adv preferred (limited connections)     |
| ESP32-C6 | 5.0 | Yes          | 9        | Full adv+GATT                           |

---

## Phase 4: nRF52 Implementation (SoftDevice/Bluefruit)

### Platform: nRF52840 (primary), nRF52833

**New files:** `src/platform/nrf52/NRF52BLEMesh.h` / `NRF52BLEMesh.cpp`

```cpp
#pragma once
#if HAS_BLE_MESH && defined(ARCH_NRF52)

#include "mesh/BLEMeshHandler.h"
#include <bluefruit.h>

class NRF52BLEMesh : public BLEMeshHandler {
public:
    void start() override;
    void stop() override;
    bool onSend(const meshtastic_MeshPacket *mp) override;

private:
    // Advertising mode
    void startScanning();
    static void onScanCallback(ble_gap_evt_adv_report_t *report);
    void broadcastViaAdv(const uint8_t *data, size_t len);

    // GATT mode
    BLEService meshService;
    BLECharacteristic meshTxChar;
    BLECharacteristic meshRxChar;
    void setupMeshService();
    static void onMeshWrite(uint16_t connHandle, BLECharacteristic *chr,
                            uint8_t *data, uint16_t len);
    void sendViaGATT(const uint8_t *data, size_t len);

    // Peers
    struct BLEPeer {
        NodeNum nodeNum;
        ble_gap_addr_t addr;
        int8_t rssi;
        uint32_t lastSeenMs;
        uint16_t connHandle;
    };
    std::vector<BLEPeer> peers;
};

#endif
```

### 4.1 Advertising mode

nRF52840 supports BLE 5.0 extended advertisements (up to 247 bytes).

**Bluefruit APIs:**

- `Bluefruit.Scanner.setRxCallback()` — scan result callback
- `Bluefruit.Scanner.start()` / `.stop()` — control scanning
- `Bluefruit.Advertising` — for standard adv
- Raw SoftDevice `sd_ble_gap_adv_set_configure()` if Bluefruit doesn't expose extended adv data

**Implementation:**

- Use Bluefruit Scanner for RX (filter by Meshtastic manufacturer ID)
- For TX, either use Bluefruit Advertising API or drop to raw SoftDevice for extended adv payloads

### 4.2 GATT mode

Same service structure as ESP32, using Bluefruit GATT APIs:

```cpp
meshService = BLEService(MESH_RELAY_SERVICE_UUID);
meshTxChar = BLECharacteristic(MESH_TX_CHAR_UUID);
meshTxChar.setProperties(CHR_PROPS_WRITE_WO_RESP);
meshTxChar.setWriteCallback(onMeshWrite);
meshTxChar.setMaxLen(512);

meshRxChar = BLECharacteristic(MESH_RX_CHAR_UUID);
meshRxChar.setProperties(CHR_PROPS_NOTIFY);
meshRxChar.setMaxLen(512);
```

**Peer connections:**

- nRF52840 SoftDevice S140 supports up to 20 concurrent connections
- Use `Bluefruit.Central` for outbound connections to mesh peers
- Bluefruit supports simultaneous peripheral (phone) + central (mesh peers)

### 4.3 Coexistence with phone BLE

Existing phone service ([NRF52Bluetooth.cpp](src/platform/nrf52/NRF52Bluetooth.cpp)) uses peripheral role:

- Keep phone API service as-is
- Add mesh relay service alongside it
- Scanner shared between peer discovery and mesh RX (filter by service UUID or manufacturer data)
- SoftDevice connection budget: 1 peripheral (phone) + N central (mesh peers)

### 4.4 nRF52 extras

| Feature                | nRF52840 | nRF52833 |
| ---------------------- | -------- | -------- |
| Extended Adv           | Yes      | Yes      |
| Max Connections        | 20       | 20       |
| RAM per connection     | ~1.8KB   | ~1.8KB   |
| Coded PHY (Long Range) | Yes      | Yes      |

**Coded PHY:** BLE Coded PHY (125kbps) provides ~4x range vs 1Mbps PHY. Could be used for extended-range BLE mesh links as a configuration option.

---

## Phase 5: Router Integration

### 5.1 Hook into Router::send()

**File:** [src/mesh/Router.cpp](src/mesh/Router.cpp) — add right next to UDP handler:

```cpp
#if HAS_BLE_MESH
    if (bleMeshHandler && config.bluetooth.mesh_enabled) {
        bleMeshHandler->onSend(p);
    }
#endif
```

### 5.2 Global handler instance

**File:** [src/main.h](src/main.h):

```cpp
#ifdef HAS_BLE_MESH
#include "mesh/BLEMeshHandler.h"
extern BLEMeshHandler *bleMeshHandler;
#endif
```

**File:** [src/main.cpp](src/main.cpp):

```cpp
#ifdef HAS_BLE_MESH
BLEMeshHandler *bleMeshHandler = nullptr;
#endif

// In setup():
#ifdef HAS_BLE_MESH
    #if defined(ARCH_ESP32)
        bleMeshHandler = new NimbleBLEMesh();
    #elif defined(ARCH_NRF52)
        bleMeshHandler = new NRF52BLEMesh();
    #endif
    bleMeshHandler->start();
#endif
```

### 5.3 NODENUM_BROADCAST_NO_LORA

Already reserved ([MeshTypes.h:13-14](src/mesh/MeshTypes.h#L13-L14)) and already dropped by LoRa ([RadioLibInterface.cpp:170](src/mesh/RadioLibInterface.cpp#L170)). BLE mesh handler should still send packets addressed to `NODENUM_BROADCAST_NO_LORA` — this is its purpose.

### 5.4 Feature flag

**File:** `src/configuration.h` or per-variant `platformio.ini`:

```cpp
#define HAS_BLE_MESH 1  // Enable BLE mesh transport
```

Disabled by default. Enabled per-board in variant configs for boards with sufficient BLE capability.

---

## Phase 6: Peer Discovery & Management

### 6.1 Discovery methods

1. **Passive scanning**: Listen for Meshtastic manufacturer data in BLE advertisements
2. **Service discovery**: Scan for Mesh Relay Service UUID (GATT mode)
3. **NodeDB integration**: Update `meshtastic_NodeInfoLite` with BLE reachability when a peer is discovered

### 6.2 Peer table

```cpp
struct BLEMeshPeer {
    NodeNum nodeNum;
    // Platform-specific BLE address (ble_addr_t on ESP32, ble_gap_addr_t on nRF52)
    uint8_t addr[6];
    uint8_t addrType;
    int8_t rssi;
    uint32_t lastSeenMs;
    uint16_t connHandle;      // 0 = not connected
    uint8_t failedConnAttempts;
};
```

Max peers: configurable (default 8), bounded by platform connection limits.

### 6.3 Connection policy (GATT mode)

- Auto-connect if RSSI > threshold and slots available
- Disconnect stale peers (no traffic for N minutes)
- Prefer advertising for broadcasts, GATT for directed/reliable
- Exponential backoff on failed connection attempts

---

## Phase 7: Power Management

### 7.1 Scan duty cycling

```
Active scanning:    ~15mA (nRF52), ~90mA (ESP32)
Advertising:        ~5mA peak per event
Idle:               ~0mA additional

Default strategy:
- Scan window: 50ms every 500ms (10% duty cycle)
- ~1.5mA average on nRF52, ~9mA on ESP32
- Configurable via BleMeshConfig.interval_ms
```

### 7.2 Sleep integration

- Light sleep: pause scanning, continue advertising at reduced rate
- Deep sleep: disable BLE mesh entirely
- Hook into existing power management observers

### 7.3 Role-based behavior

| Device Role | BLE Mesh Behavior                           |
| ----------- | ------------------------------------------- |
| CLIENT      | Scan on-demand, advertise when sending      |
| CLIENT_MUTE | Receive only, no rebroadcast                |
| ROUTER      | Continuous scan+advertise, max connections  |
| ROUTER_LATE | Delayed rebroadcast (same as LoRa behavior) |
| SENSOR      | Minimal: advertise telemetry only           |

---

## Phase 8: Testing & Validation

### 8.1 Unit tests

- BLE wire format encode/decode roundtrip
- PacketHistory deduplication across LoRa + BLE
- Peer table management (add, prune, limits)
- Echo prevention (don't re-send BLE-sourced packets over BLE)

### 8.2 Integration tests

- Two ESP32 nodes: BLE-only mesh (no LoRa)
- Two nRF52 nodes: BLE-only mesh
- Three nodes: multi-hop BLE relay
- Hybrid: Node A (LoRa+BLE) <-> Node B (BLE only) <-> Node C (LoRa+BLE)
- Phone API + BLE mesh coexistence
- Power consumption profiling

### 8.3 Compatibility matrix

| Test          | ESP32       | ESP32-S3 | ESP32-C3 | ESP32-C6 | nRF52840 |
| ------------- | ----------- | -------- | -------- | -------- | -------- |
| Adv mode      | GATT only\* | Yes      | Yes      | Yes      | Yes      |
| GATT mode     | Yes         | Yes      | Yes      | Yes      | Yes      |
| Phone coexist | Yes         | Yes      | Yes      | Yes      | Yes      |
| Multi-hop     | Yes         | Yes      | Yes      | Yes      | Yes      |

\*ESP32 (BLE 4.2): legacy adv limited to 31B, use GATT-only mode

---

## Implementation Order

### Milestone 1: Foundation

1. Protobuf additions (`TRANSPORT_BLE_MESH`, `BleMeshConfig`)
2. `BLEMeshHandler` base class
3. Router hook (alongside UDP handler)
4. Feature flag + main.cpp wiring

### Milestone 2: ESP32 MVP

5. `NimbleBLEMesh` — advertising mode on ESP32-S3
6. Phone BLE coexistence validation
7. Basic two-node BLE mesh test

### Milestone 3: nRF52 MVP

8. `NRF52BLEMesh` — advertising mode
9. Cross-platform interop (ESP32 <-> nRF52 BLE mesh)

### Milestone 4: GATT Mode

10. GATT service + connection management for ESP32
11. GATT service + connection management for nRF52
12. Peer discovery & table management

### Milestone 5: Polish

13. Power management integration
14. Full test matrix
15. ESP32 (BLE 4.2) GATT-only fallback
16. Admin UI / config in phone apps

---

## Risks & Mitigations

| Risk                             | Impact                    | Mitigation                                                                   |
| -------------------------------- | ------------------------- | ---------------------------------------------------------------------------- |
| BLE scan power drain             | Battery life              | Duty cycling, role-based policy                                              |
| Phone BLE drops during mesh scan | UX regression             | Separate adv sets, careful interleaving                                      |
| NimBLE/Bluefruit API differences | Platform code duplication | Thin platform subclasses, shared base logic                                  |
| No extended adv on ESP32 (4.2)   | Can't fit packets in adv  | GATT-only fallback                                                           |
| BLE range (~30-100m)             | Coverage gaps             | Hybrid LoRa+BLE; Coded PHY on nRF52                                          |
| Packet storms in dense BLE mesh  | Saturation                | Existing flood mitigation (PacketHistory, hop_limit, role-based rebroadcast) |

---

## Files to Create

| File                                  | Purpose                                       |
| ------------------------------------- | --------------------------------------------- |
| `src/mesh/BLEMeshHandler.h`           | Base handler class (like UdpMulticastHandler) |
| `src/nimble/NimbleBLEMesh.h`          | ESP32 BLE mesh header                         |
| `src/nimble/NimbleBLEMesh.cpp`        | ESP32 NimBLE implementation                   |
| `src/platform/nrf52/NRF52BLEMesh.h`   | nRF52 BLE mesh header                         |
| `src/platform/nrf52/NRF52BLEMesh.cpp` | nRF52 Bluefruit implementation                |

## Files to Modify

| File                                    | Change                                             |
| --------------------------------------- | -------------------------------------------------- |
| `src/mesh/Router.cpp`                   | Add `bleMeshHandler->onSend()` next to UDP handler |
| `src/main.h`                            | Declare `extern BLEMeshHandler *bleMeshHandler`    |
| `src/main.cpp`                          | Instantiate + start BLE mesh handler               |
| `src/configuration.h`                   | `HAS_BLE_MESH` feature flag                        |
| Protobufs (`mesh.proto`)                | `TRANSPORT_BLE_MESH` enum + `BleMeshConfig`        |
| `src/nimble/NimbleBluetooth.cpp`        | Coexistence (adv set separation)                   |
| `src/platform/nrf52/NRF52Bluetooth.cpp` | Coexistence (scanner sharing)                      |
| Platform `variant.h` / `platformio.ini` | Enable per-board                                   |
