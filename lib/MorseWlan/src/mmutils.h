/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MMUTILS Morse Micro Utilities
 *
 * Utility macros and functions to improve quality of life.
 *
 * @{
 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the minimum of 2 values.
 *
 * Note that this macro is not ideal and should be used with caution. Caveats include:
 * * The two parameters may be evaluated more than once, so should be constant values that
 *   do not have side effects. For example, do NOT do `MM_MIN(a++, b)`.
 * * There are no explicit constraints on types, so be careful of comparing different integer
 *   types, etc.
 *
 * @param _x    The first value to compare.
 * @param _y    The second value to compare.
 *
 * @returns the minimum of @p _x and @p _y.
 */
#define MM_MIN(_x, _y) (((_x) < (_y)) ? (_x) : (_y))

/**
 * Get the maximum of 2 values.
 *
 * Note that this macro is not ideal and should be used with caution. Caveats include:
 * * The two parameters may be evaluated more than once, so should be constant values that
 *   do not have side effects. For example, do NOT do `MM_MAX(a++, b)`.
 * * There are no explicit constraints on types, so be careful of comparing different integer
 *   types, etc.
 *
 * @param _x    The first value to compare.
 * @param _y    The second value to compare.
 *
 * @returns the maximum of @p _x and @p _y.
 */
#define MM_MAX(_x, _y) (((_x) > (_y)) ? (_x) : (_y))

/**
 * Round @p x up to the next multiple of @p m (where @p m is a power of 2).
 *
 * @warning @p m must be a power of 2.
 */
#ifndef MM_FAST_ROUND_UP
#define MM_FAST_ROUND_UP(x, m) ((((x)-1) | ((m)-1)) + 1)
#endif

/** Casts the given expression to void to avoid "unused" warnings from the compiler. */
#define MM_UNUSED(_x) (void)(_x)

/** Tells the compiler to pack the structure. */
#ifndef MM_PACKED
#define MM_PACKED __attribute__((packed))
#endif

/** Used to declare a weak symbol.  */
#ifndef MM_WEAK
#define MM_WEAK __attribute__((weak))
#endif

#ifndef MM_STATIC_ASSERT
/**
 * Assertion check that is evaluated at compile time.
 *
 * The constant expression, @p _expression, is evaluted at compile time. If zero then
 * it triggers a compilation error and @p _message is displayed. If non-zero, no code
 * is emitted.
 *
 * @param _expression   Constant expression to evaluate. If zero then a compilation error
 *                      is triggered.
 * @param _message      Message to display on error.
 */
#define MM_STATIC_ASSERT(_expression, _message) _Static_assert((_expression), _message)
#endif

/**
 * Return the number of elements in the given array.
 *
 * @param _a    The array to get the element count for. Note that this must be an _array_
 *              and not a pointer. Beware that array-type function arguments are
 *              actually treated as pointers by the compiler. Must not be NULL.
 *
 * @returns the count of elements in the given array.
 */
#define MM_ARRAY_COUNT(_a) (sizeof(_a) / sizeof((_a)[0]))

/**
 * Convert the least significant 4 bits of the given argument to a character representing their
 * hexadecimal value.
 *
 * For example, for input 0xde this will return 'E', for 0x01 it will return '1'.
 *
 * @param nibble    The input nibble (upper 4 bits will be discarded).
 *
 * @return The character that represents the hexadecimal value of the lower 4 bits of @p nibble.
 *         Values greater than 0x09 will be represented with upper case characters.
 */
static inline char mm_nibble_to_hex_char(uint8_t nibble)
{
    nibble &= 0x0f;
    if (nibble < 0x0a) {
        return '0' + nibble;
    } else {
        return 'A' + nibble - 0x0a;
    }
}

/**
 * @defgroup MMUTILS_WLAN WLAN Utilities
 *
 * Utility macros and functions relating to WLAN.
 *
 * @{
 */

/** Enumeration of Authentication Key Management (AKM) Suite OUIs as BE32 integers. */
enum mm_akm_suite_oui {
    /** Open (no security) */
    MM_AKM_SUITE_NONE = 0,
    /** Pre-shared key (WFA OUI) */
    MM_AKM_SUITE_PSK = 0x506f9a02,
    /** Simultaneous Authentication of Equals (SAE) */
    MM_AKM_SUITE_SAE = 0x000fac08,
    /** OWE */
    MM_AKM_SUITE_OWE = 0x000fac12,
    /** Another suite not in this enum */
    MM_AKM_SUITE_OTHER = 1,
};

/** Enumeration of Cipher Suite OUIs as BE32 integers. */
enum mm_cipher_suite_oui {
    /** Open (no security) */
    MM_CIPHER_SUITE_AES_CCM = 0x000fac04,
    /** Another cipher suite not in this enum */
    MM_CIPHER_SUITE_OTHER = 1,
};

/** Maximum number of pairwise cipher suites our parser will process. */
#ifndef MM_RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES
#define MM_RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES (2)
#endif

/** Maximum number of AKM suites our parser will process. */
#ifndef MM_RSN_INFORMATION_MAX_AKM_SUITES
#define MM_RSN_INFORMATION_MAX_AKM_SUITES (2)
#endif

/** Tag number of the RSN information element, in which we can find security details of the AP. */
#define MM_RSN_INFORMATION_IE_TYPE (48)
/** Tag number of the Vendor Specific information element. */
#define MM_VENDOR_SPECIFIC_IE_TYPE (221)

/** Explicitly defined errno values to obviate the need to include errno.h. MM prefix to
 *  avoid namespace collision in case errno.h gets included. */
enum mm_errno {
    MM_ENOMEM = 12,
    MM_EFAULT = 14,
    MM_ENODEV = 19,
    MM_EINVAL = 22,
    MM_ETIMEDOUT = 110,
};

/**
 * Data structure to represent information extracted from an RSN information element.
 *
 * All integers in host order.
 */
struct mm_rsn_information {
    /** The group cipher suite OUI. */
    uint32_t group_cipher_suite;
    /** Pairwise cipher suite OUIs. Count given by @c num_pairwise_cipher_suites. */
    uint32_t pairwise_cipher_suites[MM_RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES];
    /** AKM suite OUIs. Count given by @c num_akm_suites. */
    uint32_t akm_suites[MM_RSN_INFORMATION_MAX_AKM_SUITES];
    /** Number of pairwise cipher suites in @c pairwise_cipher_suites. */
    uint16_t num_pairwise_cipher_suites;
    /** Number of AKM suites in @c akm_suites. */
    uint16_t num_akm_suites;
    /** Version number of the RSN IE. */
    uint16_t version;
    /** RSN Capabilities field of the RSN IE (in host order). */
    uint16_t rsn_capabilities;
};

/**
 * Get the name of the given AKM Suite as a string.
 *
 * @param akm_suite_oui     The OUI of the AKM suite as a big endian integer.
 *
 * @returns the string representation.
 */
const char *mm_akm_suite_to_string(uint32_t akm_suite_oui);

/**
 * Search a list of Information Elements (IEs) from the given starting offset and find the first
 * instance of matching the given type.
 *
 * @warning A @p search_offset that is not aligned to the start of an IE header will result in
 *          undefined behaviour.
 *
 * @param ies           Buffer containing the information elements.
 * @param ies_len       Length of @p ies
 * @param search_offset Offset to start searching from. This **must** point to a IE header.
 * @param ie_type       The type of the IE to look for.
 *
 * @return If the information element is found, the offset of the start of the IE within @p ies; if
 *         no match is found then -1; if the IE is found but is malformed then -2.
 */
int mm_find_ie_from_offset(const uint8_t *ies, uint32_t ies_len, uint32_t search_offset, uint8_t ie_type);

/**
 * Search a list of Information Elements (IEs) and find the first instance of matching the
 * given type.
 *
 * @param ies       Buffer containing the information elements.
 * @param ies_len   Length of @p ies
 * @param ie_type   The type of the IE to look for.
 *
 * @return If the information element is found, the offset of the start of the IE within @p ies;
 *         if no match is found then -1; if the IE is found but is malformed then -2.
 */
static inline int mm_find_ie(const uint8_t *ies, uint32_t ies_len, uint8_t ie_type)
{
    return mm_find_ie_from_offset(ies, ies_len, 0, ie_type);
}

/**
 * Search through the given list of Information Elements (IEs) from the given starting offset to
 * find the first Vendor Specific IE that matches the given id.
 *
 * @warning A @p search_offset that is not aligned to the start of an IE header will result in
 *          undefined behaviour.
 *
 * @param[in] ies           Buffer containing the information elements.
 * @param[in] ies_len       Length of @p ies
 * @param[in] search_offset Offset to start searching from. This **must** point to a IE header.
 * @param[in] id            Buffer containing the IE ID, usually OUI+TYPE.
 * @param[in] id_len        Length of the ID.
 *
 * @return If the information element is found, the offset of the start of the IE within @p ies; if
 *         no match is found then -1; if the IE is found but is malformed then -2.
 */
int mm_find_vendor_specific_ie_from_offset(const uint8_t *ies, uint32_t ies_len, uint32_t search_offset, const uint8_t *id,
                                           size_t id_len);

/**
 * Search through the given list of Information Elements (IEs) to find the first Vendor Specific IE
 * that matches the given id.
 *
 * @param[in] ies       Buffer containing the information elements.
 * @param[in] ies_len   Length of @p ies
 * @param[in] id        Buffer containing the IE ID, usually OUI+TYPE.
 * @param[in] id_len    Length of the ID.
 *
 * @return If the information element is found, the offset of the start of the IE within @p ies; if
 *         no match is found then -1; if the IE is found but is malformed then -2.
 */
static inline int mm_find_vendor_specific_ie(const uint8_t *ies, uint32_t ies_len, const uint8_t *id, size_t id_len)
{
    return mm_find_vendor_specific_ie_from_offset(ies, ies_len, 0, id, id_len);
}

/**
 * Search through the given list of information elements to find the RSN IE then parse it
 * to extract relevant information into an instance of @ref mm_rsn_information.
 *
 * @param[in] ies       Buffer containing the information elements.
 * @param[in] ies_len   Length of @p ies
 * @param[out] output   Pointer to an instance of @ref mm_rsn_information to receive output.
 *
 * @returns -2 on parse error, -1 if the RSN IE was not found, 0 if the RSN IE was found.
 */
int mm_parse_rsn_information(const uint8_t *ies, uint32_t ies_len, struct mm_rsn_information *output);

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
