#include "KyberPQ.h"
#include <cstring> 
#include <kem.h>

namespace PQCrypto {

bool Kyber::generateKeyPair(uint8_t* pk, uint8_t* sk) {
    return crypto_kem_keypair(reinterpret_cast<uint8_t*>(pk),
                              reinterpret_cast<uint8_t*>(sk)) == 0;
}

bool Kyber::encap(uint8_t* ct, uint8_t* ss, const uint8_t* pk) {
    return crypto_kem_enc(reinterpret_cast<uint8_t*>(ct),
                          reinterpret_cast<uint8_t*>(ss),
                          reinterpret_cast<const uint8_t*>(pk)) == 0;
}

bool Kyber::decap(uint8_t* ss, const uint8_t* ct, const uint8_t* sk) {
    return crypto_kem_dec(reinterpret_cast<uint8_t*>(ss),
                          reinterpret_cast<const uint8_t*>(ct),
                          reinterpret_cast<const uint8_t*>(sk)) == 0;
}

} // namespace PQCrypto
