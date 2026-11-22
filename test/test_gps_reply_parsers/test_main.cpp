// GPS UBX and CAS reply parsers: getACK(class,id), getACKCas() and getACK(buffer,...).
//
// Under test: src/gps/GPS.cpp's reply matching, not setup(). Why: the GPS probe treats a NAK,
// a wrong class/id and a timeout differently, and a parser that accepts any frame leaves the
// receiver misconfigured. Regression guarded: a checksum mismatch accepted as an ACK, and the
// ACK/NAK message ids swapped.
#include "MeshTypes.h" // before TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "gps/GPS.h"
#include "support/FakeGpsSerial.h"

#if defined(ARCH_PORTDUINO)

class GpsTestShim : public GPS
{
  public:
    static void setSerial(HardwareSerial *serial) { _serial_gps = serial; }
    GPS_RESPONSE ack(uint8_t cls, uint8_t id, uint32_t waitMs) { return getACK(cls, id, waitMs); }
    GPS_RESPONSE ackCas(uint8_t cls, uint8_t id, uint32_t waitMs) { return getACKCas(cls, id, waitMs); }
    int ackPayload(uint8_t *buf, uint16_t size, uint8_t cls, uint8_t id, uint32_t waitMs)
    {
        return getACK(buf, size, cls, id, waitMs);
    }
};

namespace
{

// u-blox interface description: B5 62, class, id, length LE, payload, Fletcher-8 CK_A/CK_B.
std::vector<uint8_t> ubxFrame(uint8_t cls, uint8_t id, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> f = {0xB5, 0x62, cls, id, (uint8_t)payload.size(), (uint8_t)(payload.size() >> 8)};
    f.insert(f.end(), payload.begin(), payload.end());
    uint8_t a = 0, b = 0;
    for (size_t i = 2; i < f.size(); i++) {
        a += f[i];
        b += a;
    }
    f.push_back(a);
    f.push_back(b);
    return f;
}

std::vector<uint8_t> ubxAck(uint8_t cls, uint8_t id, bool ack)
{
    return ubxFrame(0x05, ack ? 0x01 : 0x00, {cls, id});
}

// CASIC ACK frame: BA CE, length(4) LE, class 0x05, message 0x01/0x00, payload class/id.
std::vector<uint8_t> casAck(uint8_t cls, uint8_t id, bool ack)
{
    std::vector<uint8_t> f = {0xBA, 0xCE, 0x04, 0x00, 0x05, (uint8_t)(ack ? 0x01 : 0x00), cls, id, 0, 0};
    uint32_t ck = ((uint32_t)f[5] << 24) + ((uint32_t)f[4] << 16) + f[2];
    ck += (uint32_t)f[6] | ((uint32_t)f[7] << 8) | ((uint32_t)f[8] << 16) | ((uint32_t)f[9] << 24);
    f.push_back(ck & 0xFF);
    f.push_back((ck >> 8) & 0xFF);
    f.push_back((ck >> 16) & 0xFF);
    f.push_back((ck >> 24) & 0xFF);
    return f;
}

FakeGpsSerial serial;
GpsTestShim *gpsShim = nullptr;
} // namespace

void setUp(void)
{
    gpsShim = new GpsTestShim();
    GpsTestShim::setSerial(&serial);
    serial.feed({});
}

void tearDown(void)
{
    delete gpsShim;
    gpsShim = nullptr;
}

void test_ubx_ack_ack_and_nak(void)
{
    serial.feed(ubxAck(0x06, 0x8B, true));
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_OK, gpsShim->ack(0x06, 0x8B, 20));

    serial.feed(ubxAck(0x06, 0x8B, false));
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_NAK, gpsShim->ack(0x06, 0x8B, 20));
}

void test_ubx_wrong_class_id_and_checksum_are_not_acks(void)
{
    serial.feed(ubxAck(0x06, 0x8B, true));
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_NONE, gpsShim->ack(0x06, 0x8A, 20));

    auto bad = ubxAck(0x06, 0x8B, true);
    bad.back() ^= 0xFF; // CK_B
    serial.feed(bad);
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_NONE, gpsShim->ack(0x06, 0x8B, 20));
}

void test_ubx_timeout_with_no_bytes(void)
{
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_NONE, gpsShim->ack(0x06, 0x8B, 1));
}

void test_cas_ack_and_nak(void)
{
    serial.feed(casAck(0x06, 0x8B, true));
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_OK, gpsShim->ackCas(0x06, 0x8B, 20));

    serial.feed(casAck(0x06, 0x8B, false));
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_NAK, gpsShim->ackCas(0x06, 0x8B, 20));
}

void test_cas_wrong_class_id_is_not_an_ack(void)
{
    serial.feed(casAck(0x06, 0x8B, true));
    TEST_ASSERT_EQUAL(GNSS_RESPONSE_NONE, gpsShim->ackCas(0x06, 0x8A, 20));
}

void test_buffer_ack_returns_payload_length(void)
{
    const std::vector<uint8_t> payload = {0x11, 0x22, 0x33, 0x44};
    serial.feed(ubxFrame(0x01, 0x02, payload));
    uint8_t out[8] = {};
    TEST_ASSERT_EQUAL(4, gpsShim->ackPayload(out, sizeof(out), 0x01, 0x02, 20));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), out, payload.size());
}

void test_buffer_ack_wrong_class_times_out(void)
{
    serial.feed(ubxFrame(0x01, 0x02, {0x11}));
    uint8_t out[8] = {};
    TEST_ASSERT_EQUAL(0, gpsShim->ackPayload(out, sizeof(out), 0x01, 0x03, 1));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_ubx_ack_ack_and_nak);
    RUN_TEST(test_ubx_wrong_class_id_and_checksum_are_not_acks);
    RUN_TEST(test_ubx_timeout_with_no_bytes);
    RUN_TEST(test_cas_ack_and_nak);
    RUN_TEST(test_cas_wrong_class_id_is_not_an_ack);
    RUN_TEST(test_buffer_ack_returns_payload_length);
    RUN_TEST(test_buffer_ack_wrong_class_times_out);
    exit(UNITY_END());
}

void loop() {}

#else

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
