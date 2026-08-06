#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Pure policy for the USB-Ethernet (CDC-NCM) gadget: the IP plan, the DHCP
 * offer contents, and the MAC derivation.
 *
 * Deliberately free of Arduino and ESP-IDF dependencies so it can be covered by
 * the native test suite. Nothing here talks to hardware.
 */
namespace usbnet
{

/// An IPv4 address as four octets, most significant first.
struct Ipv4 {
    uint8_t o[4];
};

constexpr Ipv4 makeIpv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return Ipv4{{a, b, c, d}};
}

/// The node's own address on the USB link. Fixed and documented so a user only
/// ever types it into the phone app once.
///
/// 192.168.7.0/24 is chosen to avoid the 192.168.4.0/24 the ESP32 softAP uses by
/// default, so a node running both does not end up with two identical subnets.
constexpr Ipv4 NODE_IP = makeIpv4(192, 168, 7, 1);
constexpr Ipv4 NETMASK = makeIpv4(255, 255, 255, 0);

/// DHCP pool handed to the attached host. One host per cable, so it is tiny.
constexpr Ipv4 POOL_FIRST = makeIpv4(192, 168, 7, 2);
constexpr uint8_t POOL_SIZE = 8;

constexpr uint32_t LEASE_SECONDS = 2 * 60 * 60;

/**
 * What the DHCP server is allowed to put in an offer.
 *
 * Both flags are false, and that is the whole point rather than an oversight.
 * Apple's guidance for an accessory DHCP server (Quinn, Apple DTS, developer
 * forums thread 779796) is that it must not vend a gateway, so the interface
 * cannot become the host's default route. Omitting option 3 (Router) and
 * option 6 (DNS) means iOS never elects the USB link for internet, never runs
 * its captive-network probe against it, and so never marks it dead - which is
 * what lets the link keep working indefinitely, and in airplane mode.
 *
 * This is the one place warthog's design must NOT be copied: it is a NAT
 * gateway and vends both, which is correct for a gateway and wrong for us.
 */
struct DhcpOfferPolicy {
    bool vendRouter;
    bool vendDns;
};

constexpr DhcpOfferPolicy DHCP_OFFER_POLICY = {false, false};

/// Gateway advertised in the netif config. Must stay 0.0.0.0 - see above.
constexpr Ipv4 GATEWAY = makeIpv4(0, 0, 0, 0);

/**
 * Derive the gadget's Ethernet MAC from a board-unique base address.
 *
 * Sets the locally-administered bit and clears the multicast bit, so the result
 * is always a valid unicast LAA regardless of what the caller passes in.
 * Deterministic: the same base always yields the same MAC, so the host does not
 * see a new adapter on every boot.
 */
void deriveUsbMac(const uint8_t base[6], uint8_t out[6]);

/**
 * Format a MAC as the 12 bare uppercase hex characters the CDC network class
 * requires for its iMACAddress string descriptor. Separators are not allowed
 * there. `out` must hold at least 13 bytes; the result is NUL terminated.
 */
void formatMacForNcm(const uint8_t mac[6], char out[13]);

/// True when `addr` falls inside the DHCP pool.
bool isInDhcpPool(const Ipv4 &addr);

} // namespace usbnet
