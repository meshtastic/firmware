/*
 * Copyright 2022-2024 Morse Micro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <stdint.h>

/*
 * ----
 * Endianness operations
 * ----
 */

#ifdef __big_endian__
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

/* Protocol families.  */
#define PF_INET 2   /* IP protocol family. */
#define PF_INET6 10 /* IP version 6. */

/* Address families. */
#define AF_INET PF_INET
#define AF_INET6 PF_INET6

/*
 * ----
 * Type definitions
 * ----
 */
typedef signed char __s8;
typedef unsigned char __u8;
typedef signed short __s16;
typedef unsigned short __u16;
typedef signed int __s32;
typedef unsigned int __u32;

struct in_addr {
    __u32 s_addr;
};

struct in6_addr {
    union {
        __u32 u32_addr[4];
        __u8 u8_addr[16];
    } un;
#define s6_addr un.u8_addr
};

/* Stub function declaration for inet_ntop which is called from write_ipv4_info
 * function in robust_av.c. Since they are not used we will create a dummy
 * function declarion here.
 */
const char *inet_ntop(int __af, const void *__cp, char *__buf, int __len);

/* Rename crypto functions to match symbol name mangling in morselib for avoidance of namespace
 * collisions. */
#define aes_decrypt mmint_aes_decrypt
#define aes_decrypt_deinit mmint_aes_decrypt_deinit
#define aes_decrypt_init mmint_aes_decrypt_init
#define crypto_bignum_add mmint_crypto_bignum_add
#define crypto_bignum_addmod mmint_crypto_bignum_addmod
#define crypto_bignum_cmp mmint_crypto_bignum_cmp
#define crypto_bignum_deinit mmint_crypto_bignum_deinit
#define crypto_bignum_div mmint_crypto_bignum_div
#define crypto_bignum_exptmod mmint_crypto_bignum_exptmod
#define crypto_bignum_init mmint_crypto_bignum_init
#define crypto_bignum_init_set mmint_crypto_bignum_init_set
#define crypto_bignum_init_uint mmint_crypto_bignum_init_uint
#define crypto_bignum_inverse mmint_crypto_bignum_inverse
#define crypto_bignum_is_odd mmint_crypto_bignum_is_odd
#define crypto_bignum_is_one mmint_crypto_bignum_is_one
#define crypto_bignum_is_zero mmint_crypto_bignum_is_zero
#define crypto_bignum_legendre mmint_crypto_bignum_legendre
#define crypto_bignum_mod mmint_crypto_bignum_mod
#define crypto_bignum_mulmod mmint_crypto_bignum_mulmod
#define crypto_bignum_rand mmint_crypto_bignum_rand
#define crypto_bignum_rshift mmint_crypto_bignum_rshift
#define crypto_bignum_sqrmod mmint_crypto_bignum_sqrmod
#define crypto_bignum_sub mmint_crypto_bignum_sub
#define crypto_bignum_to_bin mmint_crypto_bignum_to_bin
#define crypto_ec_deinit mmint_crypto_ec_deinit
#define crypto_ec_get_a mmint_crypto_ec_get_a
#define crypto_ec_get_b mmint_crypto_ec_get_b
#define crypto_ec_get_generator mmint_crypto_ec_get_generator
#define crypto_ec_get_order mmint_crypto_ec_get_order
#define crypto_ec_get_prime mmint_crypto_ec_get_prime
#define crypto_ec_init mmint_crypto_ec_init
#define crypto_ec_order_len mmint_crypto_ec_order_len
#define crypto_ec_point_add mmint_crypto_ec_point_add
#define crypto_ec_point_cmp mmint_crypto_ec_point_cmp
#define crypto_ec_point_x mmint_crypto_ec_point_x
#define crypto_ec_point_compute_y_sqr mmint_crypto_ec_point_compute_y_sqr
#define crypto_ec_point_deinit mmint_crypto_ec_point_deinit
#define crypto_ec_point_from_bin mmint_crypto_ec_point_from_bin
#define crypto_ec_point_init mmint_crypto_ec_point_init
#define crypto_ec_point_invert mmint_crypto_ec_point_invert
#define crypto_ec_point_is_at_infinity mmint_crypto_ec_point_is_at_infinity
#define crypto_ec_point_is_on_curve mmint_crypto_ec_point_is_on_curve
#define crypto_ec_point_mul mmint_crypto_ec_point_mul
#define crypto_ec_point_to_bin mmint_crypto_ec_point_to_bin
#define crypto_ec_prime_len mmint_crypto_ec_prime_len
#define crypto_ec_prime_len_bits mmint_crypto_ec_prime_len_bits
#define crypto_ecdh_deinit mmint_crypto_ecdh_deinit
#define crypto_ecdh_get_pubkey mmint_crypto_ecdh_get_pubkey
#define crypto_ecdh_init mmint_crypto_ecdh_init
#define crypto_ecdh_init2 mmint_crypto_ecdh_init2
#define crypto_ecdh_set_peerkey mmint_crypto_ecdh_set_peerkey
#define crypto_ecdh_prime_len mmint_crypto_ecdh_prime_len
#define crypto_get_random mmint_crypto_get_random
#define crypto_unload mmint_crypto_unload
#define hmac_md5 mmint_hmac_md5
#define hmac_sha1 mmint_hmac_sha1
#define hmac_sha1_vector mmint_hmac_sha1_vector
#define hmac_sha256 mmint_hmac_sha256
#define hmac_sha256_vector mmint_hmac_sha256_vector
#define hmac_sha384 mmint_hmac_sha384
#define hmac_sha384_vector mmint_hmac_sha384_vector
#define hmac_sha512 mmint_hmac_sha512
#define hmac_sha512_vector mmint_hmac_sha512_vector
#define omac1_aes_vector mmint_omac1_aes_vector
#define omac1_aes_128 mmint_omac1_aes_128
#define pbkdf2_sha1 mmint_pbkdf2_sha1
#define sha1_prf mmint_sha1_prf
#define sha1_vector mmint_sha1_vector
#define sha256_prf mmint_sha256_prf
#define sha256_prf_bits mmint_sha256_prf_bits
#define sha256_vector mmint_sha256_vector
#define sha384_prf mmint_sha384_prf
#define sha384_vector mmint_sha384_vector
#define sha512_prf mmint_sha512_prf
#define sha512_vector mmint_sha512_vector

#define wpabuf_alloc mmint_wpabuf_alloc
#define wpabuf_alloc_copy mmint_wpabuf_alloc_copy
#define wpabuf_clear_free mmint_wpabuf_clear_free
#define wpabuf_put mmint_wpabuf_put
