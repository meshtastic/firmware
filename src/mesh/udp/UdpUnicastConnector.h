#pragma once
#ifdef ARCH_PORTDUINO
// Native unicast-UDP connector for meshtasticd. Forwards every outbound MeshPacket to a single
// configured peer (the meshswitch routing daemon) and injects packets received back from it,
// routing on the cleartext header exactly like the multicast handler. This is a meshtasticd-only
// (Linux) feature, so it uses plain POSIX sockets directly rather than the portduino AsyncUDP
// library.
//
// meshtasticd is the client here: it connect()s one UDP socket to the peer, send()s outbound
// packets, and recv()s the daemon's replies on that same socket (the daemon always answers the
// source endpoint it last heard from, so no well-known listen port is required).

#include "configuration.h"
#include "main.h"
#include "mesh/Router.h"
#include "mesh/mesh-pb-constants.h"

#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define UDP_UNICAST_DEFAULT_PORT 4403 // matches the daemon's default listen port

class UdpUnicastConnector final
{
  public:
    UdpUnicastConnector() = default;
    ~UdpUnicastConnector() { stop(); }

    // peer is "host" or "host:port" (IPv4 literal). Returns true once connected.
    bool start(const std::string &peer)
    {
        if (isRunning)
            return true;

        std::string host = peer;
        uint16_t port = UDP_UNICAST_DEFAULT_PORT;
        auto colon = peer.rfind(':');
        if (colon != std::string::npos) {
            host = peer.substr(0, colon);
            port = (uint16_t)atoi(peer.substr(colon + 1).c_str());
        }

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            LOG_ERROR("UDP unicast: bad peer address '%s'", peer.c_str());
            return false;
        }

        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            LOG_ERROR("UDP unicast: socket() failed");
            return false;
        }
        // connect() pins the destination for send() and filters recv() to datagrams from the peer.
        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            LOG_ERROR("UDP unicast: connect() to %s:%u failed", host.c_str(), port);
            ::close(fd);
            fd = -1;
            return false;
        }

        isRunning = true;
        rxThread = std::thread([this]() { receiveLoop(); });
        LOG_INFO("UDP unicast connector to %s:%u started", host.c_str(), port);
        return true;
    }

    void stop()
    {
        if (!isRunning)
            return;
        isRunning = false;
        if (fd >= 0)
            ::shutdown(fd, SHUT_RDWR); // unblock the recv() in the rx thread
        if (rxThread.joinable())
            rxThread.join();
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    // Called from Router::send for every outbound packet.
    void onSend(const meshtastic_MeshPacket *mp)
    {
        if (!isRunning || fd < 0 || !mp)
            return;
        // Never reflect a packet that arrived over UDP straight back out over UDP. The daemon's
        // (from,id) dedup is the backstop if this ever slips through.
        if (mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MULTICAST_UDP)
            return;

        uint8_t buffer[meshtastic_MeshPacket_size];
        size_t len = pb_encode_to_bytes(buffer, sizeof(buffer), &meshtastic_MeshPacket_msg, mp);
        if (len == 0)
            return;
#ifdef MSG_NOSIGNAL
        constexpr int kSendFlags = MSG_NOSIGNAL;
#else
        constexpr int kSendFlags = 0;
#endif
        if (::send(fd, buffer, len, kSendFlags) < 0)
            LOG_DEBUG("UDP unicast: send() failed for packet id=%u", mp->id);
    }

  private:
    void receiveLoop()
    {
        uint8_t buf[meshtastic_MeshPacket_size];
        while (isRunning) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                if (!isRunning)
                    break;
                continue; // transient error or shutdown in progress
            }
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
            if (!pb_decode_from_bytes(buf, (size_t)n, &meshtastic_MeshPacket_msg, &mp))
                continue;
            if (mp.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
                continue;
            // Drop packets with spoofed local origin — same guard as the multicast handler.
            if (isFromUs(&mp)) {
                LOG_WARN("UDP unicast packet with spoofed local from=0x%x, dropping", mp.from);
                continue;
            }
            mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MULTICAST_UDP;
            UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
            p->rx_snr = 0;
            p->rx_rssi = 0;
            if (router)
                router->enqueueReceivedMessage(p.release());
        }
    }

    int fd = -1;
    std::atomic<bool> isRunning{false};
    std::thread rxThread;
};
#endif // ARCH_PORTDUINO
