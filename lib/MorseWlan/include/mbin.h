/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup MBIN Morse BINary Loader API
 *
 * This file defines the structure of the @c MBIN file.
 *
 * @{
 */

#pragma once

#ifndef PACKED
/** Macro for the compiler packed attribute */
#define PACKED __attribute__((packed))
#endif

/** Enumeration of TLV field types */
enum mbin_tlv_types {
    FIELD_TYPE_FW_TLV_BCF_ADDR = 0x0001,
    FIELD_TYPE_MAGIC = 0x8000,
    FIELD_TYPE_FW_SEGMENT = 0x8001,
    FIELD_TYPE_FW_SEGMENT_DEFLATED = 0x8002,
    FIELD_TYPE_BCF_BOARD_CONFIG = 0x8100,
    FIELD_TYPE_BCF_REGDOM = 0x8101,
    FIELD_TYPE_BCF_BOARD_DESC = 0x8102,
    FIELD_TYPE_BCF_BUILD_VER = 0x8103,
    FIELD_TYPE_SW_SEGMENT = 0x8201,
    FIELD_TYPE_SW_SEGMENT_DEFLATED = 0x8202,
    FIELD_TYPE_EOF = 0x8f00,
    FIELD_TYPE_EOF_WITH_SIGNATURE = 0x8f01,
};

/** TLV header data structure. */
struct PACKED mbin_tlv_hdr {
    /** Type (see mbin_tlv_types). */
    uint16_t type;
    /** Length of payload (excludes header). */
    uint16_t len;
};

/** Data header in a FIELD_TYPE_XX_SEGMENT field. */
struct PACKED mbin_segment_hdr {
    /** Destination base address at which the data should be loaded. */
    uint32_t base_address;
};

/** Data header in a FIELD_TYPE_XX_SEGMENT_DEFLATED field. */
struct PACKED mbin_deflated_segment_hdr {
    /** Destination base address at which the data should be loaded. */
    uint32_t base_address;
    /** Size of deflated data, infer size of compressed data from TLV length */
    uint16_t chunk_size;
    /** ZLib header */
    uint8_t zlib_header[2];
};

/** Data header in a @c FIELD_TYPE_BCF_REGDOM field. */
struct PACKED mbin_regdom_hdr {
    /** Country code that this @c regdom applies to. */
    uint8_t country_code[2];
    /** Reserved */
    uint16_t reserved;
};

/** Expected value of the magic field for a SW image @c MMSW. */
#define MBIN_SW_MAGIC_NUMBER (0x57534d4d)
/** Expected value of the magic field for a firmware image @c MMFW. */
#define MBIN_FW_MAGIC_NUMBER (0x57464d4d)
/** Expected value of the magic field for a BCF @c MMBC. */
#define MBIN_BCF_MAGIC_NUMBER (0x43424d4d)

/** @} */
