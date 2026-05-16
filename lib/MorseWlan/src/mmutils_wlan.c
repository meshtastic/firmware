#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "mmutils.h"

const char *mm_akm_suite_to_string(uint32_t akm_suite_oui)
{
    switch (akm_suite_oui) {
    case MM_AKM_SUITE_NONE:
        return "None";

    case MM_AKM_SUITE_PSK:
        return "PSK";

    case MM_AKM_SUITE_SAE:
        return "SAE";

    case MM_AKM_SUITE_OWE:
        return "OWE";

    default:
        return "Other";
    }
}

int mm_find_ie_from_offset(const uint8_t *ies, uint32_t ies_len, uint32_t search_offset, uint8_t ie_type)
{
    while ((search_offset + 2) <= ies_len) {
        uint8_t type = ies[search_offset];
        uint8_t length = ies[search_offset + 1];

        if (type == ie_type) {
            if ((search_offset + 2 + length) > ies_len) {
                return -2;
            }
            return search_offset;
        }

        search_offset += 2 + length;
    }

    return -1;
}

int mm_find_vendor_specific_ie_from_offset(const uint8_t *ies, uint32_t ies_len, uint32_t search_offset, const uint8_t *id,
                                           size_t id_len)
{
    int offset = 0;

    while ((search_offset + id_len) <= ies_len) {
        offset = mm_find_ie_from_offset(ies, ies_len, search_offset, MM_VENDOR_SPECIFIC_IE_TYPE);
        if (offset < 0) {
            return offset;
        }

        uint8_t ie_type = ies[offset];
        uint8_t ie_length = ies[offset + 1];
        const uint8_t *ie_data = ies + (offset + 2);

        if (ie_type == MM_VENDOR_SPECIFIC_IE_TYPE && id_len <= ie_length && (memcmp(id, ie_data, id_len) == 0)) {
            if (((uint32_t)offset + 2 + ie_length) > ies_len) {
                return -2;
            }
            return offset;
        }

        search_offset = 2 + ie_length + (uint32_t)offset;
    }

    return -1;
}

int mm_parse_rsn_information(const uint8_t *ies, uint32_t ies_len, struct mm_rsn_information *output)
{
    uint8_t length;
    uint16_t num_pairwise_cipher_suites;
    uint16_t num_akm_suites;
    uint16_t ii;

    int offset = mm_find_ie(ies, ies_len, MM_RSN_INFORMATION_IE_TYPE);

    memset(output, 0, sizeof(*output));

    if (offset < 0) {
        return offset;
    }

    /* Note that we rely on mm_find_ie() to validate that the IE does not extend past the end
     * of the given buffer. */

    length = ies[offset + 1];
    offset += 2;

    if (length < 8) {
        printf("*WRN* RSN IE too short\n");
        return -2;
    }

    /* Skip version field */
    output->version = ies[offset] | ies[offset + 1] << 8;
    offset += 2;
    length -= 2;

    output->group_cipher_suite = ies[offset] << 24 | ies[offset + 1] << 16 | ies[offset + 2] << 8 | ies[offset + 3];
    offset += 4;
    length -= 4;

    num_pairwise_cipher_suites = ies[offset] | ies[offset + 1] << 8;
    offset += 2;
    length -= 2;

    output->num_pairwise_cipher_suites = num_pairwise_cipher_suites;
    if (num_pairwise_cipher_suites > MM_RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES) {
        output->num_pairwise_cipher_suites = MM_RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES;
    }

    if (length < 4 * num_pairwise_cipher_suites + 2) {
        printf("*WRN* RSN IE too short\n");
        return -2;
    }

    for (ii = 0; ii < num_pairwise_cipher_suites; ii++) {
        if (ii < output->num_pairwise_cipher_suites) {
            output->pairwise_cipher_suites[ii] =
                ies[offset] << 24 | ies[offset + 1] << 16 | ies[offset + 2] << 8 | ies[offset + 3];
        }
        offset += 4;
        length -= 4;
    }

    num_akm_suites = ies[offset] | ies[offset + 1] << 8;
    offset += 2;
    length -= 2;

    output->num_akm_suites = num_akm_suites;
    if (num_akm_suites > MM_RSN_INFORMATION_MAX_AKM_SUITES) {
        output->num_akm_suites = MM_RSN_INFORMATION_MAX_AKM_SUITES;
    }

    if (length < 4 * num_akm_suites + 2) {
        printf("*WRN* RSN IE too short\n");
        return -2;
    }

    for (ii = 0; ii < num_akm_suites; ii++) {
        if (ii < output->num_akm_suites) {
            output->akm_suites[ii] = ies[offset] << 24 | ies[offset + 1] << 16 | ies[offset + 2] << 8 | ies[offset + 3];
        }
        offset += 4;
        length -= 4;
    }

    output->rsn_capabilities = ies[offset] | ies[offset + 1] << 8;
    return 0;
}

#endif /* USE_MM_IOT_ESP32 */
