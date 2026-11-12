// trunk-ignore-all(gitleaks): Published test vectors and throwaway keys, not real secrets.
// Crypto test vectors as hex strings. Depends on nothing but <stdint.h>/<stddef.h>, so suites that
// link only the crypto layer can reuse it.
//
// Provenance of each table:
//   Standard      - copied from the cited RFC, NIST publication or Wycheproof file.
//   Independent   - generated with Python hashlib / cryptography (OpenSSL) from the stated pattern;
//                   not the firmware, and not a standard.
//   Golden        - Independent vectors for the current wire format, which the firmware must keep
//                   producing. A failure means the wire format changed.
//   Golden (fw)   - taken from the current firmware itself; pins today's behaviour, proves nothing
//                   about correctness.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace crypto_vectors
{

inline int hexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

// Decodes up to cap bytes; returns the number of bytes in the full string.
inline size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (s[2 * n] && s[2 * n + 1]) {
        if (n < cap)
            out[n] = (uint8_t)((hexNibble(s[2 * n]) << 4) | hexNibble(s[2 * n + 1]));
        n++;
    }
    return n;
}

inline size_t hexLen(const char *s)
{
    size_t n = 0;
    while (s[2 * n] && s[2 * n + 1])
        n++;
    return n;
}

// byte i = (i * 7 + 3) mod 256, the plaintext of the Independent length tables
inline void pattern(uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = (uint8_t)(i * 7 + 3);
}

// Key bytes 00 01 02 ... of the requested length
inline void seqKey(uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = (uint8_t)i;
}

struct CcmVector {
    const char *name, *key, *nonce, *aad, *plain, *crypt, *tag;
};
struct HmacVector {
    const char *name, *key, *data, *mac; // mac may be truncated (RFC 4231 case 5)
};
struct X25519Vector {
    const char *name, *scalar, *point, *out;
};
struct Ed25519Vector {
    const char *name, *pub, *msg, *sig;
};
struct CtrVector {
    const char *name, *key, *counter, *plain, *crypt;
};
struct HashVector {
    const char *name, *msg, *digest;
};
struct CtrLenVector {
    int keyLen, len;
    const char *cryptSha256; // SHA-256 of the ciphertext, plaintext is pattern(len)
};
struct CcmLenVector {
    int keyLen, tagLen, aadLen, len;
    const char *cryptTagSha256; // SHA-256 of ciphertext || tag, plaintext is pattern(len)
};
struct HashLenVector {
    int len;
    const char *sha256, *sha512; // of pattern(len)
};
struct NonceVector {
    uint32_t from;
    uint64_t packetId;
    uint32_t extra;
    const char *nonce;
};
struct PskCtrVector {
    const char *key;
    uint32_t from;
    uint64_t packetId;
    const char *plain, *crypt;
};
struct PskCcmVector {
    const char *key;
    uint32_t from, to;
    uint64_t packetId;
    const char *plain, *aad, *cryptTag;
};
struct PkiVector {
    uint32_t from;
    uint64_t packetId;
    uint32_t extra;
    const char *plain, *wire; // wire = ciphertext || 8-byte tag || 4-byte extraNonce
};
struct AckProofVector {
    uint32_t ackFrom, ackTo, requestId;
    const char *routing, *hmacSha256;
};

// Standard: NIST SP 800-38A F.5.1 and F.5.5, CTR-AES128 and CTR-AES256.
static const char *const kSp80038aPlain[] = {
    "6bc1bee22e409f96e93d7e117393172a",
    "ae2d8a571e03ac9c9eb76fac45af8e51",
    "30c81c46a35ce411e5fbc1191a0a52ef",
    "f69f2445df4f9b17ad2b417be66c3710",
};
static const CtrVector kCtrStd[] = {
    {"SP 800-38A F.5.1 CTR-AES128", "2b7e151628aed2a6abf7158809cf4f3c", "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
     "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66"
     "c3710",
     "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f30"
     "09cee"},
    {"SP 800-38A F.5.5 CTR-AES256", "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
     "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
     "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66"
     "c3710",
     "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c52b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457"
     "941a6"},
};

// Standard: NIST FIPS 180-4 example values (csrc.nist.gov SHA256.pdf, SHA512.pdf); the empty-message
// digests are the widely published ones.
static const HashVector kSha256Std[] = {
    {"FIPS 180 SHA-256 empty", "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"FIPS 180 SHA-256 abc", "616263", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"FIPS 180 SHA-256 448-bit",
     "6162636462636465636465666465666765666768666768696768696a68696a6b696a6b6c6a6b6c6d6b6c6d6e6c6d6e6f6d6e6f706e6f7071",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
};
static const HashVector kSha512Std[] = {
    {"FIPS 180 SHA-512 empty", "",
     "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af92"
     "7da3e"},
    {"FIPS 180 SHA-512 abc", "616263",
     "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54"
     "ca49f"},
    {"FIPS 180 SHA-512 896-bit",
     "61626364656667686263646566676869636465666768696a6465666768696a6b65666768696a6b6c666768696a6b6c6d6768696a6b6c6d6e68696a6b6c6"
     "d6e6f696a6b6c6d6e6f706a6b6c6d6e6f70716b6c6d6e6f7071726c6d6e6f707172736d6e6f70717273746e6f707172737475",
     "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874"
     "be909"},
};

// Standard: RFC 7748 section 5.2, 1 iteration starting from k = u = 9.
static const char *const kX25519NineIter1 = "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079";
static const char *const kX25519Base = "0900000000000000000000000000000000000000000000000000000000000000";

// RFC 8032 test 1 with S replaced by S + q (q = group order). Strict verifiers reject it; the Crypto
// library pin in use decides, so suites record the outcome per pin instead of assuming one.
static const char *const kEd25519MalleatedSig = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901554c8c7872aa064e04"
                                                "9dbb3013fbf29380d25bf5f0595bbe24655141438e7a101b";

// Standard: RFC 3610 packet vectors (all L = 2) and Wycheproof aes_ccm_test.json (13-byte nonce,
// 128-bit tag, AES-128 and AES-256).
static const CcmVector kCcmStd[] = {
    {"RFC 3610 packet vector #1", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000003020100a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e", "588c979a61c663d2f066d0c2c0f989806d5f6b61dac384", "17e8d12cfdf926e0"},
    {"RFC 3610 packet vector #2", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000004030201a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "72c91a36e135f8cf291ca894085c87e3cc15c439c9e43a3b", "a091d56e10400916"},
    {"RFC 3610 packet vector #3", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000005040302a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", "51b1e5f44a197d1da46b0f8e2d282ae871e838bb64da859657",
     "4adaa76fbd9fb0c5"},
    {"RFC 3610 packet vector #4", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000006050403a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e", "a28c6865939a9a79faaa5c4c2a9d4a91cdac8c", "96c861b9c9e61ef1"},
    {"RFC 3610 packet vector #5", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000007060504a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e1f", "dcf1fb7b5d9e23fb9d4e131253658ad86ebdca3e", "51e83f077d9c2d93"},
    {"RFC 3610 packet vector #6", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000008070605a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e1f20", "6fc1b011f006568b5171a42d953d469b2570a4bd87", "405a0443ac91cb94"},
    {"RFC 3610 packet vector #7", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "00000009080706a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e", "0135d1b2c95f41d5d1d4fec185d166b8094e999dfed96c", "048c56602c97acbb7490"},
    {"RFC 3610 packet vector #8", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "0000000a090807a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "7b75399ac0831dd2f0bbd75879a2fd8f6cae6b6cd9b7db24",
     "c17b4433f434963f34b4"},
    {"RFC 3610 packet vector #9", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "0000000b0a0908a0a1a2a3a4a5", "0001020304050607",
     "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", "82531a60cc24945a4b8279181ab5c84df21ce7f9b73f42e197",
     "ea9c07e56b5eb17e5f4e"},
    {"RFC 3610 packet vector #10", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "0000000c0b0a09a0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e", "07342594157785152b074098330abb141b947b", "566aa9406b4d999988dd"},
    {"RFC 3610 packet vector #11", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "0000000d0c0b0aa0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e1f", "676bb20380b0e301e8ab79590a396da78b834934", "f53aa2e9107a8b6c022c"},
    {"RFC 3610 packet vector #12", "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", "0000000e0d0c0ba0a1a2a3a4a5", "000102030405060708090a0b",
     "0c0d0e0f101112131415161718191a1b1c1d1e1f20", "c0ffa0d6f05bdb67f24d43a4338d2aa4bed7b20e43", "cd1aa31662e7ad65d6db"},
    {"RFC 3610 packet vector #13", "d7828d13b2b0bdc325a76236df93cc6b", "00412b4ea9cdbe3c9696766cfa", "0be1a88bace018b1",
     "08e8cf97d820ea258460e96ad9cf5289054d895ceac47c", "4cb97f86a2a4689a877947ab8091ef5386a6ffbdd080f8", "e78cf7cb0cddd7b3"},
    {"RFC 3610 packet vector #14", "d7828d13b2b0bdc325a76236df93cc6b", "0033568ef7b2633c9696766cfa", "63018f76dc8a1bcb",
     "9020ea6f91bdd85afa0039ba4baff9bfb79c7028949cd0ec", "4ccb1e7ca981befaa0726c55d378061298c85c92814abc33", "c52ee81d7d77c08a"},
    {"RFC 3610 packet vector #15", "d7828d13b2b0bdc325a76236df93cc6b", "00103fe41336713c9696766cfa", "aa6cfa36cae86b40",
     "b916e0eacc1c00d7dcec68ec0b3bbb1a02de8a2d1aa346132e", "b1d23a2220ddc0ac900d9aa03c61fcf4a559a4417767089708",
     "a776796edb723506"},
    {"RFC 3610 packet vector #16", "d7828d13b2b0bdc325a76236df93cc6b", "00764c63b8058e3c9696766cfa", "d0d0735c531e1becf049c244",
     "12daac5630efa5396f770ce1a66b21f7b2101c", "14d253c3967b70609b7cbb7c49916028324526", "9a6f49975bcadeaf"},
    {"RFC 3610 packet vector #17", "d7828d13b2b0bdc325a76236df93cc6b", "00f8b678094e3b3c9696766cfa", "77b60f011c03e1525899bcae",
     "e88b6a46c78d63e52eb8c546efb5de6f75e9cc0d", "5545ff1a085ee2efbf52b2e04bee1e2336c73e3f", "762c0c7744fe7e3c"},
    {"RFC 3610 packet vector #18", "d7828d13b2b0bdc325a76236df93cc6b", "00d560912d3f703c9696766cfa", "cd9044d2b71fdb8120ea60c0",
     "6435acbafb11a82e2f071d7ca4a5ebd93a803ba87f", "009769ecabdf48625594c59251e6035722675e04c8", "47099e5ae0704551"},
    {"RFC 3610 packet vector #19", "d7828d13b2b0bdc325a76236df93cc6b", "0042fff8f1951c3c9696766cfa", "d85bc7e69f944fb8",
     "8a19b950bcf71a018e5e6701c91787659809d67dbedd18", "bc218daa947427b6db386a99ac1aef23ade0b52939cb6a", "637cf9bec2408897c6ba"},
    {"RFC 3610 packet vector #20", "d7828d13b2b0bdc325a76236df93cc6b", "00920f40e56cdc3c9696766cfa", "74a0ebc9069f5b37",
     "1761433c37c5a35fc1f39f406302eb907c6163be38c98437", "5810e6fd25874022e80361a478e3e9cf484ab04f447efff6",
     "f0a477cc2fc9bf548944"},
    {"RFC 3610 packet vector #21", "d7828d13b2b0bdc325a76236df93cc6b", "0027ca0c7120bc3c9696766cfa", "44a3aa3aae6475ca",
     "a434a8e58500c6e41530538862d686ea9e81301b5ae4226bfa", "f2beed7bc5098e83feb5b31608f8e29c38819a89c8e776f154",
     "4d4151a4ed3a8b87b9ce"},
    {"RFC 3610 packet vector #22", "d7828d13b2b0bdc325a76236df93cc6b", "005b8ccbcd9af83c9696766cfa", "ec46bb63b02520c33c49fd70",
     "b96b49e21d621741632875db7f6c9243d2d7c2", "31d750a09da3ed7fddd49a2032aabf17ec8ebf", "7d22c8088c666be5c197"},
    {"RFC 3610 packet vector #23", "d7828d13b2b0bdc325a76236df93cc6b", "003ebe94044b9a3c9696766cfa", "47a65ac78b3d594227e85e71",
     "e2fcfbb880442c731bf95167c8ffd7895e337076", "e882f1dbd38ce3eda7c23f04dd65071eb41342ac", "df7e00dccec7ae52987d"},
    {"RFC 3610 packet vector #24", "d7828d13b2b0bdc325a76236df93cc6b", "008d493b30ae8b3c9696766cfa", "6e37a6ef546d955d34ab6059",
     "abf21c0b02feb88f856df4a37381bce3cc128517d4", "f32905b88a641b04b9c9ffb58cc390900f3da12ab1", "6dce9e82efa16da62059"},
    {"Wycheproof aes_ccm tcId 265", "694a2ae94cc2fc6c82dcd16c58a34195", "8b4de9497e78d9c73bdcb374de", "", "", "",
     "36b20bcb064609cbc03ae32786f72eb5"},
    {"Wycheproof aes_ccm tcId 266", "268c961a4fa54c214a2af8fe76a277cc", "f13443da0e412f1cc7a90165c6", "4fad12b402c58029", "", "",
     "be58071342c9348baf78104e2258e616"},
    {"Wycheproof aes_ccm tcId 267", "7c5aa1cd2fc171d5bb91ee74f31e1a63", "70a2a5303734517827faa7ee78", "",
     "4bd1d3427e0735a08f475bc73ec3648c", "9ab61214ea6611510706315f0fc9bd65", "a2b3fc3c1349cbfd9ded7cc32effb389"},
    {"Wycheproof aes_ccm tcId 268", "e74b73c2ad93d38dd4432d6e51d3e3ec", "06ee28ea532ff5aae6b0f6a28a", "10bc9864f1332e41",
     "aad5d758041e5443ede7e9bbac1db490", "d3ed6bb55d98b00e1b76938a1c6bd5ed", "22201e4eb2a42291a7d57e357082d77e"},
    {"Wycheproof aes_ccm tcId 269", "be707774d9eab370db4e8250297a7437", "7944b487d59b6ffcc96c9df62d", "",
     "28eab56885e1e12bd72def1138237f0dbc", "ee05ffeaab17bb4de94527e0297058c48b", "ff899f929b371639d48c3c447f51c577"},
    {"Wycheproof aes_ccm tcId 270", "30829416d80d4b6dd91a16c4694c5acd", "8bc7ecd5bde196b72319de6b77", "970fe65b7789a555",
     "4a3122f801d6638228fa0e30af3f36627a", "5125ed68afbd34bc00c73171ada31ee84a", "1da320c8bab525e375f37a3bba3e0eb9"},
    {"Wycheproof aes_ccm tcId 337", "a4994b65143536707b151ee6e79e69ab9c6d73000819fd2991dd28abede6b3e8",
     "ac64444972d778d52f5531ae88", "", "", "", "8f3530c8adf86ebc6c4497cede15ccd9"},
    {"Wycheproof aes_ccm tcId 338", "f99f2720f03ec3a9d0dad37e3a915bea3a11cba4bb0f60cd8f542b330163bcd0",
     "7e0f99a048b6e2879720fe4318", "94ba977e74455ed8", "", "", "cdc3bef39ea53af680199e362609dc29"},
    {"Wycheproof aes_ccm tcId 339", "505b26d166a6ebd3db69cf12bee25b73651d0d332d0fb248b50ce9a1fb3a13f1",
     "fb04f7bb3cd382cbc0893719aa", "", "ae8bb1cbc92c73e73e59a0d7a9d7f528", "4335b708f27d1c1b4d6e985f18aba7c5",
     "069ee6f5279dab73593e11440d239eb8"},
    {"Wycheproof aes_ccm tcId 340", "a6938b2e56d5dc55665956840ac690c8ac9ff421cc062fc34209f7715f2d526e",
     "ad37de72d3521546d5ff51462b", "0a3809bc563c6675", "9d286bcc115f10b2caa8c5d8daa91ec7", "4ed4dbc8aa8cf6375021d15e43c1f6c3",
     "bfba9c41ec63aa296b1446b888b6251c"},
    {"Wycheproof aes_ccm tcId 341", "45e59f4429087360b1b240fb9b591d861ad493688b0a5e8f85ffea2acff8393a",
     "20e893f4562bc1c56c32c00cc3", "", "3fbc338ad7bbd6778cabe134a02c68e53c", "67637f9c9f7da41d4b3637e3a054362b30",
     "b346d700cfec0f2d8e176d3eb9c5bec9"},
    {"Wycheproof aes_ccm tcId 342", "004c6ca04078bdfd557f915025a8ec93b5368b86caf3d657432a5e1d1cef9917",
     "999d95edb925e7744e32874009", "082c1433bb64e110", "0552fcb52c498d91b89897ae6f640e1f5a", "5b4708b72b68466639e4b5f3d3da1d1e84",
     "f7f0c0e0c01bf772cbd90cb98093dfb8"},
};

static const HmacVector kHmacSha256[] = {
    {"RFC 4231 test case 1", "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", "4869205468657265",
     "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
    {"RFC 4231 test case 2", "4a656665", "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
     "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
    {"RFC 4231 test case 3", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
     "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
     "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"},
    {"RFC 4231 test case 4", "0102030405060708090a0b0c0d0e0f10111213141516171819",
     "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd",
     "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"},
    {"RFC 4231 test case 5", "0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c", "546573742057697468205472756e636174696f6e",
     "a3b6167473100ee06e0c796c2955552b"},
    {"RFC 4231 test case 6",
     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
     "aaaaaaaaaaaaaaaa",
     "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a65204b6579202d2048617368204b6579204669727374",
     "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
    {"RFC 4231 test case 7",
     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
     "aaaaaaaaaaaaaaaa",
     "5468697320697320612074657374207573696e672061206c6172676572207468616e20626c6f636b2d73697a65206b657920616e642061206c617267657"
     "2207468616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565647320746f20626520686173686564206265666f7265206265696e"
     "6720757365642062792074686520484d414320616c676f726974686d2e",
     "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"},
};

static const X25519Vector kX25519Std[] = {
    {"RFC 7748 section 5.2 #1", "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
     "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
     "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552"},
    {"RFC 7748 section 5.2 #2", "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
     "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
     "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957"},
};

static const char *const kRfc7748Dh[] = {
    // RFC 7748 section 6.1: Alice private, Alice public, Bob private, Bob public, shared secret
    "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
    "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
    "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
    "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",
    "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
};

static const Ed25519Vector kEd25519Std[] = {
    {"RFC 8032 section 7.1 TEST 1", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7"
     "a100b"},
    {"RFC 8032 section 7.1 TEST 2", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612b"
     "b0c00"},
    {"RFC 8032 section 7.1 TEST 3", "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1"
     "ec40a"},
    {"RFC 8032 section 7.1 TEST SHA(abc)", "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
     "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54"
     "ca49f",
     "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef117733"
     "1a704"},
};

// Independent: AES-CTR / AES-CCM / SHA over pattern(len), key = seqKey, nonce and aad as in the tables
// below. CTR counter block f0 f1 ... ff; CCM nonce a0 ... ac, aad (when present) b0 ... b7.
static const uint8_t kLenCtrBlock[16] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
                                         0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};
static const uint8_t kLenCcmNonce[13] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC};
static const uint8_t kLenCcmAad[8] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7};

// Golden: wire format of the current firmware, computed independently. All numbers are little-endian
// on the wire (the nonce and AAD use native order, which is little-endian on every supported target).
// Plaintext of the golden tables is 08 01 12 04 74 65 73 74 48 00 followed by pattern(12).
static const CtrLenVector kCtrLen[] = {
    {16, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {16, 1, "3f79bb7b435b05321651daefd374cdc681dc06faa65e374e38337b88ca046dea"},
    {16, 15, "dacc587aaad4fce0431d1e7935cc004a1c0ec78533486042fbdc52ff5db9402f"},
    {16, 16, "1e9ba52cb42479205a66b3c682e21db52ab2919f7400e35fe9f6fa58d197d25a"},
    {16, 17, "dbd973164f522257e7d1810aba0a5673d2d88813978825c809640ff658ad8dbe"},
    {16, 255, "530b7402a288e6c578e1b68ec115da2a84d5f4a65beaff304b62a88947eece0a"},
    {16, 256, "cddfea1ddb3970bd322164987092a3af460523ab778bd543d2b869847b021e89"},
    {32, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {32, 1, "7da59d0dfbe21f43e842e8afb43e12a6445bbac07c2fc26984c71d0de3f99c9c"},
    {32, 15, "3d2dadca10ccaaa60b8f0f6d784afb3f32133bbce74875acb6070c8648049acd"},
    {32, 16, "862a87827c63938eafb77f79927d490fb4b865acfb295182b036caf2d928a983"},
    {32, 17, "3483186af2bf003a4f7ad1c3a60c2f7b3a15a46fd7841098cd69b20345d626e1"},
    {32, 255, "5b770e3d1ad171b642196c3c83d4cd354b8fbe706bf07ea40b0f0c9068d7c33b"},
    {32, 256, "b5ca19b6e0028b2b2d122cd2899cb2add99c4853a9aaf348045b341925a1a795"},
};

static const CcmLenVector kCcmLen[] = {
    {16, 12, 8, 0, "4ba539aaeba8dbd847127bab029b90cec7ad47b1308daabdd81435d44b632a13"},
    {16, 12, 8, 1, "d2fd085752754bad0b95d5f24dceb634472ef487c8bbb01f29ba4ddc911f2da4"},
    {16, 12, 8, 15, "ce61742e5716810e81aae776559a1f83e2a44ae23421858fc884cbf8ac1f557e"},
    {16, 12, 8, 16, "af41e46d9cd8785ce4f4f33eae7320d4ae8611f4725ac0dc06dc5320fe672a66"},
    {16, 12, 8, 17, "799af756a68442415a622b25cf6d479fd05d05d3c534797007996acbe72c55c0"},
    {16, 12, 8, 255, "bc73083b22e111b75f6716b51a2e37ea677a4013c679a2c19a0874e58747d897"},
    {16, 12, 8, 256, "1fb430aa509c1b999b634dca63e70ff6bfb2e7fefbf7a450463baa1ee5e001d2"},
    {32, 12, 8, 0, "4851bf0aa49d7ed3c6f29cdc1369abf5e9bf794d613fd028279978d6d1d576e3"},
    {32, 12, 8, 1, "3c1acaaa31736943ac2abb0c66df175166753f211dac51dd71f9396e34310226"},
    {32, 12, 8, 15, "3d9b6688c430d2feb1e307c05e815746aff55e1cdbb2c652362bef9686101f18"},
    {32, 12, 8, 16, "b5eee2ffb2cef43ec07e3d92f65242cd78840d44aedac5425ca1a74280ef7657"},
    {32, 12, 8, 17, "8ab6886a89dde8128746edcacce49e6941f3bba87548bff4a6d7545d540b59d0"},
    {32, 12, 8, 255, "32d26426dd83cc73f56770d8106557c8c739d3493ed738539b96ee3c876227bd"},
    {32, 12, 8, 256, "814b27037e0add92624a6d6ecd38cc540a1706165103c45f2cd40d5fbbe13fab"},
    {32, 8, 0, 0, "6bd798bd4faaac75012e605d10598512de1f20d1530b5f5859592d1f825bc566"},
    {32, 8, 0, 1, "d24a4357daef8efc50674c4c20386b035b5cd49afcb67d22289d34bfb1ee3d5b"},
    {32, 8, 0, 15, "e59340c8d464fc506d6feb5a427ba4a0a23dfe17cfb2d31099cfcc942a500f73"},
    {32, 8, 0, 16, "2b278a7a5d0bc5b1afc004a55dbe7de14d299a15af7f52a8e855bfedfad5fac6"},
    {32, 8, 0, 17, "1848fc6bb38f969536135a93c4935e6d9091cea73667ba395c8ec2d56ee3e7de"},
    {32, 8, 0, 255, "61d8ee9a9927b28b2fa078d0b1fd7073acb238d4643c0382319e1bc76c5d40fe"},
    {32, 8, 0, 256, "ef3753ef8dc7ab63667a0f256ea035dbd0603985f42e1090261cdf7b533cd494"},
};

static const HashLenVector kHashLen[] = {
    {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
     "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af92"
     "7da3e"},
    {1, "084fed08b978af4d7d196a7446a86b58009e636b611db16211b65a9aadff29c5",
     "e45bf5817ddf94aa2f7a407071f0eedc6beb98f768b4cd33d1176d44d1563a45a5d7212290eb7670c6786b13591aedac86478993895e8b24e612014abaa"
     "6ba04"},
    {15, "98b03249d75e642ef41f16fc71486ea85b551fc8c90324be773d3967852f790c",
     "ff6fc538a78a0808aa2bb6fd0008b0e604c286babfa1e0e66e263f509fe1b49b5de4e35f2ab937728b1111564402e190c86f5402b3194b4ce546339b614"
     "36ac4"},
    {16, "9c94926dfb94433e790f2c209e2633b2dd3e922b2741ac687e164d488d1ff67c",
     "54cfd7e31eec13a12ec5252da8a3f00616a6dff8cfd4f7fc66e70db0a01c1a4fa0ed8acc0dcccb9a6ce1fd4b67c545a9762afbc1eaebfa69e908b842545"
     "54b4c"},
    {17, "bbc485bd3e9865564c1d1fdf5cccf969c6435d86eda9256acf9bba7f5dd69eb7",
     "777a590e51e18b3d8d87d4becb91eab0969aac393f353fbc602731475e1335343aaae5297067c852c20025ef2f62c73e33812391b7903482c2c25b0fb70"
     "df33c"},
    {55, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b",
     "14fd424b1fcadee624da946ab03f7e1def7c0d6e00f689594319881a26ff30b875ba4c622ac13100c8cc784c9c2eb23159aecbb4a02e3999062f551193e"
     "2b256"},
    {56, "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27",
     "480fa85be41ef55a41208ca28ffc8743c91cf7d24758defe6f95bfb16de614fc86b701034896b047dd571de4318853d80e0809df162f1752cb26da6ddb9"
     "4a0dd"},
    {63, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055",
     "ecd42a703a4e93e163d60d55e3785b1a763838b0351bc2e6f7c94b4bfb24f9aa15da5d744ebcebe11f0fc4315d45ba3a047b6e60e07448357f2795bf34b"
     "73502"},
    {64, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241",
     "8f3cc30b3fb5bf963688a46488249248ac2c67f0f85a145233c6c1e3c16dcd1df634c07d1d31da02576f65b9cf64e1c3fdb318b689b8a14e2e9552bcf30"
     "fb133"},
    {65, "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e",
     "22050d1b2c5bd016c4b04c3e84f513ffbbbc057f83dc9ab196fee6684e5ee19d98b84c6538b3c5e1fc67dbc484e9d0a198254e41d0fa0cf5106991e1066"
     "32419"},
    {111, "67d9492e628fd376e0b2efec8ca2b99b123e202cf620deb270728df979b2f73e",
     "68cffa6d0d76f309c9ce0d35280939f8e25990c43b7b086ccdf709be35b07d4ddba599541ff2b1c19d34ea49aeafb9659adb7ac3c0b078bb30a22d57fc6"
     "687ef"},
    {112, "96b928cff8528dbb99602c709a65b846cb6467acb8b722f0d758e4dc27bfc508",
     "d0865c524d1dddf7c23b799c413f5adcd7caefd3f66a9b49750ec81066012c25a8bcf94ddea6dc525691673097ca40e0101e897fc97218cfdb0704084e2"
     "bef4b"},
    {127, "a8d23e75d936f303d248888d9b165ee543f4cbafcad3c9dd2a79bd84faa11d07",
     "e0b6a20f1c0c88970a9340152cd5a1c1ecf3d3b8de55102741879438079473540133b812706e5dbec322c8c9523b6fc8c6d16ee626e87ad5fe3d2916afe"
     "dc369"},
    {128, "d2742f1f4ac6bb7ca2b239ee18402ba8b3f9f8e652d2a72973c2b9ba11c08cf6",
     "99b16f17aa0b969a5b8f08f367719d516e330ccd2660b6f0688ec031dbc783de50a1cd185a2568dba75070a2403d17d4741d163578515dfd2ff756ddfe4"
     "d47b1"},
    {129, "307f8fc2c1622b92762e818d39a185d4d667ad49a4b07ceae1f4afa008a93ec4",
     "a1556e29185778aa5991e34b8884c840d589f0fbb4b8ed590e51e9ac4eb03a008125000db2671f8fe7f485b59a77b518670078ecb41a54b4cd02a7f1d2c"
     "a4c6d"},
    {255, "3c8af6e36699077166f180b277f93992e354c66a63a3541ef18d61524eac85e9",
     "c2e3bb67012f9eb526202efa59997933f7d3e75e7ded738818bc27d94977f4573afddb1b2793745701e62affa3b7a1c8262c992a321f488a6b1942a4795"
     "bab98"},
    {256, "d9c76fa34978cb9620dab8c3f46bbe075fddc145eb282b39009141f98d0cfe82",
     "e49c208e41556e859d1a52d14784a061c2d5ae2c8690a5360e9f9344f60861c1362a9ec05a9f08a4167b3da41bdd122a387413dd06976470e4beff5053f"
     "2ac71"},
};

// initNonce(fromNode, packetId, extraNonce): from, id, extra, expected 16-byte nonce
static const NonceVector kNonce[] = {
    {0x929, 0x13B2D662ULL, 0x0, "62d6b213000000002909000000000000"},
    {0x929, 0x1122334455667788ULL, 0x0, "88776655443322112909000000000000"},
    {0x929, 0x13B2D662ULL, 0x2B796A03, "62d6b213036a792b2909000000000000"},
    {0xAABBCCDD, 0x1122334455667788ULL, 0xCAFEBABE, "88776655bebafecaddccbbaa00000000"},
    {0xAABBCCDD, 0x200000002ULL, 0xCAFEBABE, "02000000bebafecaddccbbaa00000000"},
    {0xAABBCCDD, 0x100000002ULL, 0xCAFEBABE, "02000000bebafecaddccbbaa00000000"},
};

static const PskCtrVector kPskCtr[] = {
    {"000102030405060708090a0b0c0d0e0f", 0x929, 0x13B2D662ULL, "08011204746573744800030a11181f262d343b424950",
     "541ede13acfdce4da06a4bb5d3258459cf6b5cd6bb9b"},
    {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0x929, 0x13B2D662ULL,
     "08011204746573744800030a11181f262d343b424950", "dd48ee44cb52628117583f0f3a5a12c74f76f1ff2ded"},
    {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0xAABBCCDD, 0x1122334455667788ULL,
     "08011204746573744800030a11181f262d343b424950", "df45e4d4606372a8493cb562be0a0aad2634334997ea"},
};

static const PskCcmVector kPskCcm[] = {
    {"000102030405060708090a0b0c0d0e0f", 0x929, 0xFFFFFFFF, 0x13B2D662ULL, "08011204746573744800030a11181f262d343b424950",
     "29090000ffffffff", "ce6d310390d1afbbe9631908dbbe967909d4b13b01f12dc0403831ad5034d95fe965"},
    {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0x929, 0xFFFFFFFF, 0x13B2D662ULL,
     "08011204746573744800030a11181f262d343b424950", "29090000ffffffff",
     "3ab1cdab4d1fc725e1db6476d4f045cf17f81f75d710aec7eafe6f1e3b563aabe33a"},
    {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", 0x1020304, 0x5060708, 0x1122334455667788ULL,
     "08011204746573744800030a11181f262d343b424950", "0403020108070605",
     "63ea38c4a294db3749bd028a68e7425b2ac091135f605b7aaa656b433147a477997b"},
};

// SHA256 of the RFC 7748 section 6.1 shared secret
static const char *const kPkiSharedKey = "dead45a1d43d6902aa9240b43c0d75a0b5fc750660590d6d45461cbfc4010684";

static const PkiVector kPki[] = {
    {0x929, 0x13B2D662ULL, 0xDEADBEEF, "08011204746573744800030a11181f262d343b424950",
     "3f45db6bdbf6324109b37bb9b46b20092c50396c240ab8ceae62f03cf3a3efbeadde"},
    {0x929, 0x100000002ULL, 0xDEADBEEF, "08011204746573744800030a11181f262d343b424950",
     "abdadd045c85c0b7690dc3e4b0206ef82b797e56f1d1ae755b04d9d7dc0eefbeadde"},
    {0x929, 0x200000002ULL, 0xDEADBEEF, "08011204746573744800030a11181f262d343b424950",
     "abdadd045c85c0b7690dc3e4b0206ef82b797e56f1d1ae755b04d9d7dc0eefbeadde"},
    {0x929, 0x13B2D662ULL, 0x1, "08011204746573744800030a11181f262d343b424950",
     "0b5b615543dde548c6091edeefbf38d7f4ad939c70ced15faa09baf6994901000000"},
};

static const AckProofVector kAckProof[] = {
    {0x11223344, 0x55667788, 0x99AABBCC, "0800030a1118", "1e69f446d8f5e7686df1dbd5cc80a299bec28395f76bfe40b905cf326fafa4e9"},
    {0x11223344, 0x55667788, 0x99AABBCC, "", "07562e48ba8c8119f8666a56af614fd9403353a38f3c075ceef81645034723ab"},
};

// Golden: XEdDSA signature made by Alice (RFC 7748 section 6.1 key) over pattern(17) with from 0x929,
// id 0x13B2D662, to 0x1234, portnum 1, request_id 0xCAFE0001, reply_id 0xCAFE0002, emoji 0, bitfield 1
// (present), want_response set. Taken from the current firmware (signatures are randomized, so this
// is one sample); Python verified it as Ed25519 over the signing buffer layout and the key map of
// RFC 7748 section 4.1, which is the independent check of that layout.
static const char *const kXeddsaGoldenSig = "c7f5d48af869ccaf75b6b272b1a0c024b03e223241c030a9389db485da74dcf23198e3af6796e770b7a5"
                                            "0824d5be708cbe68dc256206b0f28949a39f9731aa01";

} // namespace crypto_vectors
