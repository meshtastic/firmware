#include "Channels.h"
#include "GeoCoord.h"
#include "NodeDB.h"
#include "PositionPrecision.h"
#include "Router.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cstdint>
#include <cstring>
#include <unity.h>
#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

static meshtastic_Position makePosition()
{
    meshtastic_Position position = meshtastic_Position_init_default;
    position.has_latitude_i = true;
    position.latitude_i = static_cast<int32_t>(0x12345678);
    position.has_longitude_i = true;
    position.longitude_i = static_cast<int32_t>(0x22345678);
    position.has_altitude = true;
    position.altitude = 123;
    position.time = 42;
    position.location_source = meshtastic_Position_LocSource_LOC_EXTERNAL;
    position.timestamp = 43;
    position.sats_in_view = 10;
    return position;
}

static meshtastic_Channel makeChannel(meshtastic_Channel_Role role, bool hasModuleSettings, uint32_t positionPrecision)
{
    meshtastic_Channel channel = meshtastic_Channel_init_default;
    channel.has_settings = true;
    channel.role = role;
    channel.settings.has_module_settings = hasModuleSettings;
    channel.settings.module_settings.position_precision = positionPrecision;
    return channel;
}

static void test_applyPositionPrecision_clampsLatLonAndSetsPrecisionBits()
{
    meshtastic_Position position = makePosition();

    applyPositionPrecision(position, 16);

    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x12348000), position.latitude_i);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x22348000), position.longitude_i);
    TEST_ASSERT_EQUAL_UINT32(16, position.precision_bits);
    TEST_ASSERT_TRUE(position.has_latitude_i);
    TEST_ASSERT_TRUE(position.has_longitude_i);
}

static void test_applyPositionPrecision_fullPrecisionKeepsLatLon()
{
    meshtastic_Position position = makePosition();

    applyPositionPrecision(position, 32);

    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x12345678), position.latitude_i);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x22345678), position.longitude_i);
    TEST_ASSERT_EQUAL_UINT32(32, position.precision_bits);
}

static void test_applyPositionPrecision_zeroScrubsLocationButKeepsTime()
{
    meshtastic_Position position = makePosition();

    applyPositionPrecision(position, 0);

    TEST_ASSERT_FALSE(position.has_latitude_i);
    TEST_ASSERT_EQUAL_INT32(0, position.latitude_i);
    TEST_ASSERT_FALSE(position.has_longitude_i);
    TEST_ASSERT_EQUAL_INT32(0, position.longitude_i);
    TEST_ASSERT_FALSE(position.has_altitude);
    TEST_ASSERT_EQUAL_INT32(0, position.altitude);
    TEST_ASSERT_EQUAL_UINT32(42, position.time);
    TEST_ASSERT_EQUAL_UINT32(0, position.timestamp);
    TEST_ASSERT_EQUAL_UINT32(0, position.sats_in_view);
    TEST_ASSERT_EQUAL_UINT32(0, position.precision_bits);
}

static void test_applyPositionPrecision_reencodesPositionPacket()
{
    meshtastic_Position position = makePosition();
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = meshtastic_PortNum_POSITION_APP;
    packet.decoded.payload.size = pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes),
                                                     &meshtastic_Position_msg, &position);

    TEST_ASSERT_TRUE(applyPositionPrecision(packet, 16));

    meshtastic_Position decoded = meshtastic_Position_init_default;
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_Position_msg, &decoded));
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x12348000), decoded.latitude_i);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x22348000), decoded.longitude_i);
    TEST_ASSERT_EQUAL_UINT32(16, decoded.precision_bits);
}

static void test_getPositionPrecisionForChannel_explicitPrecisionIsHonored()
{
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 16);

    TEST_ASSERT_EQUAL_UINT32(16, getPositionPrecisionForChannel(channel));
}

static void test_getPositionPrecisionForChannel_explicitZeroDisablesPrimary()
{
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 0);

    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(channel));
}

static void test_getPositionPrecisionForChannel_primaryWithoutModuleSettingsFailsClosed()
{
    // Regression guard for #10509: precision 32 below must be ignored (no module settings).
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, false, 32);

    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(channel));
}

static void test_getPositionPrecisionForChannel_secondaryWithoutModuleSettingsFailsClosed()
{
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_SECONDARY, false, 32);

    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(channel));
}

// End-to-end via the channelIndex overload + live channels singleton, exercising getKey()'s 1-byte->16-byte expansion.
static void test_getPositionPrecisionForChannel_clampsPreciseOnDefaultKeyChannel()
{
    channels.initDefaults(); // channel 0: primary, default key (psk {0x01}) -> publicly decryptable
    uint8_t idx = 0;
    meshtastic_Channel &ch = channels.getByIndex(idx);
    ch.settings.psk.size = 1;
    ch.settings.psk.bytes[0] = 0x01;
    ch.settings.has_module_settings = true;
    ch.settings.module_settings.position_precision = 32; // user requests "Precise" on a public channel

    TEST_ASSERT_EQUAL_UINT32(MAX_POSITION_PRECISION_PUBLIC_KEY, getPositionPrecisionForChannel(idx));
}

static void test_getPositionPrecisionForChannel_keepsPreciseOnStrongKeyChannel()
{
    channels.initDefaults();
    uint8_t idx = 0;
    meshtastic_Channel &ch = channels.getByIndex(idx);
    memset(ch.settings.psk.bytes, 0xAB, 16); // a private 128-bit key, not the defaultpsk family
    ch.settings.psk.size = 16;
    ch.settings.has_module_settings = true;
    ch.settings.module_settings.position_precision = 32;

    TEST_ASSERT_EQUAL_UINT32(32, getPositionPrecisionForChannel(idx));
}

static CryptoKey makeCryptoKey(const uint8_t *bytes, int length)
{
    CryptoKey k;
    memset(k.bytes, 0, sizeof(k.bytes));

    // CryptoKey::length is int8_t and CryptoKey::bytes is 32 bytes; keep the helper consistent and overflow-safe.
    int cappedLen = length;
    if (cappedLen < 0)
        cappedLen = -1;
    else if (cappedLen > static_cast<int>(sizeof(k.bytes)))
        cappedLen = static_cast<int>(sizeof(k.bytes));

    if (cappedLen > 0 && bytes != nullptr) {
        memcpy(k.bytes, bytes, static_cast<size_t>(cappedLen));
    }

    k.length = static_cast<int8_t>(cappedLen);
    return k;
}

static void test_cryptoKeyIsPublic_openKeyIsPublic()
{
    // length 0 == encryption disabled.
    TEST_ASSERT_TRUE(cryptoKeyIsPublic(makeCryptoKey(nullptr, 0)));
}

static void test_cryptoKeyIsPublic_defaultKeyIsPublic()
{
    // The expanded default PSK (the 16-byte defaultpsk) -- the case a key-length check misses.
    TEST_ASSERT_TRUE(cryptoKeyIsPublic(makeCryptoKey(defaultpsk, sizeof(defaultpsk))));
}

static void test_cryptoKeyIsPublic_defaultKeyFamilyVariesLastByte()
{
    // Higher indices (e.g. {0x02}) expand to defaultpsk with only the last byte bumped -- still public.
    uint8_t key[sizeof(defaultpsk)];
    memcpy(key, defaultpsk, sizeof(defaultpsk));
    key[sizeof(defaultpsk) - 1] = static_cast<uint8_t>(key[sizeof(defaultpsk) - 1] + 1);
    TEST_ASSERT_TRUE(cryptoKeyIsPublic(makeCryptoKey(key, sizeof(key))));
}

static void test_cryptoKeyIsPublic_strongKeyIsPrivate()
{
    uint8_t key[16];
    memset(key, 0xAB, sizeof(key)); // not the defaultpsk family
    TEST_ASSERT_FALSE(cryptoKeyIsPublic(makeCryptoKey(key, sizeof(key))));
}

static void test_cryptoKeyIsPublic_aes256KeyIsPrivate()
{
    uint8_t key[32];
    memset(key, 0x11, sizeof(key));
    TEST_ASSERT_FALSE(cryptoKeyIsPublic(makeCryptoKey(key, sizeof(key))));
}

static void test_cryptoKeyIsPublic_invalidKeyIsNotPublic()
{
    // length < 0 == no/invalid key (e.g. a disabled channel); it carries no traffic to leak.
    TEST_ASSERT_FALSE(cryptoKeyIsPublic(makeCryptoKey(nullptr, -1)));
}

// Regression for out-of-bounds indexing in GeoCoord's UTM/MGRS conversion on extreme
// latitude_i/longitude_i that arrive in a received Position (raw int32, unvalidated on decode).
// Pre-fix, latitude_i = INT32_MAX made latLongToUTM read latBands[36] on a 21-char string
// (stack-buffer-overflow at GeoCoord.cpp:128, an AddressSanitizer abort); extreme longitude produced
// a negative UTM zone feeding the MGRS letter tables. The fix clamps the zone/band/col/row indices.
// This exercises the fix under the coverage env's ASan. Each representation is computed lazily,
// so its getter must be called explicitly here to exercise its conversion path.
static void test_geocoord_extreme_coords_no_oob()
{
    const int32_t vals[] = {INT32_MIN,  INT32_MAX,   INT32_MIN + 1, INT32_MAX - 1, 0, 1, -1, 900000000, -900000000, // +/-90 deg
                            1800000000, -1800000000,                                                                // +/-180 deg
                            2000000000, -2000000000, 123456789,     -123456789};
    const size_t n = sizeof(vals) / sizeof(vals[0]);
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            GeoCoord g(vals[i], vals[j], 0); // ctor -> setCoords() -> DMS only
            // Force UTM/MGRS/OSGR/OLC computation (each lazy on first getter access).
            g.getUTMZone();
            g.getMGRSZone();
            g.getOSGRE100k();
            char olcCode[OLC_CODE_LEN + 1];
            g.getOLCCode(olcCode);
            // Surviving every extreme pair (no ASan fault) means the index clamps hold.
            TEST_ASSERT_EQUAL_INT32(vals[i], g.getLatitude());
        }
}

static void configureEventChannels(bool eventAtIndexOne, bool inheritEventKeyOnSecondary)
{
    memset(&channelFile, 0, sizeof(channelFile));
    channelFile.channels_count = 2;

    meshtastic_Channel eventChannel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 16);
    meshtastic_Channel otherChannel = makeChannel(meshtastic_Channel_Role_SECONDARY, true, 16);
    strcpy(eventChannel.settings.name, "everyone");
#ifdef USERPREFS_CHANNEL_0_PSK
    static const uint8_t configuredEventPsk[] = USERPREFS_CHANNEL_0_PSK;
    eventChannel.settings.psk.size = sizeof(configuredEventPsk);
    memcpy(eventChannel.settings.psk.bytes, configuredEventPsk, sizeof(configuredEventPsk));
#endif
    if (!inheritEventKeyOnSecondary) {
        otherChannel.settings.psk.size = 32;
        memset(otherChannel.settings.psk.bytes, 0xAB, 32);
        strcpy(otherChannel.settings.name, "private");
    }

    eventChannel.index = eventAtIndexOne ? 1 : 0;
    otherChannel.index = eventAtIndexOne ? 0 : 1;
    channelFile.channels[eventChannel.index] = eventChannel;
    channelFile.channels[otherChannel.index] = otherChannel;
    channels.onConfigChanged();
}

static void test_getPositionPrecisionForChannel_eventChannelClampedToZero()
{
    // The event ("everyone") channel must never share location, even when the
    // stored precision is non-zero. Under the block gate the clamp forces 0;
    // otherwise the stored value is honored like any other channel.
#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
    configureEventChannels(false, false);
    TEST_ASSERT_TRUE(channels.isEventChannel(0));
    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(0));
#else
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 16);
    TEST_ASSERT_EQUAL_UINT32(16, getPositionPrecisionForChannel(channel));
#endif
}

static void test_eventChannelIdentity_usesEffectiveKeyAndSurvivesReorder()
{
#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
    configureEventChannels(false, true);
    TEST_ASSERT_TRUE(channels.isEventChannel(0));
    TEST_ASSERT_TRUE(channels.isEventChannel(1));
    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(1));

    configureEventChannels(true, false);
    TEST_ASSERT_FALSE(channels.isEventChannel(0));
    TEST_ASSERT_TRUE(channels.isEventChannel(1));
    TEST_ASSERT_EQUAL_UINT32(16, getPositionPrecisionForChannel(0));
    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(1));
#else
    TEST_ASSERT_FALSE(channels.isEventChannel(0));
#endif
}

static meshtastic_MeshPacket makeDecodedPacket(meshtastic_PortNum portnum, uint8_t channelIndex)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = portnum;
    packet.channel = channelIndex;
    return packet;
}

static void test_eventCoordinatePolicy_coversPortsAndExcludesPki()
{
#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
    configureEventChannels(false, false);
    auto position = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, 0);
    auto waypoint = makeDecodedPacket(meshtastic_PortNum_WAYPOINT_APP, 0);
    auto mapReport = makeDecodedPacket(meshtastic_PortNum_MAP_REPORT_APP, 0);
    auto text = makeDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, 0);

    TEST_ASSERT_TRUE(isBlockedEventCoordinatePacket(&position));
    TEST_ASSERT_TRUE(isBlockedEventCoordinatePacket(&waypoint));
    TEST_ASSERT_TRUE(isBlockedEventCoordinatePacket(&mapReport));
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&text));

    waypoint.pki_encrypted = true;
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&waypoint));

    waypoint.pki_encrypted = false;
    waypoint.to = 0x12345678;
    config.security.private_key.size = 32;
    owner.is_licensed = false;
#if ARCH_PORTDUINO
    portduino_config.force_simradio = false;
#endif
    TEST_ASSERT_TRUE(willUsePki(&waypoint));
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&waypoint));
#else
    auto position = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, 0);
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&position));
#endif
}

static void test_eventCoordinatePolicy_doesNotClassifyOpaquePacketsByHash()
{
#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
    configureEventChannels(false, false);
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.channel = channels.getHash(0);
    packet.from = 0;
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&packet));

    packet.pki_encrypted = true;
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&packet));

    packet.channel = 0;
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&packet));

    packet.pki_encrypted = false;
    packet.channel = channels.getHash(0);
    packet.from = 0x12345678;
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&packet));

    packet.from = 0;
    packet.channel = channels.getHash(1);
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&packet));
#else
    TEST_ASSERT_TRUE(true);
#endif
}

static void test_eventCoordinatePolicy_usesResolvedUnicastChannel()
{
#if USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL && defined(USERPREFS_CHANNEL_0_PSK)
    NodeDB *savedNodeDB = nodeDB;
    nodeDB = new NodeDB();
    configureEventChannels(false, false);
    meshtastic_NodeInfoLite *node =
        nodeDB->getNumMeshNodes() > 1 ? nodeDB->getMeshNodeByIndex(1) : nodeDB->getOrCreateMeshNode(0x12345678);
    TEST_ASSERT_NOT_NULL(node);
    const NodeNum destination = node->num;
    const uint8_t savedChannel = node->channel;

    auto position = makeDecodedPacket(meshtastic_PortNum_POSITION_APP, 0);
    position.to = destination;
    node->channel = 1;
    TEST_ASSERT_FALSE(isBlockedEventCoordinatePacket(&position));

    position.from = 0x87654321;
    TEST_ASSERT_TRUE(isBlockedEventCoordinatePacket(&position));

    position.from = 0;
    node->channel = 0;
    TEST_ASSERT_TRUE(isBlockedEventCoordinatePacket(&position));
    node->channel = savedChannel;
    delete nodeDB;
    nodeDB = savedNodeDB;
#else
    TEST_ASSERT_TRUE(true);
#endif
}

static void test_getPositionPrecisionForChannel_nonEventFullKeyIsHonored()
{
    // A private channel with a full 32-byte key that is not the configured
    // channel-0 PSK must be
    // unaffected by the clamp on either side of the gate.
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 16);
    channel.settings.psk.size = 32;
    memset(channel.settings.psk.bytes, 0xAB, 32);

    TEST_ASSERT_EQUAL_UINT32(16, getPositionPrecisionForChannel(channel));
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_applyPositionPrecision_clampsLatLonAndSetsPrecisionBits);
    RUN_TEST(test_applyPositionPrecision_fullPrecisionKeepsLatLon);
    RUN_TEST(test_applyPositionPrecision_zeroScrubsLocationButKeepsTime);
    RUN_TEST(test_applyPositionPrecision_reencodesPositionPacket);
    RUN_TEST(test_getPositionPrecisionForChannel_explicitPrecisionIsHonored);
    RUN_TEST(test_getPositionPrecisionForChannel_explicitZeroDisablesPrimary);
    RUN_TEST(test_getPositionPrecisionForChannel_primaryWithoutModuleSettingsFailsClosed);
    RUN_TEST(test_getPositionPrecisionForChannel_secondaryWithoutModuleSettingsFailsClosed);
    RUN_TEST(test_getPositionPrecisionForChannel_clampsPreciseOnDefaultKeyChannel);
    RUN_TEST(test_getPositionPrecisionForChannel_keepsPreciseOnStrongKeyChannel);
    RUN_TEST(test_cryptoKeyIsPublic_openKeyIsPublic);
    RUN_TEST(test_cryptoKeyIsPublic_defaultKeyIsPublic);
    RUN_TEST(test_cryptoKeyIsPublic_defaultKeyFamilyVariesLastByte);
    RUN_TEST(test_cryptoKeyIsPublic_strongKeyIsPrivate);
    RUN_TEST(test_cryptoKeyIsPublic_aes256KeyIsPrivate);
    RUN_TEST(test_cryptoKeyIsPublic_invalidKeyIsNotPublic);
    RUN_TEST(test_geocoord_extreme_coords_no_oob);
    RUN_TEST(test_getPositionPrecisionForChannel_eventChannelClampedToZero);
    RUN_TEST(test_eventChannelIdentity_usesEffectiveKeyAndSurvivesReorder);
    RUN_TEST(test_eventCoordinatePolicy_coversPortsAndExcludesPki);
    RUN_TEST(test_eventCoordinatePolicy_doesNotClassifyOpaquePacketsByHash);
    RUN_TEST(test_eventCoordinatePolicy_usesResolvedUnicastChannel);
    RUN_TEST(test_getPositionPrecisionForChannel_nonEventFullKeyIsHonored);
    exit(UNITY_END());
}

void loop() {}
}
