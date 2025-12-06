#include "cobs.h"
#include <stdlib.h>

#ifdef SENSECAP_INDICATOR

cobs_encode_result cobs_encode(uint8_t *dst_buf_ptr, size_t dst_buf_len, const uint8_t *src_ptr, size_t src_len)
{

    cobs_encode_result result = {0, COBS_ENCODE_OK};

    if (!dst_buf_ptr || !src_ptr) {
        result.status = COBS_ENCODE_NULL_POINTER;
        return result;
    }

    const uint8_t *src_read_ptr = src_ptr;
    const uint8_t *src_end_ptr = src_read_ptr + src_len;
    uint8_t *dst_buf_start_ptr = dst_buf_ptr;
    uint8_t *dst_buf_end_ptr = dst_buf_start_ptr + dst_buf_len;
    uint8_t *dst_code_write_ptr = dst_buf_ptr;
    uint8_t *dst_write_ptr = dst_code_write_ptr + 1;
    uint8_t search_len = 1;

    if (src_len != 0) {
        for (;;) {
            if (dst_write_ptr >= dst_buf_end_ptr) {
                result.status = (cobs_encode_status)(result.status | (cobs_encode_status)COBS_ENCODE_OUT_BUFFER_OVERFLOW);
                break;
            }

            uint8_t src_byte = *src_read_ptr++;
            if (src_byte == 0) {
                *dst_code_write_ptr = search_len;
                dst_code_write_ptr = dst_write_ptr++;
                search_len = 1;
                if (src_read_ptr >= src_end_ptr) {
                    break;
                }
            } else {
                *dst_write_ptr++ = src_byte;
                search_len++;
                if (src_read_ptr >= src_end_ptr) {
                    break;
                }
                if (search_len == 0xFF) {
                    *dst_code_write_ptr = search_len;
                    dst_code_write_ptr = dst_write_ptr++;
                    search_len = 1;
                }
            }
        }
    }

    if (dst_code_write_ptr >= dst_buf_end_ptr) {
        result.status = (cobs_encode_status)(result.status | (cobs_encode_status)COBS_ENCODE_OUT_BUFFER_OVERFLOW);
        dst_write_ptr = dst_buf_end_ptr;
    } else {
        *dst_code_write_ptr = search_len;
    }

    result.out_len = dst_write_ptr - dst_buf_start_ptr;

    return result;
}

cobs_decode_result cobs_decode(uint8_t *dst_buf_ptr, size_t dst_buf_len, const uint8_t *src_ptr, size_t src_len)
{
    cobs_decode_result result = {0, COBS_DECODE_OK};

    if (!dst_buf_ptr || !src_ptr) {
        result.status = COBS_DECODE_NULL_POINTER;
        return result;
    }

    const uint8_t *src_read_ptr = src_ptr;
    const uint8_t *src_end_ptr = src_read_ptr + src_len;
    uint8_t *dst_buf_start_ptr = dst_buf_ptr;
    const uint8_t *dst_buf_end_ptr = dst_buf_start_ptr + dst_buf_len;
    uint8_t *dst_write_ptr = dst_buf_ptr;

    if (src_len != 0) {
        for (;;) {
            uint8_t len_code = *src_read_ptr++;
            if (len_code == 0) {
                result.status = (cobs_decode_status)(result.status | (cobs_decode_status)COBS_DECODE_ZERO_BYTE_IN_INPUT);
                break;
            }
            len_code--;

            size_t remaining_bytes = src_end_ptr - src_read_ptr;
            if (len_code > remaining_bytes) {
                result.status = (cobs_decode_status)(result.status | (cobs_decode_status)COBS_DECODE_INPUT_TOO_SHORT);
                len_code = remaining_bytes;
            }

            remaining_bytes = dst_buf_end_ptr - dst_write_ptr;
            if (len_code > remaining_bytes) {
                result.status = (cobs_decode_status)(result.status | (cobs_decode_status)COBS_DECODE_OUT_BUFFER_OVERFLOW);
                len_code = remaining_bytes;
            }

            for (uint8_t i = len_code; i != 0; i--) {
                uint8_t src_byte = *src_read_ptr++;
                if (src_byte == 0) {
                    result.status = (cobs_decode_status)(result.status | (cobs_decode_status)COBS_DECODE_ZERO_BYTE_IN_INPUT);
                }
                *dst_write_ptr++ = src_byte;
            }

            if (src_read_ptr >= src_end_ptr) {
                break;
            }

            if (len_code != 0xFE) {
                if (dst_write_ptr >= dst_buf_end_ptr) {
                    result.status = (cobs_decode_status)(result.status | (cobs_decode_status)COBS_DECODE_OUT_BUFFER_OVERFLOW);
                    break;
                }
                *dst_write_ptr++ = 0;
            }
        }
    }

    result.out_len = dst_write_ptr - dst_buf_start_ptr;

    return result;
}

#endif