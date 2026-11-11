// Unit tests for DeviceMetadata.ble_address: formatMacAddr() in src/meshUtils.cpp and the
// getBluetoothAddr() wiring in getDeviceMetadata() (src/main.cpp).
//
// Clients (the CLI, provisioning tools printing a connect QR code) read ble_address as the
// address phones connect to, and paste it into links like /connections?address=x<MAC>. The
// contract is "AA:BB:CC:DD:EE:FF": uppercase hex, most significant byte first, every byte two
// digits, and empty rather than guessed when the platform cannot name its BLE address.
//
// Regressions guarded:
// - a byte formatted without its leading zero ("A:B:...") or lowercase, which no longer matches
//   the address a phone scan shows;
// - the bytes emitted in reverse, the SoftDevice's own little-endian order;
// - a write past a short buffer instead of truncating with a terminating NUL;
// - native (portduino), which has no BLE address source wired in, reporting an address anyway.
#include "MeshTypes.h"
#include "TestUtil.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "meshUtils.h"
#include <cstring>
#include <unity.h>

// Declared here rather than via main.h, which pulls in the whole firmware for one prototype.
meshtastic_DeviceMetadata getDeviceMetadata();

void setUp(void) {}
void tearDown(void) {}

static void test_formatMacAddr_uppercaseMostSignificantFirst()
{
    const uint8_t mac[6] = {0xAA, 0xbb, 0xC0, 0x01, 0xde, 0xFF};
    char out[18];
    formatMacAddr(mac, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AA:BB:C0:01:DE:FF", out);
}

static void test_formatMacAddr_keepsLeadingZeros()
{
    const uint8_t mac[6] = {0x00, 0x01, 0x02, 0x0a, 0x0b, 0x00};
    char out[18];
    formatMacAddr(mac, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("00:01:02:0A:0B:00", out);
}

static void test_formatMacAddr_shortBuffer_truncatesAndTerminates()
{
    const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    char out[9];
    memset(out, 'x', sizeof(out));
    formatMacAddr(mac, out, 6);
    TEST_ASSERT_EQUAL_STRING("11:22", out);
    TEST_ASSERT_EQUAL_CHAR('x', out[6]); // nothing written past the stated size
}

static void test_bleAddressField_fitsAFormattedAddress()
{
    meshtastic_DeviceMetadata md = meshtastic_DeviceMetadata_init_zero;
    TEST_ASSERT_TRUE(sizeof(md.ble_address) >= sizeof("AA:BB:CC:DD:EE:FF"));
}

static void test_getDeviceMetadata_native_reportsNoBleAddress()
{
    meshtastic_DeviceMetadata md = getDeviceMetadata();
    TEST_ASSERT_EQUAL_STRING("", md.ble_address);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_formatMacAddr_uppercaseMostSignificantFirst);
    RUN_TEST(test_formatMacAddr_keepsLeadingZeros);
    RUN_TEST(test_formatMacAddr_shortBuffer_truncatesAndTerminates);
    RUN_TEST(test_bleAddressField_fitsAFormattedAddress);
    RUN_TEST(test_getDeviceMetadata_native_reportsNoBleAddress);
    exit(UNITY_END());
}

void loop() {}
