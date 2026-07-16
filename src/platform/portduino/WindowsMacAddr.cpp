#if defined(ARCH_PORTDUINO) && defined(_WIN32)

// Host-MAC lookup for PortduinoGlue's getMacAddr(), standing in for the BlueZ
// HCI path on Linux and the en0/getifaddrs path on macOS.
//
// Its own translation unit on purpose: <iphlpapi.h> pulls in the RPC/OLE chain,
// whose `boolean` and MSG types collide with the Arduino API, which is why the
// native-windows env builds with -DNOUSER -DWIN32_LEAN_AND_MEAN -DNOGDI. Those
// trims in turn break <iphlpapi.h> itself (oleidl.h needs LPMSG). Keeping this
// file free of Arduino headers lets us undo them locally.
#undef WIN32_LEAN_AND_MEAN
#undef NOUSER
#undef NOGDI

// Order is load-bearing, hence the blank lines: winsock2.h must come first, or
// windows.h pulls in the winsock v1 header and the two collide. iphlpapi.h
// needs both.
#include <winsock2.h>

#include <windows.h>

#include <iphlpapi.h>

#include <cstring>
#include <memory>
#include <stdint.h>

// Fill dmac with the first up, non-loopback adapter's 6-byte physical address.
// Returns false and leaves dmac untouched if none is found.
//
// GetAdaptersAddresses() replaces the deprecated GetAdaptersInfo(). Its ordering
// is driver-determined but stable across reboots, so this picks the same adapter
// each run and the node keeps a stable identity.
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
