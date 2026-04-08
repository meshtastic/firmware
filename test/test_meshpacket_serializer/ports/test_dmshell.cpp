#include "../test_helpers.h"
#include "mesh/mesh-pb-constants.h"

namespace
{
struct BytesDecodeState {
    uint8_t *buffer;
    size_t capacity;
    size_t length;
};

struct BytesEncodeState {
    const uint8_t *buffer;
    size_t length;
};

bool decodeBytesField(pb_istream_t *stream, const pb_field_iter_t *field, void **arg)
{
    (void)field;
    auto *state = static_cast<BytesDecodeState *>(*arg);
    if (!state) {
        return false;
    }

    const size_t fieldLen = stream->bytes_left;
    if (fieldLen > state->capacity) {
        return false;
    }

    if (!pb_read(stream, state->buffer, fieldLen)) {
        return false;
    }

    state->length = fieldLen;
    return true;
}

bool encodeBytesField(pb_ostream_t *stream, const pb_field_iter_t *field, void *const *arg)
{
    auto *state = static_cast<const BytesEncodeState *>(*arg);
    if (!state || !state->buffer || state->length == 0) {
        return true;
    }

    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }

    return pb_encode_string(stream, state->buffer, state->length);
}

void assert_dmshell_roundtrip(meshtastic_DMShell_OpCode op, uint32_t sessionId, uint32_t seq, const uint8_t *payload,
                              size_t payloadLen, uint32_t cols = 0, uint32_t rows = 0)
{
    meshtastic_DMShell tx = meshtastic_DMShell_init_zero;
    tx.op = op;
    tx.session_id = sessionId;
    tx.seq = seq;
    tx.cols = cols;
    tx.rows = rows;

    BytesEncodeState txPayload = {payload, payloadLen};
    if (payload && payloadLen > 0) {
        tx.payload.funcs.encode = encodeBytesField;
        tx.payload.arg = &txPayload;
    }

    uint8_t encoded[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};
    size_t encodedLen = pb_encode_to_bytes(encoded, sizeof(encoded), meshtastic_DMShell_fields, &tx);
    TEST_ASSERT_GREATER_THAN_UINT32(0, encodedLen);

    meshtastic_DMShell rx = meshtastic_DMShell_init_zero;
    uint8_t decodedPayload[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};
    BytesDecodeState rxPayload = {decodedPayload, sizeof(decodedPayload), 0};
    rx.payload.funcs.decode = decodeBytesField;
    rx.payload.arg = &rxPayload;

    TEST_ASSERT_TRUE(pb_decode_from_bytes(encoded, encodedLen, meshtastic_DMShell_fields, &rx));
    TEST_ASSERT_EQUAL(op, rx.op);
    TEST_ASSERT_EQUAL_UINT32(sessionId, rx.session_id);
    TEST_ASSERT_EQUAL_UINT32(seq, rx.seq);
    TEST_ASSERT_EQUAL_UINT32(cols, rx.cols);
    TEST_ASSERT_EQUAL_UINT32(rows, rx.rows);
    TEST_ASSERT_EQUAL_UINT32(payloadLen, rxPayload.length);

    if (payloadLen > 0) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decodedPayload, payloadLen);
    }
}
} // namespace

void test_dmshell_open_roundtrip()
{
    assert_dmshell_roundtrip(meshtastic_DMShell_OpCode_OPEN, 0x101, 1, nullptr, 0, 120, 40);
}

void test_dmshell_input_roundtrip()
{
    const uint8_t payload[] = {'l', 's', '\n'};
    assert_dmshell_roundtrip(meshtastic_DMShell_OpCode_INPUT, 0x202, 2, payload, sizeof(payload));
}

void test_dmshell_resize_roundtrip()
{
    assert_dmshell_roundtrip(meshtastic_DMShell_OpCode_RESIZE, 0x303, 3, nullptr, 0, 180, 55);
}

void test_dmshell_close_roundtrip()
{
    const uint8_t reason[] = {'b', 'y', 'e'};
    assert_dmshell_roundtrip(meshtastic_DMShell_OpCode_CLOSE, 0x404, 4, reason, sizeof(reason));
}