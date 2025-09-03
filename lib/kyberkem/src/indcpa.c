#include <stddef.h>
#include <stdint.h>
#include "params.h"
#include "indcpa.h"
#include "polyvec.h"
#include "poly.h"
#include "ntt.h"
#include "symmetric.h"
#include "randombytes.h"

#if ((INDCPA_KEYPAIR_DUAL == 1) || (INDCPA_ENC_DUAL == 1) || (INDCPA_DEC_DUAL == 1))
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "taskpriorities.h"

SemaphoreHandle_t Semaphore_core_0 = NULL;
SemaphoreHandle_t Semaphore_core_1 = NULL;
SemaphoreHandle_t Semaphore_core_done = NULL;
#endif

/*************************************************
* Name:        pack_pk
*
* Description: Serialize the public key as concatenation of the
*              serialized vector of polynomials pk
*              and the public seed used to generate the matrix A.
*
* Arguments:   uint8_t *r: pointer to the output serialized public key
*              polyvec *pk: pointer to the input public-key polyvec
*              const uint8_t *seed: pointer to the input public seed
**************************************************/
static void pack_pk(uint8_t r[KYBER_INDCPA_PUBLICKEYBYTES],
                    polyvec *pk,
                    const uint8_t seed[KYBER_SYMBYTES])
{
  size_t i;
  polyvec_tobytes(r, pk);
  for(i=0;i<KYBER_SYMBYTES;i++)
    r[i+KYBER_POLYVECBYTES] = seed[i];
}

/*************************************************
* Name:        unpack_pk
*
* Description: De-serialize public key from a byte array;
*              approximate inverse of pack_pk
*
* Arguments:   - polyvec *pk: pointer to output public-key polynomial vector
*              - uint8_t *seed: pointer to output seed to generate matrix A
*              - const uint8_t *packedpk: pointer to input serialized public key
**************************************************/
static void unpack_pk(polyvec *pk,
                      uint8_t seed[KYBER_SYMBYTES],
                      const uint8_t packedpk[KYBER_INDCPA_PUBLICKEYBYTES])
{
  size_t i;
  polyvec_frombytes(pk, packedpk);
  for(i=0;i<KYBER_SYMBYTES;i++)
    seed[i] = packedpk[i+KYBER_POLYVECBYTES];
}

/*************************************************
* Name:        pack_sk
*
* Description: Serialize the secret key
*
* Arguments:   - uint8_t *r: pointer to output serialized secret key
*              - polyvec *sk: pointer to input vector of polynomials (secret key)
**************************************************/
static void pack_sk(uint8_t r[KYBER_INDCPA_SECRETKEYBYTES], polyvec *sk)
{
  polyvec_tobytes(r, sk);
}

/*************************************************
* Name:        unpack_sk
*
* Description: De-serialize the secret key; inverse of pack_sk
*
* Arguments:   - polyvec *sk: pointer to output vector of polynomials (secret key)
*              - const uint8_t *packedsk: pointer to input serialized secret key
**************************************************/
static void unpack_sk(polyvec *sk, const uint8_t packedsk[KYBER_INDCPA_SECRETKEYBYTES])
{
  polyvec_frombytes(sk, packedsk);
}

/*************************************************
* Name:        pack_ciphertext
*
* Description: Serialize the ciphertext as concatenation of the
*              compressed and serialized vector of polynomials b
*              and the compressed and serialized polynomial v
*
* Arguments:   uint8_t *r: pointer to the output serialized ciphertext
*              poly *pk: pointer to the input vector of polynomials b
*              poly *v: pointer to the input polynomial v
**************************************************/
static void pack_ciphertext(uint8_t r[KYBER_INDCPA_BYTES], polyvec *b, poly *v)
{
  polyvec_compress(r, b);
  poly_compress(r+KYBER_POLYVECCOMPRESSEDBYTES, v);
}

/*************************************************
* Name:        unpack_ciphertext
*
* Description: De-serialize and decompress ciphertext from a byte array;
*              approximate inverse of pack_ciphertext
*
* Arguments:   - polyvec *b: pointer to the output vector of polynomials b
*              - poly *v: pointer to the output polynomial v
*              - const uint8_t *c: pointer to the input serialized ciphertext
**************************************************/
static void unpack_ciphertext(polyvec *b, poly *v, const uint8_t c[KYBER_INDCPA_BYTES])
{
  polyvec_decompress(b, c);
  poly_decompress(v, c+KYBER_POLYVECCOMPRESSEDBYTES);
}

/*************************************************
* Name:        rej_uniform
*
* Description: Run rejection sampling on uniform random bytes to generate
*              uniform random integers mod q
*
* Arguments:   - int16_t *r: pointer to output buffer
*              - unsigned int len: requested number of 16-bit integers (uniform mod q)
*              - const uint8_t *buf: pointer to input buffer (assumed to be uniformly random bytes)
*              - unsigned int buflen: length of input buffer in bytes
*
* Returns number of sampled 16-bit integers (at most len)
**************************************************/
static unsigned int rej_uniform(int16_t *r,
                                unsigned int len,
                                const uint8_t *buf,
                                unsigned int buflen)
{
  unsigned int ctr, pos;
  uint16_t val0, val1;

  ctr = pos = 0;
  while(ctr < len && pos + 3 <= buflen) {
    val0 = ((buf[pos+0] >> 0) | ((uint16_t)buf[pos+1] << 8)) & 0xFFF;
    val1 = ((buf[pos+1] >> 4) | ((uint16_t)buf[pos+2] << 4)) & 0xFFF;
    pos += 3;

    if(val0 < KYBER_Q)
      r[ctr++] = val0;
    if(ctr < len && val1 < KYBER_Q)
      r[ctr++] = val1;
  }

  return ctr;
}

#define gen_a(A,B)  gen_matrix(A,B,0)
#define gen_at(A,B) gen_matrix(A,B,1)

/*************************************************
* Name:        gen_matrix
*
* Description: Deterministically generate matrix A (or the transpose of A)
*              from a seed. Entries of the matrix are polynomials that look
*              uniformly random. Performs rejection sampling on output of
*              a XOF
*
* Arguments:   - polyvec *a: pointer to ouptput matrix A
*              - const uint8_t *seed: pointer to input seed
*              - int transposed: boolean deciding whether A or A^T is generated
**************************************************/
#define GEN_MATRIX_NBLOCKS ((12*KYBER_N/8*(1 << 12)/KYBER_Q + XOF_BLOCKBYTES)/XOF_BLOCKBYTES)
// Not static for benchmarking
void gen_matrix(polyvec *a, const uint8_t seed[KYBER_SYMBYTES], int transposed)
{
  unsigned int ctr, i, j, k;
  unsigned int buflen, off;
  uint8_t buf[GEN_MATRIX_NBLOCKS*XOF_BLOCKBYTES+2];
  xof_state state;

  for(i=0;i<KYBER_K;i++) {
    for(j=0;j<KYBER_K;j++) {
      if(transposed)
        xof_absorb(&state, seed, i, j);
      else
        xof_absorb(&state, seed, j, i);

      xof_squeezeblocks(buf, GEN_MATRIX_NBLOCKS, &state);
      buflen = GEN_MATRIX_NBLOCKS*XOF_BLOCKBYTES;
      ctr = rej_uniform(a[i].vec[j].coeffs, KYBER_N, buf, buflen);

      while(ctr < KYBER_N) {
        off = buflen % 3;
        for(k = 0; k < off; k++)
          buf[k] = buf[buflen - off + k];
        xof_squeezeblocks(buf + off, 1, &state);
        buflen = off + XOF_BLOCKBYTES;
        ctr += rej_uniform(a[i].vec[j].coeffs + ctr, KYBER_N - ctr, buf, buflen);
      }
    }
  }
}

/*************************************************
* Name:        indcpa_keypair
*
* Description: Generates public and private key for the CPA-secure
*              public-key encryption scheme underlying Kyber
*
* Arguments:   - uint8_t *pk: pointer to output public key
*                             (of length KYBER_INDCPA_PUBLICKEYBYTES bytes)
*              - uint8_t *sk: pointer to output private key
                              (of length KYBER_INDCPA_SECRETKEYBYTES bytes)
**************************************************/
#if (INDCPA_KEYPAIR_DUAL == 1)

typedef struct IndcpaKeypairData_t
{
  uint8_t * pk;
  uint8_t * sk;
  uint8_t buf[2*KYBER_SYMBYTES];
  polyvec a[KYBER_K], e, pkpv, skpv;
} GenericIndcpaKeypairData_t;

TaskFunction_t indcpa_keypair_dual_0(void *xStruct) {
  GenericIndcpaKeypairData_t * data = (GenericIndcpaKeypairData_t *) xStruct;
  const uint8_t *publicseed = data->buf;
  const uint8_t *noiseseed = data->buf+KYBER_SYMBYTES;

  while(1) {
    esp_randombytes(data->buf, KYBER_SYMBYTES);
    hash_g(data->buf, data->buf, KYBER_SYMBYTES);
    xSemaphoreGive(Semaphore_core_0); //give sign that core_1 can run

    gen_a(data->a, publicseed);

    // uint8_t nonce = 0;
    // for(unsigned int i=0;i<KYBER_K;i++)
    //   poly_getnoise_eta1(&data->skpv.vec[i], noiseseed, nonce++);
    // for(unsigned int i=0;i<KYBER_K;i++)
    //   poly_getnoise_eta1(&data->e.vec[i], noiseseed, nonce++);

    //polyvec_ntt(&skpv);
    //polyvec_ntt(&e);

    xSemaphoreTake(Semaphore_core_1, portMAX_DELAY); //wait until core_1 finish

    // matrix-vector multiplication
    for(unsigned int i=0;i<KYBER_K;i++) {
      polyvec_basemul_acc_montgomery(&data->pkpv.vec[i], &data->a[i], &data->skpv);
      poly_tomont(&data->pkpv.vec[i]);
    }

    polyvec_add(&data->pkpv, &data->pkpv, &data->e);
    polyvec_reduce(&data->pkpv);

    // pack_sk(data->sk, &data->skpv);
    pack_pk(data->pk, &data->pkpv, publicseed);
    xSemaphoreGive(Semaphore_core_done); //give sign, that task is done
    vTaskDelete(NULL);    // Delete the task using the xHandle_0
  }
}

TaskFunction_t indcpa_keypair_dual_1(void *xStruct) {
  GenericIndcpaKeypairData_t * data = (GenericIndcpaKeypairData_t *) xStruct;
  const uint8_t *publicseed = data->buf;
  const uint8_t *noiseseed = data->buf+KYBER_SYMBYTES;

  while(1) {
    xSemaphoreTake(Semaphore_core_0, portMAX_DELAY); //wait until core_0 finish its job
    // esp_randombytes(data->buf, KYBER_SYMBYTES);
    // hash_g(data->buf, data->buf, KYBER_SYMBYTES);
    
    // gen_a(data->a, publicseed);

    uint8_t nonce = 0;
    for(unsigned int i=0;i<KYBER_K;i++)
      poly_getnoise_eta1(&data->skpv.vec[i], noiseseed, nonce++);
    for(unsigned int i=0;i<KYBER_K;i++)
      poly_getnoise_eta1(&data->e.vec[i], noiseseed, nonce++);

    polyvec_ntt(&data->skpv);
    polyvec_ntt(&data->e);

    xSemaphoreGive(Semaphore_core_1); //give sign core_0 can run

    // matrix-vector multiplication
    // for(unsigned int i=0;i<KYBER_K;i++) {
    //   polyvec_basemul_acc_montgomery(&data->pkpv.vec[i], &data->a[i], &data->skpv);
    //   poly_tomont(&data->pkpv.vec[i]);
    // }

    // polyvec_add(&data->pkpv, &data->pkpv, &data->e);
    // polyvec_reduce(&data->pkpv);

    pack_sk(data->sk, &data->skpv);
    // pack_pk(data->pk, &data->pkpv, publicseed);
    xSemaphoreGive(Semaphore_core_done); //give sign, that task is done
    vTaskDelete(NULL);    // Delete the task using the xHandle_1
  }
}

void indcpa_keypair(uint8_t pk[KYBER_INDCPA_PUBLICKEYBYTES],
                    uint8_t sk[KYBER_INDCPA_SECRETKEYBYTES])
{
  Semaphore_core_0 = xSemaphoreCreateCounting(1, 0);
  Semaphore_core_1 = xSemaphoreCreateCounting(1, 0);
  Semaphore_core_done = xSemaphoreCreateCounting(2, 0);

  GenericIndcpaKeypairData_t xStruct = { .pk = pk, .sk = sk};

  TaskHandle_t xHandle_0 = NULL;
  TaskHandle_t xHandle_1 = NULL;
  BaseType_t xReturned_0;
  BaseType_t xReturned_1;

  xReturned_0 = xTaskCreatePinnedToCore(
                  indcpa_keypair_dual_0,       /* Function that implements the task. */
                  "indcpa_keypair_dual_0",          /* Text name for the task. */
                  20000,      /* Stack size in words, not bytes. */
                  ( void * ) &xStruct,    /* Parameter passed into the task. */
                  INDCPA_SUBTASK_PRIORITY, /* Priority at which the task is created. */
                  &xHandle_0, /* Used to pass out the created task's handle. */
                  (BaseType_t) 0); /* Core ID */ 

  xReturned_1 = xTaskCreatePinnedToCore(
                  indcpa_keypair_dual_1,       /* Function that implements the task. */
                  "indcpa_keypair_dual_1",          /* Text name for the task. */
                  20000,      /* Stack size in words, not bytes. */
                  ( void * ) &xStruct,    /* Parameter passed into the task. */
                  INDCPA_SUBTASK_PRIORITY, /* Priority at which the task is created. */
                  &xHandle_1, /* Used to pass out the created task's handle. */
                  (BaseType_t) 1); /* Core ID */    

  xSemaphoreTake(Semaphore_core_done, portMAX_DELAY); //wait until both tasks finish
  xSemaphoreTake(Semaphore_core_done, portMAX_DELAY); //wait until both tasks finish

  vSemaphoreDelete(Semaphore_core_0);
  vSemaphoreDelete(Semaphore_core_1);
  vSemaphoreDelete(Semaphore_core_done);

  // for(unsigned int i = 0; i<KYBER_INDCPA_PUBLICKEYBYTES; i++) {
  //   printf("%u ",pk[i]);
  //   if (!((i+1) % 8)) printf("\n");   
  // }
}
#else
void indcpa_keypair(uint8_t pk[KYBER_INDCPA_PUBLICKEYBYTES],
                    uint8_t sk[KYBER_INDCPA_SECRETKEYBYTES])
{
  unsigned int i;
  uint8_t buf[2*KYBER_SYMBYTES];
  const uint8_t *publicseed = buf;
  const uint8_t *noiseseed = buf+KYBER_SYMBYTES;
  uint8_t nonce = 0;
  polyvec a[KYBER_K], e, pkpv, skpv;

  esp_randombytes(buf, KYBER_SYMBYTES);
  hash_g(buf, buf, KYBER_SYMBYTES);
  
  gen_a(a, publicseed);

  for(i=0;i<KYBER_K;i++)
    poly_getnoise_eta1(&skpv.vec[i], noiseseed, nonce++);
  for(i=0;i<KYBER_K;i++)
    poly_getnoise_eta1(&e.vec[i], noiseseed, nonce++);

  polyvec_ntt(&skpv);
  polyvec_ntt(&e);

  // matrix-vector multiplication
  for(i=0;i<KYBER_K;i++) {
    polyvec_basemul_acc_montgomery(&pkpv.vec[i], &a[i], &skpv);
    poly_tomont(&pkpv.vec[i]);
  }

  polyvec_add(&pkpv, &pkpv, &e);
  polyvec_reduce(&pkpv);

  pack_sk(sk, &skpv);
  pack_pk(pk, &pkpv, publicseed);
}
#endif

/*************************************************
* Name:        indcpa_enc
*
* Description: Encryption function of the CPA-secure
*              public-key encryption scheme underlying Kyber.
*
* Arguments:   - uint8_t *c: pointer to output ciphertext
*                            (of length KYBER_INDCPA_BYTES bytes)
*              - const uint8_t *m: pointer to input message
*                                  (of length KYBER_INDCPA_MSGBYTES bytes)
*              - const uint8_t *pk: pointer to input public key
*                                   (of length KYBER_INDCPA_PUBLICKEYBYTES)
*              - const uint8_t *coins: pointer to input random coins used as seed
*                                      (of length KYBER_SYMBYTES) to deterministically
*                                      generate all randomness
**************************************************/
#if (INDCPA_ENC_DUAL == 1)
typedef struct IndcpaEncData_t
{
  uint8_t * c;
  const uint8_t *m;
  const uint8_t *pk;
  const uint8_t *coins;
  uint8_t seed[KYBER_SYMBYTES];
  polyvec sp, pkpv, ep, at[KYBER_K], b;
  poly v, k, epp;
} GenericIndcpaEncData_t;

TaskFunction_t indcpa_enc_dual_0(void *xStruct) {
  GenericIndcpaEncData_t * data = (GenericIndcpaEncData_t *) xStruct;
  while(1) {
    unpack_pk(&data->pkpv, data->seed, data->pk);

    xSemaphoreGive(Semaphore_core_0);
    gen_at(data->at, data->seed);

    // uint8_t nonce = 0;
    // for(unsigned int i=0;i<KYBER_K;i++)
    //   poly_getnoise_eta1(data->sp.vec+i, data->coins, nonce++);
    // polyvec_ntt(&data->sp);

    xSemaphoreTake(Semaphore_core_1, 1);

    // matrix-vector multiplication
    for(unsigned int i=0;i<KYBER_K;i++)
      polyvec_basemul_acc_montgomery(&data->b.vec[i], &data->at[i], &data->sp);

    polyvec_invntt_tomont(&data->b);

    // for(unsigned int i=0;i<KYBER_K;i++)
    //   poly_getnoise_eta2(data->ep.vec+i, data->coins, nonce++);

    xSemaphoreTake(Semaphore_core_1, 1);

    polyvec_add(&data->b, &data->b, &data->ep);
    polyvec_reduce(&data->b);


    // poly_getnoise_eta2(&data->epp, data->coins, nonce++);

    // poly_frommsg(&data->k, data->m);
    // poly_add(&data->epp, &data->epp, &data->k);

    // polyvec_basemul_acc_montgomery(&data->v, &data->pkpv, &data->sp);
    // poly_invntt_tomont(&data->v);

    // poly_add(&data->v, &data->v, &data->epp);
    // poly_reduce(&data->v);

    xSemaphoreTake(Semaphore_core_1, 1);

    pack_ciphertext(data->c, &data->b, &data->v);
    
    xSemaphoreGive(Semaphore_core_done); //give sign, that task is done
    vTaskDelete(NULL);    // Delete the task using the xHandle_1
  }
}

TaskFunction_t indcpa_enc_dual_1(void *xStruct) {
  GenericIndcpaEncData_t * data = (GenericIndcpaEncData_t *) xStruct;
  while(1) {
    uint8_t nonce = 0;
    for(unsigned int i=0;i<KYBER_K;i++)
      poly_getnoise_eta1(data->sp.vec+i, data->coins, nonce++);
    polyvec_ntt(&data->sp);

    xSemaphoreGive(Semaphore_core_1);
    
    for(unsigned int i=0;i<KYBER_K;i++)
      poly_getnoise_eta2(data->ep.vec+i, data->coins, nonce++);

    xSemaphoreGive(Semaphore_core_1);

    poly_getnoise_eta2(&data->epp, data->coins, nonce++);

    poly_frommsg(&data->k, data->m);
    poly_add(&data->epp, &data->epp, &data->k);

    // unpack_pk(&data->pkpv, data->seed, data->pk);
    // gen_at(data->at, data->seed);

    xSemaphoreTake(Semaphore_core_0, portMAX_DELAY);

    polyvec_basemul_acc_montgomery(&data->v, &data->pkpv, &data->sp);
    poly_invntt_tomont(&data->v);
    
    poly_add(&data->v, &data->v, &data->epp);
    
    poly_reduce(&data->v);

    xSemaphoreGive(Semaphore_core_1);

    // matrix-vector multiplication
    // for(unsigned int i=0;i<KYBER_K;i++)
    //   polyvec_basemul_acc_montgomery(&data->b.vec[i], &data->at[i], &data->sp);

    // polyvec_invntt_tomont(&data->b);

    // polyvec_add(&data->b, &data->b, &data->ep);
    // polyvec_reduce(&data->b);
    
    // pack_ciphertext(data->c, &data->b, &data->v);
    
    xSemaphoreGive(Semaphore_core_done);
    vTaskDelete(NULL);    
  }
}

void indcpa_enc(uint8_t c[KYBER_INDCPA_BYTES],
                const uint8_t m[KYBER_INDCPA_MSGBYTES],
                const uint8_t pk[KYBER_INDCPA_PUBLICKEYBYTES],
                const uint8_t coins[KYBER_SYMBYTES])
{
  GenericIndcpaEncData_t xStruct = { .c = c, .m = m, .pk = pk, .coins = coins };

  Semaphore_core_0 = xSemaphoreCreateCounting(1, 0);
  Semaphore_core_1 = xSemaphoreCreateCounting(3, 0);
  Semaphore_core_done = xSemaphoreCreateCounting(2, 0);

  TaskHandle_t xHandle_0 = NULL;
  TaskHandle_t xHandle_1 = NULL;
  BaseType_t xReturned_0;
  BaseType_t xReturned_1;

  xReturned_0 = xTaskCreatePinnedToCore(
                  indcpa_enc_dual_0,       /* Function that implements the task. */
                  "indcpa_enc_dual_0",          /* Text name for the task. */
                  20000,      /* Stack size in words, not bytes. */
                  ( void * ) &xStruct,    /* Parameter passed into the task. */
                  INDCPA_SUBTASK_PRIORITY, /* Priority at which the task is created. */
                  &xHandle_0, /* Used to pass out the created task's handle. */
                  (BaseType_t) 0); /* Core ID */ 

  xReturned_1 = xTaskCreatePinnedToCore(
                  indcpa_enc_dual_1,       /* Function that implements the task. */
                  "indcpa_enc_dual_1",          /* Text name for the task. */
                  20000,      /* Stack size in words, not bytes. */
                  ( void * ) &xStruct,    /* Parameter passed into the task. */
                  INDCPA_SUBTASK_PRIORITY, /* Priority at which the task is created. */
                  &xHandle_1, /* Used to pass out the created task's handle. */
                  (BaseType_t) 1); /* Core ID */    

  xSemaphoreTake(Semaphore_core_done, portMAX_DELAY); //wait until both tasks finish
  xSemaphoreTake(Semaphore_core_done, portMAX_DELAY); //wait until both tasks finish

  vSemaphoreDelete(Semaphore_core_0);
  vSemaphoreDelete(Semaphore_core_1);
  vSemaphoreDelete(Semaphore_core_done);
}
#else
void indcpa_enc(uint8_t c[KYBER_INDCPA_BYTES],
                const uint8_t m[KYBER_INDCPA_MSGBYTES],
                const uint8_t pk[KYBER_INDCPA_PUBLICKEYBYTES],
                const uint8_t coins[KYBER_SYMBYTES])
{
  unsigned int i;
  uint8_t seed[KYBER_SYMBYTES];
  uint8_t nonce = 0;
  polyvec sp, pkpv, ep, at[KYBER_K], b;
  poly v, k, epp;

  unpack_pk(&pkpv, seed, pk);
  poly_frommsg(&k, m);
  gen_at(at, seed);

  for(i=0;i<KYBER_K;i++)
    poly_getnoise_eta1(sp.vec+i, coins, nonce++);
  for(i=0;i<KYBER_K;i++)
    poly_getnoise_eta2(ep.vec+i, coins, nonce++);
  poly_getnoise_eta2(&epp, coins, nonce++);

  polyvec_ntt(&sp);

  // matrix-vector multiplication
  for(i=0;i<KYBER_K;i++)
    polyvec_basemul_acc_montgomery(&b.vec[i], &at[i], &sp);

  polyvec_basemul_acc_montgomery(&v, &pkpv, &sp);

  polyvec_invntt_tomont(&b);
  poly_invntt_tomont(&v);

  polyvec_add(&b, &b, &ep);
  poly_add(&v, &v, &epp);
  poly_add(&v, &v, &k);
  polyvec_reduce(&b);
  poly_reduce(&v);

  pack_ciphertext(c, &b, &v);
}
#endif

/*************************************************
* Name:        indcpa_dec
*
* Description: Decryption function of the CPA-secure
*              public-key encryption scheme underlying Kyber.
*
* Arguments:   - uint8_t *m: pointer to output decrypted message
*                            (of length KYBER_INDCPA_MSGBYTES)
*              - const uint8_t *c: pointer to input ciphertext
*                                  (of length KYBER_INDCPA_BYTES)
*              - const uint8_t *sk: pointer to input secret key
*                                   (of length KYBER_INDCPA_SECRETKEYBYTES)
**************************************************/
#if (INDCPA_DEC_DUAL == 1)
typedef struct IndcpaDecData_t
{
  uint8_t * m;
  const uint8_t *c;
  const uint8_t *sk;
  
  polyvec b, skpv;
  poly v, mp;
} GenericIndcpaDecData_t;

TaskFunction_t indcpa_dec_dual_0(void *xStruct) {
  GenericIndcpaDecData_t * data = (GenericIndcpaDecData_t *) xStruct;
  while(1) {
    unpack_sk(&data->skpv, data->sk);
    poly_decompress(&data->v, data->c+KYBER_POLYVECCOMPRESSEDBYTES);

    //polyvec_decompress(&data->b, data->c);
    //polyvec_ntt(&data->b);
    xSemaphoreTake(Semaphore_core_1, portMAX_DELAY);
    
    polyvec_basemul_acc_montgomery(&data->mp, &data->skpv, &data->b);
    poly_invntt_tomont(&data->mp);

    poly_sub(&data->mp, &data->v, &data->mp);
    poly_reduce(&data->mp);

    poly_tomsg(data->m, &data->mp);

    xSemaphoreGive(Semaphore_core_done);
    vTaskDelete(NULL);    
  }
}

TaskFunction_t indcpa_dec_dual_1(void *xStruct) {
  GenericIndcpaDecData_t * data = (GenericIndcpaDecData_t *) xStruct;
  while(1) {
    polyvec_decompress(&data->b, data->c);
    polyvec_ntt(&data->b);

    //unpack_sk(&data->skpv, data->sk);
    xSemaphoreGive(Semaphore_core_1);
    
    // polyvec_basemul_acc_montgomery(&data->mp, &data->skpv, &data->b);
    // poly_invntt_tomont(&data->mp);

    // poly_sub(&data->mp, &data->v, &data->mp);
    // poly_reduce(&data->mp);

    // poly_tomsg(data->m, &data->mp);

    xSemaphoreGive(Semaphore_core_done);
    vTaskDelete(NULL);    
  }
}

void indcpa_dec(uint8_t m[KYBER_INDCPA_MSGBYTES],
                const uint8_t c[KYBER_INDCPA_BYTES],
                const uint8_t sk[KYBER_INDCPA_SECRETKEYBYTES])
{
  GenericIndcpaDecData_t xStruct = {.m = m, .c = c, .sk = sk};
  Semaphore_core_0 = xSemaphoreCreateCounting(1, 0);
  Semaphore_core_1 = xSemaphoreCreateCounting(1, 0);
  Semaphore_core_done = xSemaphoreCreateCounting(2, 0);

  TaskHandle_t xHandle_0 = NULL;
  TaskHandle_t xHandle_1 = NULL;
  BaseType_t xReturned_0;
  BaseType_t xReturned_1;

  xReturned_0 = xTaskCreatePinnedToCore(
                  indcpa_dec_dual_0,       /* Function that implements the task. */
                  "indcpa_dec_dual_0",          /* Text name for the task. */
                  20000,      /* Stack size in words, not bytes. */
                  ( void * ) &xStruct,    /* Parameter passed into the task. */
                  INDCPA_SUBTASK_PRIORITY, /* Priority at which the task is created. */
                  &xHandle_0, /* Used to pass out the created task's handle. */
                  (BaseType_t) 0); /* Core ID */ 

  xReturned_1 = xTaskCreatePinnedToCore(
                  indcpa_dec_dual_1,       /* Function that implements the task. */
                  "indcpa_dec_dual_1",          /* Text name for the task. */
                  20000,      /* Stack size in words, not bytes. */
                  ( void * ) &xStruct,    /* Parameter passed into the task. */
                  INDCPA_SUBTASK_PRIORITY, /* Priority at which the task is created. */
                  &xHandle_1, /* Used to pass out the created task's handle. */
                  (BaseType_t) 1); /* Core ID */    

  xSemaphoreTake(Semaphore_core_done, portMAX_DELAY); //wait until both tasks finish
  xSemaphoreTake(Semaphore_core_done, portMAX_DELAY); //wait until both tasks finish

  vSemaphoreDelete(Semaphore_core_0);
  vSemaphoreDelete(Semaphore_core_1);
  vSemaphoreDelete(Semaphore_core_done);
}
#else
void indcpa_dec(uint8_t m[KYBER_INDCPA_MSGBYTES],
                const uint8_t c[KYBER_INDCPA_BYTES],
                const uint8_t sk[KYBER_INDCPA_SECRETKEYBYTES])
{
  polyvec b, skpv;
  poly v, mp;

  unpack_ciphertext(&b, &v, c);
  unpack_sk(&skpv, sk);

  polyvec_ntt(&b);
  polyvec_basemul_acc_montgomery(&mp, &skpv, &b);
  poly_invntt_tomont(&mp);

  poly_sub(&mp, &v, &mp);
  poly_reduce(&mp);

  poly_tomsg(m, &mp);
}
#endif