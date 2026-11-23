/*
 * Shared known-answer vectors for the app clients: the same bytes the archive produces for a message
 * hash, an AES-CTR channel decrypt, an AES-CCM DM decrypt, and a framed replay split.
 *
 * Every expected value here is a fixed literal produced once from the firmware encoder and checked
 * into sfpp-vectors.json beside the analysis scripts. The cases deliberately do not recompute an
 * expectation from the code under test: an app and the firmware agree only if both match these bytes.
 *
 * The regression guarded: a change to the hash field order, nonce layout, PSK expansion, CCM tag
 * handling or frame encoding would silently break recovery on every client while these vectors stayed
 * green, because no other suite compares against an independent copy.
 */

#include "configuration.h" // for the extern "C" setup()/loop() the portduino runner links against

#include "TestUtil.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "modules/Native/SfppFrame.h"

#include <cstring>
#include <pb_encode.h>
#include <string>
#include <unity.h>
#include <vector>

namespace
{

constexpr uint32_t kFrom = 0x11223344;
constexpr uint32_t kFrom256 = 0x99AABBCC;
constexpr uint32_t kId = 0x55667788;
constexpr uint32_t kId256 = 0x01020304;
constexpr uint32_t kDmTo = 0xAABBCCDD;

std::vector<uint8_t> fromHex(const char *hex)
{
    std::vector<uint8_t> out;
    const size_t len = strlen(hex);
    out.reserve(len / 2);
    for (size_t i = 0; i + 1 < len; i += 2) {
        char byte[3] = {hex[i], hex[i + 1], 0};
        out.push_back((uint8_t)strtoul(byte, nullptr, 16));
    }
    return out;
}

std::string toHex(const uint8_t *data, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

CryptoKey keyFromHex(const char *hex)
{
    CryptoKey key = {};
    const auto bytes = fromHex(hex);
    memcpy(key.bytes, bytes.data(), bytes.size());
    key.length = (int8_t)bytes.size();
    return key;
}

// --- hash inputs and expected 16-byte message hashes ---

const char *kHashShortCipher = "42";
const char *kHashLongCipher = "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8ff060d141b222930373e454c53"
                              "5a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6edf4fb020910171e252c333a41484f56"
                              "5d646b727980878e959ca3aab1b8bfc6cdd4dbe2e9f0f7fe050c131a21282f363d444b525960676e757c838a91989fa6ad"
                              "b4bbc2c9d0d7dee5ecf3fa01080f161d242b323940474e555c636a71787f86"
                              "8d949ba2a9b0b7bec5ccd3dae1e8eff6fd040b121920272e353c434a51585f666d74";

const char *kHashBcastShort = "f7df5ce7020e1ef464411e7d88ceeb53";
const char *kHashDmShort = "45dd2f59e87e1646aec917d5b91e8e18";
const char *kHashBcastLong = "14fb909190fe8edba0c404068a15de30";
const char *kHashDmLong = "c341882de1df7e8747dca21c5a44c507";

// --- CTR inputs and expected plaintexts ---

const char *kDefaultPskHex = "01";
const char *kExpandedDefaultKeyHex = "d4f1bb3a20290759f0bcffabcf4e6901";
const char *kCtrDefaultCipher = "7297fe7cfbe83fa35e6ffc4235def389";
const char *kCtrDefaultPlain = "6374722064656661756c74206b657921";

const char *kCtr256Key = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
const char *kCtr256Cipher = "73038f4905cc5b666a083a79934c777c";
const char *kCtr256Plain = "637472203235362d626974206b657921";

// --- CCM input and expected ciphertext + 12-byte tag ---

const char *kCcmKey = "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf";
const char *kCcmPlain = "736670702063636d20766563746f72207061796c6f6164";
const char *kCcmCipherAndTag = "de4eaed1f465b6d1eacc91d2373e284a1279fde7ea95dbd71db87f5582a12685252df1";

// --- framed replay inputs and expected encoded ITEM_FRAME bodies ---

const char *kFrames140Cipher =
    "101316191c1f2225282b2e3134373a3d404346494c4f5255585b5e6164676a6d707376797c7f8285888b8e9194979a9da0a3a6a9acafb2b5b8bbbec1c4c7"
    "cacdd0d3d6d9dcdfe2e5e8ebeef1f4f7fafd000306090c0f12"
    "15181b1e2124272a2d303336393c3f4245484b4e5154575a5d606366696c6f7275787b7e8184878a8d909396999c9fa2a5a8abaeb1";
const char *kFrames140_0 = "080d1208296f8748c01ea4fd2a7f101316191c1f2225282b2e3134373a3d404346494c4f5255585b5e6164676a6d707376797"
                           "c7f8285888b8e9194979a9da0a3a6a9acafb2b5b8bbbec1c4c7cacdd0d3d6d9dcdfe2e5e8ebeef1"
                           "f4f7fafd000306090c0f1215181b1e2124272a2d303336393c3f4245484b4e5154575a5d606366696c6f7275787b7e8184878"
                           "a3088ef99ab0540c4e688890188010790018004980101";
const char *kFrames140_1 = "080d1208296f8748c01ea4fd2a0d8d909396999c9fa2a5a8abaeb190018104";

const char *kFrames300Cipher = "80858a8f94999ea3a8adb2b7bcc1c6cbd0d5dadfe4e9eef3f8fd02070c11161b20252a2f34393e43484d52575c61666b7"
                               "0757a7f84898e93989da2a7acb1b6bbc0c5cacfd4d9dee3e8edf2f7fc01060b10151a1f"
                               "24292e33383d42474c51565b60656a6f74797e83888d92979ca1a6abb0b5babfc4c9ced3d8dde2e7ecf1f6fb00050a0f1"
                               "4191e23282d32373c41464b50555a5f64696e73787d82878c91969ba0a5aaafb4b9bec3c8cd"
                               "d2d7dce1e6ebf0f5faff04090e13181d22272c31363b40454a4f54595e63686d72777c81868b90959a9fa4a9aeb3b8bdc"
                               "2c7ccd1d6dbe0e5eaeff4f9fe03080d12171c21262b30353a3f44494e53585d62676c7176"
                               "7b80858a8f94999ea3a8adb2b7bcc1c6cbd0d5dadfe4e9eef3f8fd02070c11161b20252a2f34393e43484d5257";
const char *kFrames300_0 = "080d1208fc223b7f49903fee2a7f80858a8f94999ea3a8adb2b7bcc1c6cbd0d5dadfe4e9eef3f8fd02070c11161b20252a2f3"
                           "4393e43484d52575c61666b70757a7f84898e93989da2a7acb1b6bbc0c5cacfd4d9dee3e8ed"
                           "f2f7fc01060b10151a1f24292e33383d42474c51565b60656a6f74797e83888d92979ca1a6abb0b5babfc4c9ced3d8dde2e7e"
                           "cf1f63088ef99ab0540c4e688890188010790018006980101";
const char *kFrames300_1 = "080d1208fc223b7f49903fee2a9501fb00050a0f14191e23282d32373c41464b50555a5f64696e73787d82878c91969ba0a5a"
                           "aafb4b9bec3c8cdd2d7dce1e6ebf0f5faff04090e13181d22272c31363b40454a4f5459"
                           "5e63686d72777c81868b90959a9fa4a9aeb3b8bdc2c7ccd1d6dbe0e5eaeff4f9fe03080d12171c21262b30353a3f44494e535"
                           "85d62676c71767b80858a8f94999ea3a8adb2b7bcc1c6cbd0d5dadf90018106";
const char *kFrames300_2 = "080d1208fc223b7f49903fee2a18e4e9eef3f8fd02070c11161b20252a2f34393e43484d525790018206";

} // namespace

void setUp(void) {}
void tearDown(void) {}

// --- hash ---

void test_hashBroadcastOneByte()
{
    const auto ciphertext = fromHex(kHashShortCipher);
    uint8_t out[sfppframe::kMessageHashBytes];
    sfppframe::messageHash(ciphertext.data(), ciphertext.size(), 0xFFFFFFFF, kFrom, kId, out);
    TEST_ASSERT_EQUAL_STRING(kHashBcastShort, toHex(out, sizeof(out)).c_str());
}

void test_hashDmOneByte()
{
    const auto ciphertext = fromHex(kHashShortCipher);
    uint8_t out[sfppframe::kMessageHashBytes];
    sfppframe::messageHash(ciphertext.data(), ciphertext.size(), kDmTo, kFrom, kId, out);
    TEST_ASSERT_EQUAL_STRING(kHashDmShort, toHex(out, sizeof(out)).c_str());
}

void test_hashBroadcastTwoHundredBytes()
{
    const auto ciphertext = fromHex(kHashLongCipher);
    uint8_t out[sfppframe::kMessageHashBytes];
    sfppframe::messageHash(ciphertext.data(), ciphertext.size(), 0xFFFFFFFF, kFrom, kId, out);
    TEST_ASSERT_EQUAL_STRING(kHashBcastLong, toHex(out, sizeof(out)).c_str());
}

void test_hashDmTwoHundredBytes()
{
    const auto ciphertext = fromHex(kHashLongCipher);
    uint8_t out[sfppframe::kMessageHashBytes];
    sfppframe::messageHash(ciphertext.data(), ciphertext.size(), kDmTo, kFrom, kId, out);
    TEST_ASSERT_EQUAL_STRING(kHashDmLong, toHex(out, sizeof(out)).c_str());
}

// --- CTR ---

// The short PSK is index 1 into the public default family, so the expanded key is defaultpsk
// unchanged. This also pins that the expansion happens before the AES key is used.
void test_ctrDefaultKey()
{
    const CryptoKey key = channels.getKey(0);
    TEST_ASSERT_EQUAL_INT8(16, key.length);
    TEST_ASSERT_EQUAL_STRING(kExpandedDefaultKeyHex, toHex(key.bytes, 16).c_str());

    auto ciphertext = fromHex(kCtrDefaultCipher);
    crypto->setKey(key);
    crypto->decrypt(kFrom, kId, ciphertext.size(), ciphertext.data());
    TEST_ASSERT_EQUAL_STRING(kCtrDefaultPlain, toHex(ciphertext.data(), ciphertext.size()).c_str());
}

void test_ctr256BitKey()
{
    const auto ciphertextIn = fromHex(kCtr256Cipher);
    auto ciphertext = ciphertextIn;
    crypto->setKey(keyFromHex(kCtr256Key));
    crypto->decrypt(kFrom256, kId256, ciphertext.size(), ciphertext.data());
    TEST_ASSERT_EQUAL_STRING(kCtr256Plain, toHex(ciphertext.data(), ciphertext.size()).c_str());
}

// --- CCM ---

void test_ccmEncryptMatchesVector()
{
    const CryptoKey key = keyFromHex(kCcmKey);
    const auto plaintext = fromHex(kCcmPlain);
    const auto expected = fromHex(kCcmCipherAndTag);

    uint8_t out[128] = {};
    TEST_ASSERT_TRUE(crypto->encryptPacketCCM(key, kFrom, kDmTo, kId, plaintext.size(), plaintext.data(), out));
    TEST_ASSERT_EQUAL_STRING(kCcmCipherAndTag, toHex(out, expected.size()).c_str());
}

void test_ccmDecryptMatchesVector()
{
    const CryptoKey key = keyFromHex(kCcmKey);
    const auto ciphertext = fromHex(kCcmCipherAndTag);

    uint8_t out[128] = {};
    TEST_ASSERT_TRUE(crypto->decryptPacketCCM(key, kFrom, kDmTo, kId, ciphertext.size(), ciphertext.data(), out));
    TEST_ASSERT_EQUAL_STRING(kCcmPlain, toHex(out, ciphertext.size() - CryptoEngine::AEAD_TAG_SIZE).c_str());
}

void test_ccmTamperedTagFails()
{
    const CryptoKey key = keyFromHex(kCcmKey);
    auto ciphertext = fromHex(kCcmCipherAndTag);
    ciphertext.back() ^= 0x01;

    uint8_t out[128] = {};
    TEST_ASSERT_FALSE(crypto->decryptPacketCCM(key, kFrom, kDmTo, kId, ciphertext.size(), ciphertext.data(), out));
}

// --- framed replay ---

void test_frames140()
{
    const auto ciphertext = fromHex(kFrames140Cipher);
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_TRUE(sfppframe::split(ciphertext.data(), ciphertext.size(), 0xFFFFFFFF, kFrom, kId, 7, true, frames));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)frames.size());

    const char *expected[] = {kFrames140_0, kFrames140_1};
    for (size_t i = 0; i < frames.size(); i++) {
        uint8_t body[256];
        const size_t len = pb_encode_to_bytes(body, sizeof(body), &meshtastic_StoreForwardPlusPlus_msg, &frames[i]);
        TEST_ASSERT_EQUAL_STRING(expected[i], toHex(body, len).c_str());
    }
}

void test_frames300()
{
    const auto ciphertext = fromHex(kFrames300Cipher);
    std::vector<meshtastic_StoreForwardPlusPlus> frames;
    TEST_ASSERT_TRUE(sfppframe::split(ciphertext.data(), ciphertext.size(), 0xFFFFFFFF, kFrom, kId, 7, true, frames));
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)frames.size());

    const char *expected[] = {kFrames300_0, kFrames300_1, kFrames300_2};
    for (size_t i = 0; i < frames.size(); i++) {
        uint8_t body[256];
        const size_t len = pb_encode_to_bytes(body, sizeof(body), &meshtastic_StoreForwardPlusPlus_msg, &frames[i]);
        TEST_ASSERT_EQUAL_STRING(expected[i], toHex(body, len).c_str());
    }
}

void setup()
{
    initializeTestEnvironment();
    channels.initDefaults();

    UNITY_BEGIN();

    RUN_TEST(test_hashBroadcastOneByte);
    RUN_TEST(test_hashDmOneByte);
    RUN_TEST(test_hashBroadcastTwoHundredBytes);
    RUN_TEST(test_hashDmTwoHundredBytes);

    RUN_TEST(test_ctrDefaultKey);
    RUN_TEST(test_ctr256BitKey);

    RUN_TEST(test_ccmEncryptMatchesVector);
    RUN_TEST(test_ccmDecryptMatchesVector);
    RUN_TEST(test_ccmTamperedTagFails);

    RUN_TEST(test_frames140);
    RUN_TEST(test_frames300);

    exit(UNITY_END());
}

void loop() {}
