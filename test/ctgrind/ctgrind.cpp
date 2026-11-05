// ctgrind-style constant-time check: secrets are marked undefined for valgrind memcheck, so any
// branch or memory address that depends on them is reported. Usage: ctgrind <operation>.
// Built and run by bin/crypto-ctgrind.sh.
#include "crypto/crypto_api.h"
#include <stdio.h>
#include <string.h>
#include <valgrind/memcheck.h>

static void secret(void *p, size_t n)
{
    VALGRIND_MAKE_MEM_UNDEFINED(p, n);
}

static void publish(void *p, size_t n)
{
    VALGRIND_MAKE_MEM_DEFINED(p, n);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const char *op = argv[1];
    uint8_t key[32], in[64], out[64], tag[16], nonce[13], ctr[16], point[32], sig[64], pub[32];
    for (size_t i = 0; i < sizeof(key); i++)
        key[i] = (uint8_t)(i * 7 + 1);
    memset(in, 0x5a, sizeof(in));
    memset(nonce, 3, sizeof(nonce));
    memset(ctr, 4, sizeof(ctr));
    memset(point, 9, 1);
    point[0] = 9;
    memset(point + 1, 0, 31);
    crypto_x25519(pub, key, nullptr);
    crypto_part part = {in, sizeof(in)};

    if (!strcmp(op, "aes128_ctr")) {
        secret(key, 16);
        crypto_aes_ctr(key, 16, ctr, in, sizeof(in), out);
    } else if (!strcmp(op, "aes256_ctr")) {
        secret(key, 32);
        crypto_aes_ctr(key, 32, ctr, in, sizeof(in), out);
    } else if (!strcmp(op, "aes256_ccm_seal")) {
        secret(key, 32);
        crypto_aes_ccm_seal(key, 32, nonce, nullptr, 0, in, sizeof(in), out, tag, 8);
    } else if (!strcmp(op, "hmac_sha256")) {
        secret(key, 32);
        crypto_hmac_sha256(key, 32, &part, 1, out);
    } else if (!strcmp(op, "x25519")) {
        secret(key, 32);
        crypto_x25519(out, key, pub);
    } else if (!strcmp(op, "x25519_base")) {
        secret(key, 32);
        crypto_x25519(out, key, nullptr);
    } else if (!strcmp(op, "ed25519_basemul")) {
        key[31] &= 0x0f;
        secret(key, 32);
        crypto_ed25519_basemul(out, key);
    } else if (!strcmp(op, "xeddsa_sign")) {
        uint8_t edPriv[32], edPub[32];
        crypto_xeddsa_keys_from_x25519(edPriv, edPub, key);
        secret(edPriv, 32);
        crypto_xeddsa_sign(sig, edPriv, edPub, in, 32);
    } else {
        fprintf(stderr, "unknown operation %s\n", op);
        return 2;
    }
    publish(out, sizeof(out));
    return 0;
}
