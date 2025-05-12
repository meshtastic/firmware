#pragma once
#if HAS_UDP_MULTICAST
#include "configuration.h"
#include "main.h"
#include "mesh/Router.h"

#include <AsyncUDP.h>
#include <WiFi.h>

#if HAS_ETHERNET && defined(USE_WS5500)
#include <ETHClass2.h>
#define ETH ETH2
#endif // HAS_ETHERNET

#define UDP_MULTICAST_DEFAUL_PORT 4403 // Default port for UDP multicast is same as TCP api server

class UdpMulticastHandler final
{
  public:
    UdpMulticastHandler() { udpIpAddress = IPAddress(224, 0, 0, 69); }

    void start()
    {
        if (udp.listenMulticast(udpIpAddress, UDP_MULTICAST_DEFAUL_PORT, 64)) {
#ifndef ARCH_PORTDUINO
            // FIXME(PORTDUINO): arduino lacks IPAddress::toString()
            LOG_DEBUG("UDP Listening on IP: %s", WiFi.localIP().toString().c_str());
#else
            LOG_DEBUG("UDP Listening");
#endif
            udp.onPacket([this](AsyncUDPPacket packet) { onReceive(packet); });
        } else {
            LOG_DEBUG("Failed to listen on UDP");
        }
    }

    void onReceive(AsyncUDPPacket packet)
    {
        size_t packetLength = packet.length();
#ifndef ARCH_PORTDUINO
        // FIXME(PORTDUINO): arduino lacks IPAddress::toString()
        LOG_DEBUG("UDP broadcast from: %s, len=%u", packet.remoteIP().toString().c_str(), packetLength);
#endif
        meshtastic_MeshPacket mp;
        LOG_DEBUG("Decoding MeshPacket from UDP len=%u", packetLength);
        bool isPacketDecoded = pb_decode_from_bytes(packet.data(), packetLength, &meshtastic_MeshPacket_msg, &mp);
        if (isPacketDecoded && router) {
            UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
            // Unset received SNR/RSSI
            p->rx_snr = 0;
            p->rx_rssi = 0;
            router->enqueueReceivedMessage(p.release());
        }
    }

    bool onSend(const meshtastic_MeshPacket *mp)
    {
        if (!mp || !udp) {
            return false;
        }
#ifndef ARCH_PORTDUINO
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }
#endif
        LOG_DEBUG("Broadcasting packet over UDP (id=%u)", mp->id);
        uint8_t buffer[meshtastic_MeshPacket_size];
        size_t encodedLength = pb_encode_to_bytes(buffer, sizeof(buffer), &meshtastic_MeshPacket_msg, mp);
        udp.writeTo(buffer, encodedLength, udpIpAddress, UDP_MULTICAST_DEFAUL_PORT);
        return true;
    }

  private:
    IPAddress udpIpAddress;
    AsyncUDP udp;
};
#endif // HAS_UDP_MULTICAST