# Official core audit

## Locked official base

- Repository: `meshtastic/firmware`
- Commit: `d4aa7760ccffd100ddc7dacd6875e6eee5df4f11`
- Custom release branch: `gat-iot`

The GAT562 branch keeps the following communication and security paths byte-for-byte
identical to the locked official base:

- `src/MessageStore.cpp`
- `src/modules/TextMessageModule.cpp`
- `src/modules/RoutingModule.cpp`
- `src/modules/AdminModule.cpp`
- `src/mesh/PhoneAPI.cpp`
- `src/mesh/CryptoEngine.cpp`
- `src/mesh/Router.cpp`
- `src/mesh/ReliableRouter.cpp`
- `src/mesh/FloodingRouter.cpp`
- `src/mesh/NextHopRouter.cpp`
- `src/mesh/MeshService.cpp`
- `src/mesh/Channels.cpp`
- `src/security/`
- `protobufs/`

## Explicit GAT562 exceptions

These files intentionally differ and must be reviewed after every official rebase:

- `src/mesh/NodeDB.cpp`: default GAT562 owner identity, fixed GPS pins, and the
  one-time onboard telemetry migration. It must not alter keys, favorites, node
  removal, Bluetooth bonds, or factory-reset behavior.
- `src/modules/PositionModule.cpp` and `src/mesh/PositionPrecision.cpp`: an
  authoritative time-only `POSITION_APP` broadcast every 30 minutes and preservation
  of its trusted source marker. Packet schema, encryption, routing, and PKI remain
  official.
- `src/mesh/RadioInterface.cpp`: CN and EU433 maximum power are set to 22 dBm.
- `src/platform/nrf52/NRF52Bluetooth.cpp`: the fixed `GAT562_XXXX` advertising name
  and full-screen PIN presentation. Pairing, bonding, GATT, security callbacks,
  connect, and disconnect behavior remain official.
- `src/modules/CannedMessageModule.cpp`: local preset editing and a non-blocking
  post-send confirmation tone. Official packet construction and send path remain in use.
- `src/modules/ExternalNotificationModule.cpp`: GAT562 buzzer and WS2812 presentation.
- `src/mesh/RadioLibInterface.h`: read-only RSSI and SNR display accessors.

## Rebase guard

Run before every release and compare the output with this allowlist:

```sh
git diff --name-status d4aa7760..HEAD -- \
  src/MessageStore.cpp \
  src/modules/TextMessageModule.cpp \
  src/modules/RoutingModule.cpp \
  src/modules/AdminModule.cpp \
  src/mesh/PhoneAPI.cpp \
  src/mesh/CryptoEngine.cpp \
  src/mesh/Router.cpp \
  src/mesh/ReliableRouter.cpp \
  src/mesh/FloodingRouter.cpp \
  src/mesh/NextHopRouter.cpp \
  src/mesh/MeshService.cpp \
  src/mesh/Channels.cpp \
  src/security protobufs
```

The command must produce no output. Any output blocks firmware publication until it
has been separately reviewed and tested.
