/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file should be included from the application mbedtls_config.h file to ensure that
 * the mbedTLS features necessary for morselib functionality are enabled.
 *
 * It is recommended to include this file at the _end_ of the application mbedtls_config.h file
 * to avoid redefinition of macros.
 */

/* Cipher modes */
#ifndef MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CBC
#endif

#ifndef MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_CIPHER_MODE_CTR
#endif

/* EC curves */
#ifndef MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#endif

#ifndef MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#endif

#ifndef MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#endif

/* Features */
#ifndef MBEDTLS_AES_C
#define MBEDTLS_AES_C
#endif

#ifndef MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#endif

#ifndef MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_ASN1_WRITE_C
#endif

#ifndef MBEDTLS_BIGNUM_C
#define MBEDTLS_BIGNUM_C
#endif

#ifndef MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_C
#endif

#ifndef MBEDTLS_CMAC_C
#define MBEDTLS_CMAC_C
#endif

#ifndef MBEDTLS_CTR_DRBG_C
#define MBEDTLS_CTR_DRBG_C
#endif

#ifndef MBEDTLS_ECDH_C
#define MBEDTLS_ECDH_C
#endif

#ifndef MBEDTLS_ECP_C
#define MBEDTLS_ECP_C
#endif

#ifndef MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_C
#endif

#ifndef MBEDTLS_MD_C
#define MBEDTLS_MD_C
#endif

#ifndef MBEDTLS_NIST_KW_C
#define MBEDTLS_NIST_KW_C
#endif

#ifndef MBEDTLS_OID_C
#define MBEDTLS_OID_C
#endif

#ifndef MBEDTLS_PK_C
#define MBEDTLS_PK_C
#endif

#ifndef MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_PARSE_C
#endif

#ifndef MBEDTLS_PK_WRITE_C
#define MBEDTLS_PK_WRITE_C
#endif

#ifndef MBEDTLS_PKCS5_C
#define MBEDTLS_PKCS5_C
#endif

#ifndef MBEDTLS_SHA1_C
#define MBEDTLS_SHA1_C
#endif

#ifndef MBEDTLS_SHA224_C
#define MBEDTLS_SHA224_C
#endif

#ifndef MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_C
#endif

#ifndef MBEDTLS_SHA384_C
#define MBEDTLS_SHA384_C
#endif

#ifndef MBEDTLS_SHA512_C
#define MBEDTLS_SHA512_C
#endif
