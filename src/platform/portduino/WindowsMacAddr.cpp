#if defined(ARCH_PORTDUINO) && defined(_WIN32)

// Host-MAC lookup for getMacAddr(), replacing BlueZ on Linux and en0 on macOS.
// Isolated TU: <iphlpapi.h> needs the header trims the env sets, and undoes them here.
#undef WIN32_LEAN_AND_MEAN
#undef NOUSER
#undef NOGDI

// Order is load-bearing, hence the blank lines: winsock2.h must precede
// windows.h, which would otherwise pull in the colliding winsock v1 header.
#include <winsock2.h>

#include <windows.h>

#include <iphlpapi.h>

#include <cstring>
#include <memory>
#include <stdint.h>

// Fill dmac with the first up, non-loopback adapter's physical address, else
// return false. Adapter order is stable across reboots, so the identity persists.
bool portduinoWindowsPrimaryMac(uint8_t *dmac)
{
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;

    ULONG bufLen = 15000; // starting size recommended by the API docs
    std::unique_ptr<char[]> buf(new char[bufLen]);
    auto *addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.get());

    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        // bufLen now holds the required size; retry once.
        buf.reset(new char[bufLen]);
        addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.get());
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &bufLen);
    }
    if (ret != NO_ERROR)
        return false;

    for (auto *a = addrs; a != nullptr; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        if (a->OperStatus != IfOperStatusUp)
            continue;
        if (a->PhysicalAddressLength != 6)
            continue;
        std::memcpy(dmac, a->PhysicalAddress, 6);
        return true;
    }
    return false;
}

#endif // ARCH_PORTDUINO && _WIN32
