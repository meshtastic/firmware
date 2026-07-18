# Official core audit

This branch keeps Meshtastic communication, persistence, PKI, Bluetooth
security, and protobuf behavior at official commit
d4aa7760ccffd100ddc7dacd6875e6eee5df4f11.

## Protected files

The following paths have no branch diff:

- src/mesh/NodeDB.cpp
- src/MessageStore.cpp
- src/modules/TextMessageModule.cpp
- src/modules/RoutingModule.cpp
- src/modules/AdminModule.cpp
- src/mesh/PhoneAPI.cpp
- src/mesh/CryptoEngine.cpp
- protobufs/

No node limits, warm-node storage counts, message persistence settings, packet
formats, channel rules, PKI keys, or BLE bonding/security callbacks are
overridden.

## Narrow integration points

- NRF52Bluetooth.cpp changes only the advertised local name passed to
  Bluefruit.setName(). Pairing, bonding, GATT, connect, and disconnect logic are
  unchanged.
- RadioLibInterface.h adds read-only RSSI/SNR display accessors.
- CannedMessageModule.cpp opens the local T9 editor and later returns text to
  the official canned-message send state. It does not replace packet creation,
  PKI lookup, routing, sendToMesh(), or message persistence.
- ExternalNotificationModule.cpp changes only GAT562 audio presentation.

Run this audit after every official rebase:

    git diff --name-status d4aa7760 -- src/mesh/NodeDB.cpp src/MessageStore.cpp src/modules/TextMessageModule.cpp src/modules/RoutingModule.cpp src/modules/AdminModule.cpp src/mesh/PhoneAPI.cpp src/mesh/CryptoEngine.cpp protobufs
