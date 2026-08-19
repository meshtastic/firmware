// Channel key derivation and hash layer: getKey() PSK expansion, generateHash() golden values,
// onConfigChanged() primary restore, setChannel() demotion, and perhapsDecode()'s hash fall-through.

#include "Channels.h"
#include "CryptoEngine.h"
#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, isBroadcast, etc.)
#include "NodeDB.h"
#include "Router.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cstdio> // printf() group separators
#include <cstring>
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define CK_TEST_ENTRY extern "C"
#else
#define CK_TEST_ENTRY
#endif

// --- Test output helpers ---
#define MSG_BUF_LEN 200
#define TEST_MSG_FMT(fmt, ...)                                                                                                   \
    do {                                                                                                                         \
        char _buf[MSG_BUF_LEN];                                                                                                  \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                          \
        TEST_MESSAGE(_buf);                                                                                                      \
    } while (0)

// --- Reference hash implementation ---
// Independent re-statement of the algorithm in Channels.cpp (xorHash of the channel name,
// XORed with xorHash of the *expanded* key bytes), used to derive expected values from
// first principles. The golden constants below were computed by hand from this same rule.
static uint8_t refXorHash(const uint8_t *p, size_t len)
{
    uint8_t code = 0;
    for (size_t i = 0; i < len; i++)
        code ^= p[i];
    return code;
}

static uint8_t refHash(const char *name, const uint8_t *keyBytes, size_t keyLen)
{
    return refXorHash((const uint8_t *)name, strlen(name)) ^ refXorHash(keyBytes, keyLen);
}

// Golden values, derived by hand from the algorithm above (pinned so a helper bug cannot
// silently re-derive a wrong expectation):
//   xorHash("LongFast") = 'L'^'o'^'n'^'g'^'F'^'a'^'s'^'t' = 0x0A
//   xorHash(defaultpsk) = d4^f1^bb^3a^20^29^07^59^f0^bc^ff^ab^cf^4e^69^01 = 0x02
//   hash(default LongFast channel) = 0x0A ^ 0x02 = 0x08
static const int16_t GOLDEN_LONGFAST_HASH = 0x08;
static const uint8_t GOLDEN_LONGFAST_NAME_XOR = 0x0A;
static const uint8_t GOLDEN_DEFAULTPSK_XOR = 0x02;

// --- Fixture helpers ---

// A 16-byte-of-0xEE sentinel armed before each test so "crypto key unchanged" is a real
// assertion instead of an accident of whatever the previous test left behind.
static const uint8_t kSentinelByte = 0xEE;

static void armCryptoSentinel()
{
    CryptoKey s;
    memset(s.bytes, kSentinelByte, sizeof(s.bytes));
    s.length = 16;
    crypto->setKey(s);
}

static bool cryptoKeyIsSentinel()
{
    if (crypto->key.length != 16)
        return false;
    for (int i = 0; i < 16; i++)
        if (crypto->key.bytes[i] != kSentinelByte)
            return false;
    return true;
}

static void expectCryptoKey(const uint8_t *expected, int len)
{
    TEST_ASSERT_EQUAL_INT(len, crypto->key.length);
    if (len > 0)
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, crypto->key.bytes, (uint32_t)len);
}

// Write a slot directly and re-run fixupChannel() so the hash cache tracks the edit,
// mirroring how the admin/config paths mutate channelFile.
static meshtastic_Channel &setSlot(uint8_t idx, meshtastic_Channel_Role role, const char *name, const uint8_t *psk, size_t pskLen)
{
    meshtastic_Channel &ch = channels.getByIndex(idx);
    ch.index = idx;
    ch.has_settings = true;
    ch.role = role;
    memset(&ch.settings, 0, sizeof(ch.settings));
    if (name)
        strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);
    if (psk && pskLen)
        memcpy(ch.settings.psk.bytes, psk, pskLen);
    ch.settings.psk.size = (pb_size_t)pskLen;
    channels.fixupChannel(idx);
    return ch;
}

// Slot 0 as the canonical stock channel (1-byte PSK index 1, empty name -> preset name),
// independent of any USERPREFS_CHANNEL_0_* a build variant may bake into initDefaults().
static void forceCanonicalDefaultSlot0()
{
    static const uint8_t defaultIndexPsk[1] = {0x01};
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", defaultIndexPsk, 1);
}

// =====================================================================================
// Group 1: generateHash golden values and sensitivity
// =====================================================================================

void test_default_longfast_hash_is_golden()
{
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_HASH, channels.getHash(0));
    // Cross-check the hand-derived constant against the reference algorithm on the
    // expanded key (a 1-byte index-1 PSK expands to exactly defaultpsk).
    TEST_ASSERT_EQUAL_UINT8((uint8_t)GOLDEN_LONGFAST_HASH, refHash("LongFast", defaultpsk, sizeof(defaultpsk)));
    TEST_ASSERT_EQUAL_UINT8(GOLDEN_LONGFAST_NAME_XOR ^ GOLDEN_DEFAULTPSK_XOR, (uint8_t)GOLDEN_LONGFAST_HASH);
}

void test_explicit_longfast_name_hashes_like_empty_name()
{
    // getName() substitutes the modem-preset display name for "" - so an explicit
    // "LongFast" and the stock empty name MUST be wire-identical or the two devices
    // silently stop decoding each other.
    static const uint8_t defaultIndexPsk[1] = {0x01};
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "LongFast", defaultIndexPsk, 1);
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_HASH, channels.getHash(0));
}

void test_default_string_name_is_normalized()
{
    // fixupChannel() converts the legacy "Default" name to the "" short form.
    static const uint8_t defaultIndexPsk[1] = {0x01};
    meshtastic_Channel &ch = setSlot(0, meshtastic_Channel_Role_PRIMARY, "Default", defaultIndexPsk, 1);
    TEST_ASSERT_EQUAL_STRING("", ch.settings.name);
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_HASH, channels.getHash(0));
}

void test_hash_differs_on_psk_only()
{
    // Same name, PSKs that differ in bytes AND xor -> different hashes.
    static const uint8_t pskA[16] = {0x01};
    static const uint8_t pskB[16] = {0x02};
    setSlot(1, meshtastic_Channel_Role_SECONDARY, "alpha", pskA, sizeof(pskA));
    setSlot(2, meshtastic_Channel_Role_SECONDARY, "alpha", pskB, sizeof(pskB));
    TEST_ASSERT_TRUE(channels.getHash(1) >= 0);
    TEST_ASSERT_TRUE(channels.getHash(2) >= 0);
    TEST_ASSERT_NOT_EQUAL(channels.getHash(1), channels.getHash(2));
    TEST_ASSERT_EQUAL_UINT8(refHash("alpha", pskA, sizeof(pskA)), (uint8_t)channels.getHash(1));
    TEST_ASSERT_EQUAL_UINT8(refHash("alpha", pskB, sizeof(pskB)), (uint8_t)channels.getHash(2));
}

void test_hash_differs_on_name_only()
{
    static const uint8_t psk[16] = {0x01};
    setSlot(1, meshtastic_Channel_Role_SECONDARY, "alpha", psk, sizeof(psk));
    setSlot(2, meshtastic_Channel_Role_SECONDARY, "beta", psk, sizeof(psk));
    TEST_ASSERT_TRUE(channels.getHash(1) >= 0);
    TEST_ASSERT_TRUE(channels.getHash(2) >= 0);
    TEST_ASSERT_NOT_EQUAL(channels.getHash(1), channels.getHash(2));
}

void test_disabled_channel_has_invalid_hash()
{
    // Slot 3 was never configured: fixupChannel() in setUp left it DISABLED.
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_DISABLED, channels.getByIndex(3).role);
    TEST_ASSERT_EQUAL_INT16(-1, channels.getHash(3));
    // setActiveByIndex on it must refuse and must not touch the crypto key.
    TEST_ASSERT_EQUAL_INT16(-1, channels.setActiveByIndex(3));
    TEST_ASSERT_TRUE(cryptoKeyIsSentinel());
}

// =====================================================================================
// Group 2: getKey() PSK expansion and padding (observed via setActiveByIndex -> crypto->key,
// which is public under PIO_UNIT_TESTING)
// =====================================================================================

void test_psk_index_1_expands_to_defaultpsk()
{
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_HASH, channels.setActiveByIndex(0));
    expectCryptoKey(defaultpsk, sizeof(defaultpsk));
}

void test_psk_index_2_bumps_last_byte()
{
    static const uint8_t psk[1] = {0x02};
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk, 1);
    uint8_t expected[sizeof(defaultpsk)];
    memcpy(expected, defaultpsk, sizeof(defaultpsk));
    expected[sizeof(defaultpsk) - 1] = (uint8_t)(expected[sizeof(defaultpsk) - 1] + 1); // index 2 -> last byte +1
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(expected, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT8(refHash("LongFast", expected, sizeof(expected)), (uint8_t)channels.getHash(0));
}

void test_psk_index_0_disables_encryption()
{
    static const uint8_t psk[1] = {0x00};
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk, 1);
    // Key length 0 = plaintext; the hash then covers the name alone.
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_NAME_XOR, channels.setActiveByIndex(0));
    TEST_ASSERT_EQUAL_INT8(0, crypto->key.length);
}

void test_psk_index_255_boundary()
{
    static const uint8_t psk[1] = {0xFF};
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk, 1);
    uint8_t expected[sizeof(defaultpsk)];
    memcpy(expected, defaultpsk, sizeof(defaultpsk));
    // last byte 0x01 + 0xFF - 1 = 0xFF: the full index range stays inside one uint8_t
    expected[sizeof(defaultpsk) - 1] = 0xFF;
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(expected, sizeof(expected));
}

void test_short_key_pads_to_aes128()
{
    static const uint8_t psk[5] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5};
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk, sizeof(psk));
    uint8_t expected[16] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5}; // bytes 5..15 zero-padded
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(expected, sizeof(expected));
}

void test_midsize_key_pads_to_aes256()
{
    uint8_t psk[24];
    for (size_t i = 0; i < sizeof(psk); i++)
        psk[i] = (uint8_t)(0x40 + i);
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk, sizeof(psk));
    uint8_t expected[32] = {};
    memcpy(expected, psk, sizeof(psk)); // bytes 24..31 zero-padded
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(expected, sizeof(expected));
}

void test_exact_16_and_32_byte_keys_pass_through()
{
    uint8_t psk16[16];
    for (size_t i = 0; i < sizeof(psk16); i++)
        psk16[i] = (uint8_t)(0x10 + i);
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk16, sizeof(psk16));
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(psk16, sizeof(psk16));

    uint8_t psk32[32];
    for (size_t i = 0; i < sizeof(psk32); i++)
        psk32[i] = (uint8_t)(0x20 + i);
    setSlot(0, meshtastic_Channel_Role_PRIMARY, "", psk32, sizeof(psk32));
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(psk32, sizeof(psk32));
}

// =====================================================================================
// Group 3: secondary key inheritance and the recursion guard
// =====================================================================================

void test_secondary_empty_psk_inherits_primary_key()
{
    setSlot(1, meshtastic_Channel_Role_SECONDARY, "second", nullptr, 0);
    // Effective key is the primary's expanded key (defaultpsk); the hash mixes the
    // secondary's OWN name with that inherited key:
    //   xorHash("second") = 's'^'e'^'c'^'o'^'n'^'d' = 0x10; 0x10 ^ 0x02 = 0x12
    TEST_ASSERT_EQUAL_INT16(0x12, channels.getHash(1));
    TEST_ASSERT_EQUAL_UINT8(refHash("second", defaultpsk, sizeof(defaultpsk)), (uint8_t)channels.getHash(1));
    TEST_ASSERT_TRUE(channels.setActiveByIndex(1) >= 0);
    expectCryptoKey(defaultpsk, sizeof(defaultpsk));
}

void test_recursion_guard_primary_slot_marked_secondary()
{
    // Malformed config: the slot primaryIndex points at (0) is itself SECONDARY with no
    // PSK. Without the chIndex != primaryIndex guard, getKey(0) would recurse into
    // getKey(0) forever; the guarded path treats it as encryption-off instead.
    setSlot(0, meshtastic_Channel_Role_SECONDARY, "", nullptr, 0);
    TEST_ASSERT_EQUAL_UINT8(0, channels.getPrimaryIndex());
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_NAME_XOR, channels.getHash(0)); // name-only hash
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_NAME_XOR, channels.setActiveByIndex(0));
    TEST_ASSERT_EQUAL_INT8(0, crypto->key.length);
}

// =====================================================================================
// Group 4: onConfigChanged() no-primary restore and setChannel() demotion
// =====================================================================================

void test_onconfigchanged_promotes_demoted_primary_slot_keeping_key()
{
    // Phone demotes every slot: the slot primaryIndex references is SECONDARY with real
    // key material -> it must be promoted in place, NOT replaced with a default key.
    static const uint8_t privatePsk[16] = {0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB,
                                           0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB};
    setSlot(0, meshtastic_Channel_Role_SECONDARY, "keep", privatePsk, sizeof(privatePsk));
    channels.onConfigChanged();

    meshtastic_Channel &ch = channels.getByIndex(0);
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_PRIMARY, ch.role);
    TEST_ASSERT_EQUAL_UINT8(0, channels.getPrimaryIndex());
    TEST_ASSERT_EQUAL_UINT16(sizeof(privatePsk), ch.settings.psk.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(privatePsk, ch.settings.psk.bytes, sizeof(privatePsk));
    TEST_ASSERT_TRUE(channels.setActiveByIndex(0) >= 0);
    expectCryptoKey(privatePsk, sizeof(privatePsk));
}

void test_onconfigchanged_restores_default_when_all_disabled()
{
    // Every slot DISABLED (zeroed): promoting a zeroed slot would create a plaintext
    // primary, so the restore must install the stock default channel instead.
    memset(&channelFile, 0, sizeof(channelFile));
    channelFile.channels_count = MAX_NUM_CHANNELS;
    channels.onConfigChanged();

    meshtastic_Channel &ch = channels.getByIndex(channels.getPrimaryIndex());
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_PRIMARY, ch.role);
    TEST_ASSERT_TRUE(ch.settings.psk.size >= 1);
    TEST_ASSERT_TRUE(channels.setActiveByIndex(channels.getPrimaryIndex()) >= 0);
    // The restored primary must never come up plaintext.
    TEST_ASSERT_TRUE(crypto->key.length > 0);
#if !defined(USERPREFS_CHANNEL_0_PSK) && !defined(USERPREFS_CHANNEL_0_NAME)
    // Stock build: the restored channel is exactly the default LongFast channel.
    TEST_ASSERT_EQUAL_UINT8(0, channels.getPrimaryIndex());
    TEST_ASSERT_EQUAL_UINT16(1, ch.settings.psk.size);
    TEST_ASSERT_EQUAL_UINT8(0x01, ch.settings.psk.bytes[0]);
    TEST_ASSERT_EQUAL_INT16(GOLDEN_LONGFAST_HASH, channels.getHash(0));
    expectCryptoKey(defaultpsk, sizeof(defaultpsk));
#endif
}

void test_setchannel_demotes_old_primary()
{
    static const uint8_t psk[1] = {0x02};
    meshtastic_Channel c = meshtastic_Channel_init_zero;
    c.index = 1;
    c.role = meshtastic_Channel_Role_PRIMARY;
    c.has_settings = true;
    strncpy(c.settings.name, "boss", sizeof(c.settings.name) - 1);
    memcpy(c.settings.psk.bytes, psk, sizeof(psk));
    c.settings.psk.size = sizeof(psk);

    channels.setChannel(c);
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_SECONDARY, channels.getByIndex(0).role);
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_PRIMARY, channels.getByIndex(1).role);

    // primaryIndex tracks the change only once onConfigChanged() re-scans.
    channels.onConfigChanged();
    TEST_ASSERT_EQUAL_UINT8(1, channels.getPrimaryIndex());
}

// =====================================================================================
// Group 5: decryptForHash() bounds - regression pin for #11046 (cfecef537). Pre-fix the
// bound was `>`, so chIndex == getNumChannels() read one past hashes[] on the hot decode
// path for every received packet.
// =====================================================================================

void test_decryptforhash_rejects_out_of_range_index()
{
    const ChannelIndex n = channels.getNumChannels();
    TEST_ASSERT_EQUAL_UINT8(MAX_NUM_CHANNELS, n);
    TEST_ASSERT_FALSE(channels.decryptForHash(n, (ChannelHash)channels.getHash(0)));
    TEST_ASSERT_FALSE(channels.decryptForHash((ChannelIndex)(n + 1), (ChannelHash)channels.getHash(0)));
    TEST_ASSERT_FALSE(channels.decryptForHash((ChannelIndex)MAX_NUM_CHANNELS, 0x08));
    TEST_ASSERT_FALSE(channels.decryptForHash((ChannelIndex)255, 0x08));
    // A rejected index must not have touched the crypto key.
    TEST_ASSERT_TRUE(cryptoKeyIsSentinel());
}

void test_decryptforhash_accepts_valid_index_and_hash()
{
    TEST_ASSERT_TRUE(channels.decryptForHash(0, (ChannelHash)GOLDEN_LONGFAST_HASH));
    expectCryptoKey(defaultpsk, sizeof(defaultpsk));
}

void test_decryptforhash_rejects_wrong_hash()
{
    TEST_ASSERT_FALSE(channels.decryptForHash(0, (ChannelHash)(GOLDEN_LONGFAST_HASH + 1)));
    TEST_ASSERT_TRUE(cryptoKeyIsSentinel());
}

void test_decryptforhash_disabled_slot_matches_no_hash()
{
    // A DISABLED slot's cached hash is -1 (int16), which no 0-255 wire hash can equal.
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_DISABLED, channels.getByIndex(3).role);
    for (int h = 0; h <= 255; h++)
        TEST_ASSERT_FALSE(channels.decryptForHash(3, (ChannelHash)h));
    TEST_ASSERT_TRUE(cryptoKeyIsSentinel());
}

// =====================================================================================
// Group 6: Router perhapsDecode() same-hash fall-through. Two enabled channels can share
// a hash (it is one xor byte); the decoder must try each candidate and commit the one
// whose key authenticates a well-formed Data, rewriting p->channel from hash to INDEX -
// the value admin-channel authorization consumes downstream.
//
// Skipped on event builds: their decode path runs isBlockedEventCoordinatePacket() ->
// willUsePki(), which dereferences the nodeDB this suite deliberately never constructs
// (keeping it free of disk writes).
// =====================================================================================

#if !USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL

// Same name + PSKs with equal xor but different bytes -> identical hash, different keys.
static const uint8_t kClashPskA[16] = {0x01};
static const uint8_t kClashPskB[16] = {0x00, 0x01};

static uint8_t configureCollisionChannels()
{
    setSlot(1, meshtastic_Channel_Role_SECONDARY, "clash", kClashPskA, sizeof(kClashPskA));
    setSlot(2, meshtastic_Channel_Role_SECONDARY, "clash", kClashPskB, sizeof(kClashPskB));
    TEST_ASSERT_TRUE(channels.getHash(1) >= 0);
    TEST_ASSERT_EQUAL_INT16(channels.getHash(1), channels.getHash(2));
    // Nonzero hash keeps perhapsDecode() off the PKI-candidate branch (p->channel == 0),
    // which would dereference the nodeDB this suite deliberately never constructs.
    TEST_ASSERT_TRUE(channels.getHash(1) != 0);
    return (uint8_t)channels.getHash(1);
}

static meshtastic_Data makeProbeData()
{
    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = meshtastic_PortNum_POSITION_APP;
    static const char probe[] = "collision-probe";
    memcpy(d.payload.bytes, probe, sizeof(probe));
    d.payload.size = sizeof(probe);
    return d;
}

// Encrypts with whatever key is currently loaded into the crypto engine.
static meshtastic_MeshPacket makeEncryptedPacket(uint8_t channelHash, const meshtastic_Data &d)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = 0x11223344;
    p.to = NODENUM_BROADCAST; // broadcast: no unicast-only branches
    p.id = 0xA5A5A5A5;
    p.channel = channelHash;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.encrypted.size = (pb_size_t)pb_encode_to_bytes(p.encrypted.bytes, sizeof(p.encrypted.bytes), &meshtastic_Data_msg, &d);
    TEST_ASSERT_TRUE(p.encrypted.size > 0);
    crypto->encryptPacket(p.from, p.id, p.encrypted.size, p.encrypted.bytes);
    return p;
}

void test_perhapsdecode_collision_selects_matching_psk()
{
    // is_licensed short-circuits the legacy-DM isToUs() check inside perhapsDecode(),
    // which would otherwise dereference the absent nodeDB (restored in tearDown).
    owner.is_licensed = true;
    const uint8_t h = configureCollisionChannels();
    const meshtastic_Data d = makeProbeData();

    TEST_ASSERT_TRUE(channels.setActiveByIndex(2) >= 0); // encrypt with slot 2's key
    meshtastic_MeshPacket p = makeEncryptedPacket(h, d);

    TEST_ASSERT_EQUAL_INT(DecodeState::DECODE_SUCCESS, perhapsDecode(&p));
    // Hash slot 1 was tried first and rejected; the committed channel is the INDEX 2.
    TEST_ASSERT_EQUAL_UINT8(2, p.channel);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_decoded_tag, p.which_payload_variant);
    TEST_ASSERT_EQUAL_INT(meshtastic_PortNum_POSITION_APP, p.decoded.portnum);
    TEST_ASSERT_EQUAL_UINT16(d.payload.size, p.decoded.payload.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(d.payload.bytes, p.decoded.payload.bytes, d.payload.size);
}

void test_perhapsdecode_wrong_key_is_decode_failure()
{
    owner.is_licensed = true;
    const uint8_t h = configureCollisionChannels();

    // Encrypt with a key belonging to NO configured channel; the hash still matches
    // slots 1 and 2, so a channel was tried -> DECODE_FAILURE, not DECODE_OPAQUE.
    CryptoKey stranger;
    memset(stranger.bytes, 0x5A, sizeof(stranger.bytes));
    stranger.length = 16;
    crypto->setKey(stranger);
    meshtastic_MeshPacket p = makeEncryptedPacket(h, makeProbeData());

    TEST_ASSERT_EQUAL_INT(DecodeState::DECODE_FAILURE, perhapsDecode(&p));
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, p.which_payload_variant);
}

void test_perhapsdecode_unknown_hash_is_opaque()
{
    owner.is_licensed = true;
    configureCollisionChannels();

    // Find a nonzero wire hash no enabled channel produces.
    int candidate = -1;
    for (int c = 1; c < 256 && candidate < 0; c++) {
        bool used = false;
        for (ChannelIndex i = 0; i < channels.getNumChannels(); i++)
            if (channels.getHash(i) == c)
                used = true;
        if (!used)
            candidate = c;
    }
    TEST_ASSERT_TRUE(candidate > 0);
    TEST_MSG_FMT("unknown-hash probe uses 0x%02x", (unsigned)candidate);

    TEST_ASSERT_TRUE(channels.setActiveByIndex(2) >= 0);
    meshtastic_MeshPacket p = makeEncryptedPacket((uint8_t)candidate, makeProbeData());

    // No channel matched at all: the packet stays opaque (relayable ciphertext).
    TEST_ASSERT_EQUAL_INT(DecodeState::DECODE_OPAQUE, perhapsDecode(&p));
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, p.which_payload_variant);
}

#endif // !USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL

// --- Unity lifecycle ---

void setUp(void)
{
    memset(&channelFile, 0, sizeof(channelFile));
    memset(&config, 0, sizeof(config));
    owner.is_licensed = false;
    channels.initDefaults(); // 8 slots + default lora config; only slot 0 populated
    // Pin the preset the golden hashes assume ("" -> "LongFast"), in case a variant
    // build's USERPREFS_LORACONFIG_MODEM_PRESET overrode it inside initDefaults().
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    channels.onConfigChanged(); // computes the hash cache and primaryIndex
    forceCanonicalDefaultSlot0();
    armCryptoSentinel();
}

void tearDown(void)
{
    owner.is_licensed = false;
}

CK_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    // perhapsDecode() takes cryptLock; normally Router's ctor allocates it, but this
    // suite never constructs a Router (nor a NodeDB - it must stay disk-write free).
    if (!cryptLock)
        cryptLock = new concurrency::Lock();
    UNITY_BEGIN();

    printf("\n=== generateHash golden values ===\n");
    RUN_TEST(test_default_longfast_hash_is_golden);
    RUN_TEST(test_explicit_longfast_name_hashes_like_empty_name);
    RUN_TEST(test_default_string_name_is_normalized);
    RUN_TEST(test_hash_differs_on_psk_only);
    RUN_TEST(test_hash_differs_on_name_only);
    RUN_TEST(test_disabled_channel_has_invalid_hash);

    printf("\n=== getKey expansion and padding ===\n");
    RUN_TEST(test_psk_index_1_expands_to_defaultpsk);
    RUN_TEST(test_psk_index_2_bumps_last_byte);
    RUN_TEST(test_psk_index_0_disables_encryption);
    RUN_TEST(test_psk_index_255_boundary);
    RUN_TEST(test_short_key_pads_to_aes128);
    RUN_TEST(test_midsize_key_pads_to_aes256);
    RUN_TEST(test_exact_16_and_32_byte_keys_pass_through);

    printf("\n=== secondary inheritance and recursion guard ===\n");
    RUN_TEST(test_secondary_empty_psk_inherits_primary_key);
    RUN_TEST(test_recursion_guard_primary_slot_marked_secondary);

    printf("\n=== onConfigChanged restore and setChannel ===\n");
    RUN_TEST(test_onconfigchanged_promotes_demoted_primary_slot_keeping_key);
    RUN_TEST(test_onconfigchanged_restores_default_when_all_disabled);
    RUN_TEST(test_setchannel_demotes_old_primary);

    printf("\n=== decryptForHash bounds (#11046) ===\n");
    RUN_TEST(test_decryptforhash_rejects_out_of_range_index);
    RUN_TEST(test_decryptforhash_accepts_valid_index_and_hash);
    RUN_TEST(test_decryptforhash_rejects_wrong_hash);
    RUN_TEST(test_decryptforhash_disabled_slot_matches_no_hash);

#if !USERPREFS_BLOCK_POSITION_ON_EVENT_CHANNEL
    printf("\n=== perhapsDecode same-hash fall-through ===\n");
    RUN_TEST(test_perhapsdecode_collision_selects_matching_psk);
    RUN_TEST(test_perhapsdecode_wrong_key_is_decode_failure);
    RUN_TEST(test_perhapsdecode_unknown_hash_is_opaque);
#endif

    exit(UNITY_END());
}

CK_TEST_ENTRY void loop() {}
