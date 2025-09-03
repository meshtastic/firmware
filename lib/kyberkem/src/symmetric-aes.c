#include <stddef.h>
#include <stdint.h>
#include "params.h"
#include "symmetric.h"
#include "aes256ctr.h"

#if (AES_ACC == 1)
#include "mbedtls/aes.h"
#endif

void kyber_aes256xof_absorb(aes256ctr_ctx *state, const uint8_t seed[32], uint8_t x, uint8_t y)
{
  uint8_t expnonce[12] = {0};
  expnonce[0] = x;
  expnonce[1] = y;
  aes256ctr_init(state, seed, expnonce);
}

#if (AES_ACC == 1)
void kyber_aes256ctr_prf(uint8_t *out, size_t outlen, const uint8_t key[32], uint8_t nonce)
{ 
  uint8_t expnonce[16] = {0};
  expnonce[0] = nonce;
  uint8_t stream_block[16] = {0};
  mbedtls_aes_context ctx;
  uint8_t *in = alloca(outlen);
  size_t nc_off = 0;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 256);

  mbedtls_aes_crypt_ctr(&ctx, outlen, &nc_off, expnonce, stream_block, in, out);
  mbedtls_aes_free(&ctx);
}
#else
void kyber_aes256ctr_prf(uint8_t *out, size_t outlen, const uint8_t key[32], uint8_t nonce)
{
  uint8_t expnonce[12] = {0};
  expnonce[0] = nonce;
  aes256ctr_prf(out, outlen, key, expnonce);
}
#endif //AES_ACC==1
