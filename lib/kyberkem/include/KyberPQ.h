#pragma once
#include <cstdint>
#include <array>
extern "C" {
        #include <kem.h>
    }

namespace PQCrypto {

class Kyber {
public:
    static constexpr size_t PublicKeySize  = CRYPTO_PUBLICKEYBYTES;
    static constexpr size_t PrivateKeySize = CRYPTO_SECRETKEYBYTES;
    static constexpr size_t CipherTextSize = CRYPTO_CIPHERTEXTBYTES;
    static constexpr size_t SharedSecretSize = CRYPTO_BYTES;

    Kyber() = default;

    bool generateKeyPair(uint8_t* pk,
                         uint8_t* sk);

    bool encap(uint8_t* ct,
               uint8_t* ss,
               const uint8_t* pk);

    bool decap(uint8_t* ss,
               const uint8_t* ct,
               const uint8_t* sk);

private:
};

} // namespace PQCrypto
