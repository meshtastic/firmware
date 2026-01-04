#ifndef COBS_H_
#define COBS_H_

#include "configuration.h"

#ifdef SENSECAP_INDICATOR

#include <stdint.h>
#include <stdlib.h>

#define COBS_ENCODE_DST_BUF_LEN_MAX(SRC_LEN) ((SRC_LEN) + (((SRC_LEN) + 253u) / 254u))
#define COBS_DECODE_DST_BUF_LEN_MAX(SRC_LEN) (((SRC_LEN) == 0) ? 0u : ((SRC_LEN)-1u))
#define COBS_ENCODE_SRC_OFFSET(SRC_LEN) (((SRC_LEN) + 253u) / 254u)

typedef enum {
    COBS_ENCODE_OK = 0x00,
    COBS_ENCODE_NULL_POINTER = 0x01,
    COBS_ENCODE_OUT_BUFFER_OVERFLOW = 0x02
} cobs_encode_status;

typedef struct {
    size_t out_len;
    cobs_encode_status status;
} cobs_encode_result;

typedef enum {
    COBS_DECODE_OK = 0x00,
    COBS_DECODE_NULL_POINTER = 0x01,
    COBS_DECODE_OUT_BUFFER_OVERFLOW = 0x02,
    COBS_DECODE_ZERO_BYTE_IN_INPUT = 0x04,
    COBS_DECODE_INPUT_TOO_SHORT = 0x08
} cobs_decode_status;

typedef struct {
    size_t out_len;
    cobs_decode_status status;
} cobs_decode_result;

#ifdef __cplusplus
extern "C" {
#endif

/* COBS-encode a string of input bytes.
 *
 * dst_buf_ptr:    The buffer into which the result will be written
 * dst_buf_len:    Length of the buffer into which the result will be written
 * src_ptr:        The byte string to be encoded
 * src_len         Length of the byte string to be encoded
 *
 * returns:        A struct containing the success status of the encoding
 *                 operation and the length of the result (that was written to
 *                 dst_buf_ptr)
 */
cobs_encode_result cobs_encode(uint8_t *dst_buf_ptr, size_t dst_buf_len, const uint8_t *src_ptr, size_t src_len);

/* Decode a COBS byte string.
 *
 * dst_buf_ptr:    The buffer into which the result will be written
 * dst_buf_len:    Length of the buffer into which the result will be written
 * src_ptr:        The byte string to be decoded
 * src_len         Length of the byte string to be decoded
 *
 * returns:        A struct containing the success status of the decoding
 *                 operation and the length of the result (that was written to
 *                 dst_buf_ptr)
 */
cobs_decode_result cobs_decode(uint8_t *dst_buf_ptr, size_t dst_buf_len, const uint8_t *src_ptr, size_t src_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SENSECAP_INDICATOR */

#endif /* COBS_H_ */
