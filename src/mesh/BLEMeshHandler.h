#pragma once

#if HAS_BLE_MESH

#include "MeshTypes.h"
#include "Router.h"
#include "mesh-pb-constants.h"

// Meshtastic BLE mesh manufacturer data identifier
// Using 0xFFFF (reserved for testing) until a real BLE SIG company ID is assigned
#define BLE_MESH_COMPANY_ID 0xFFFF
#define BLE_MESH_PROTOCOL_VERSION 1

// TODO: Add TRANSPORT_BLE_MESH = 8 to mesh.proto TransportMechanism enum and regenerate.
// Until then, define it here so the code compiles.
#ifndef meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_MESH
#define meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_MESH ((meshtastic_MeshPacket_TransportMechanism)8)
#endif

class BLEMeshHandler
{
  public:
    BLEMeshHandler() : isRunning(false) {}
    virtual ~BLEMeshHandler() {}

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void onBluetoothReady() {}

    // Called from Router::send() to broadcast packet over BLE mesh
    virtual bool onSend(const meshtastic_MeshPacket *mp) = 0;

  protected:
    bool isRunning;

    // Decode received BLE mesh data and enqueue into router
    void deliverToRouter(const uint8_t *data, size_t len)
    {
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        bool decoded = pb_decode_from_bytes(data, len, &meshtastic_MeshPacket_msg, &mp);
        if (!decoded || mp.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
            return;

        mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_MESH;
        mp.pki_encrypted = false;
        mp.public_key.size = 0;
        memset(mp.public_key.bytes, 0, sizeof(mp.public_key.bytes));
        mp.rx_snr = 0;
        mp.rx_rssi = 0;

        UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
        if (p && router)
            router->enqueueReceivedMessage(p.release());
    }

    // Encode a mesh packet for BLE transmission, returns encoded length
    size_t encodeForBLE(const meshtastic_MeshPacket *mp, uint8_t *buf, size_t bufLen)
    {
        return pb_encode_to_bytes(buf, bufLen, &meshtastic_MeshPacket_msg, mp);
    }
};

#endif // HAS_BLE_MESH
