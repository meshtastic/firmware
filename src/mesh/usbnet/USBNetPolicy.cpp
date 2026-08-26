#include "USBNetPolicy.h"

namespace usbnet
{

void deriveUsbMac(const uint8_t base[6], uint8_t out[6])
{
    for (size_t i = 0; i < 6; i++)
        out[i] = base[i];

    out[0] &= 0xFE; // clear the multicast bit - a source MAC must be unicast
    out[0] |= 0x02; // set locally administered, we are not using an OUI we own
}

void formatMacForNcm(const uint8_t mac[6], char out[13])
{
    static const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < 6; i++) {
        out[i * 2] = hex[(mac[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[mac[i] & 0x0F];
    }
    out[12] = '\0';
}

bool isInDhcpPool(const Ipv4 &addr)
{
    for (size_t i = 0; i < 3; i++) {
        if (addr.o[i] != POOL_FIRST.o[i])
            return false;
    }

    const uint16_t first = POOL_FIRST.o[3];
    return addr.o[3] >= first && addr.o[3] < first + POOL_SIZE;
}

} // namespace usbnet
