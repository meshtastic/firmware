#include "PersistedRandomDeviceId.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include "configuration.h"
#include "mesh/HardwareRNG.h"
#include <string.h>

#if USERPREFS_RANDOM_DEVICE_ID && defined(FSCom)

static const char *idFileName = "/prefs/deviceid.dat";
static uint8_t cachedMac[6];
static bool haveMac = false;

// Locally administered, unicast; the two high bits are also set so the same
// bytes are usable verbatim as a BLE random static address.
static void fixupMacBits(uint8_t *mac)
{
    mac[0] |= 0xc2;
    mac[0] &= 0xfe;
}

static bool loadMac(uint8_t *mac)
{
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(idFileName, FILE_O_READ);
    if (!f)
        return false;
    bool ok = f.read(mac, sizeof(cachedMac)) == (int)sizeof(cachedMac);
    f.close();
    // Reject anything that generateMac() could not have produced
    return ok && (mac[0] & 0xc3) == 0xc2;
}

static bool saveMac(const uint8_t *mac)
{
    concurrency::LockGuard g(spiLock);
    FSCom.mkdir("/prefs");
    FSCom.remove(idFileName);
    auto f = FSCom.open(idFileName, FILE_O_WRITE);
    if (!f)
        return false;
    bool ok = f.write(mac, sizeof(cachedMac)) == sizeof(cachedMac);
    f.close();
    return ok;
}

static bool generateMac(uint8_t *mac)
{
    if (!HardwareRNG::fill(mac, sizeof(cachedMac)))
        return false;
    fixupMacBits(mac);
    return true;
}

bool persistedRandomDeviceIdGet(uint8_t out[6])
{
    if (!haveMac) {
        if (loadMac(cachedMac)) {
            haveMac = true;
        } else if (generateMac(cachedMac)) {
            if (!saveMac(cachedMac))
                LOG_WARN("Could not persist random MAC; it will change next boot");
            haveMac = true;
        } else {
            LOG_ERROR("No entropy for random MAC, falling back to hardware address");
            return false;
        }
    }
    memcpy(out, cachedMac, sizeof(cachedMac));
    return true;
}

void persistedRandomDeviceIdRegenerate()
{
    uint8_t mac[6];
    if (!generateMac(mac))
        return;
    if (!saveMac(mac))
        LOG_WARN("Could not persist random MAC; it will change next boot");
    memcpy(cachedMac, mac, sizeof(cachedMac));
    haveMac = true;
}

#else

bool persistedRandomDeviceIdGet(uint8_t out[6])
{
    (void)out;
    return false;
}

void persistedRandomDeviceIdRegenerate() {}

#endif
